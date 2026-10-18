#include "physics/Player.hpp"

#include "gameplay/Armor.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace betacraft::physics {

Player::Player() noexcept = default;

void Player::setGameMode(core::GameMode mode) noexcept {
    m_gameMode = mode;
    if (mode == core::GameMode::Survival) {
        m_flying = false;
    }
}

void Player::setPosition(const glm::vec3& pos) noexcept {
    m_position = pos;
    m_prevPosition = pos;
    m_prevEyeHeightCurrent = m_eyeHeightCurrent;
    m_velocity = glm::vec3(0.0f);
    m_fallDistance = 0.0f;
    m_pendingFall = 0.0f; // A teleport lands nowhere
}

void Player::setRotation(float yawDeg, float pitchDeg) noexcept {
    m_yaw = yawDeg;
    m_pitch = std::clamp(pitchDeg, -89.4f, 89.4f);
}

void Player::rotate(float deltaYawDeg, float deltaPitchDeg) noexcept {
    m_yaw = std::fmod(m_yaw + deltaYawDeg, 360.0f);
    m_pitch = std::clamp(m_pitch + deltaPitchDeg, -89.4f, 89.4f);
}

AABB Player::getAABBAt(const glm::vec3& pos) noexcept {
    constexpr float halfW = core::EngineConfig::PLAYER_WIDTH * 0.5f;
    constexpr float h = core::EngineConfig::PLAYER_HEIGHT;
    return AABB{
        .min = {pos.x - halfW, pos.y, pos.z - halfW},
        .max = {pos.x + halfW, pos.y + h, pos.z + halfW}
    };
}

AABB Player::getAABB() const noexcept {
    return getAABBAt(m_position);
}

glm::vec3 Player::getEyePosition() const noexcept {
    return getInterpolatedEyePosition(m_renderAlpha);
}

glm::vec3 Player::getInterpolatedPosition(float alpha) const noexcept {
    return glm::mix(m_prevPosition, m_position, std::clamp(alpha, 0.0f, 1.0f));
}

glm::vec3 Player::getInterpolatedEyePosition(float alpha) const noexcept {
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const glm::vec3 pos = glm::mix(m_prevPosition, m_position, clampedAlpha);
    const float eyeH = glm::mix(m_prevEyeHeightCurrent, m_eyeHeightCurrent, clampedAlpha);
    return pos + glm::vec3(0.0f, eyeH, 0.0f);
}

glm::vec3 Player::getCameraEyePosition(float alpha) const noexcept {
    glm::vec3 eye = getInterpolatedEyePosition(alpha);
    if (m_bobIntensity > 0.001f) {
        const float bobY = std::abs(std::cos(m_bobPhase)) * 0.055f * m_bobIntensity;
        eye.y += bobY;
    }
    return eye;
}

glm::vec3 Player::getLookDirection() const noexcept {
    const float yawRad = glm::radians(m_yaw);
    const float pitchRad = glm::radians(m_pitch);
    glm::vec3 dir{
        std::cos(pitchRad) * std::cos(yawRad),
        std::sin(pitchRad),
        std::cos(pitchRad) * std::sin(yawRad)
    };
    return glm::normalize(dir);
}

float Player::hurtCameraRollDeg(float alpha) const noexcept {
    const float t = static_cast<float>(m_hurtTime) - alpha;
    if (!(t >= 0.0f)) {
        return 0.0f;
    }
    const float f = t / static_cast<float>(HURT_TIME_TICKS);
    const float f4 = f * f * f * f;
    return -std::sin(f4 * glm::pi<float>()) * 14.0f;
}

void Player::applyKnockback(const glm::vec3& impulse) noexcept {
    const auto finite = [](float v) { return std::isfinite(v) ? v : 0.0f; };
    const glm::vec3 push(finite(impulse.x), finite(impulse.y), finite(impulse.z));
    m_velocity = m_velocity * 0.5f + push;
    m_velocity.y = std::min(m_velocity.y, KNOCKBACK_MAX_UPWARD_SPEED);
    if (push.y > 0.0f) {
        m_onGround = false;
    }
}

void Player::addVelocity(const glm::vec3& delta) noexcept {
    const auto finite = [](float v) { return std::isfinite(v) ? v : 0.0f; };
    const glm::vec3 push(finite(delta.x), finite(delta.y), finite(delta.z));
    m_velocity += push;
    if (push.y > 0.0f) {
        m_onGround = false;
    }
}

glm::mat4 Player::getInterpolatedViewMatrix(float alpha) const noexcept {
    const glm::vec3 eye = getCameraEyePosition(alpha);
    const glm::vec3 dir = getLookDirection();
    const glm::mat4 view = glm::lookAt(eye, eye + dir, glm::vec3(0.0f, 1.0f, 0.0f));
    const float roll = hurtCameraRollDeg(m_tickAlpha);
    if (roll == 0.0f) {
        return view;
    }
    // Beta rotates the camera around Z in eye space, i.e. around the line of sight.
    return glm::rotate(glm::mat4(1.0f), glm::radians(roll), glm::vec3(0.0f, 0.0f, 1.0f)) * view;
}

glm::mat4 Player::getViewMatrix() const noexcept {
    return getInterpolatedViewMatrix(m_renderAlpha);
}

bool Player::collidesWithWorld(const AABB& box, const world::World& world) const noexcept {
    const int32_t minX = static_cast<int32_t>(std::floor(box.min.x));
    const int32_t maxX = static_cast<int32_t>(std::floor(box.max.x - 1e-5f));
    // One cell lower than the box: a fence's collision box rises 0.5 into the cell above it
    // (Beta getCollidingBoundingBoxes scans from minY - 1 for the same reason).
    const int32_t minY = static_cast<int32_t>(std::floor(box.min.y)) - 1;
    const int32_t maxY = static_cast<int32_t>(std::floor(box.max.y - 1e-5f));
    const int32_t minZ = static_cast<int32_t>(std::floor(box.min.z));
    const int32_t maxZ = static_cast<int32_t>(std::floor(box.max.z - 1e-5f));

    for (int32_t y = minY; y <= maxY; ++y) {
        for (int32_t z = minZ; z <= maxZ; ++z) {
            for (int32_t x = minX; x <= maxX; ++x) {
                const world::BlockId id = world.getBlock(x, y, z);
                if (!world::BlockRegistry::isSolid(id)) {
                    continue;
                }
                const uint8_t meta = world::shapeDependsOnMetadata(world::BlockRegistry::get(id).shape)
                    ? world.getMetadata(x, y, z)
                    : uint8_t{0};
                const bool hit = AABB::forEachBlockBox(x, y, z, id, meta, [&](AABB blockBox) {
                    if (id == world::BlockId::Cactus) {
                        // Beta BlockCactus.getCollisionBoundingBox is also 1/16 short on top
                        // (the selection box is not), so standing on a cactus is contact too.
                        blockBox.max.y -= 1.0f / 16.0f;
                    }
                    return box.intersects(blockBox);
                });
                if (hit) {
                    return true;
                }
            }
        }
    }
    return false;
}

StepEvent Player::update(float dt, const PlayerInput& input, const world::World& world) noexcept {
    return fixedUpdate(dt, input, world);
}

StepEvent Player::fixedUpdate(float dt, const PlayerInput& input, const world::World& world) noexcept {
    m_prevPosition = m_position;
    m_prevEyeHeightCurrent = m_eyeHeightCurrent;

    StepEvent stepEvent{};
    dt = std::clamp(dt, 0.0f, 0.05f);

    if (m_swingTimer > 0.0f) {
        m_swingTimer = std::max(0.0f, m_swingTimer - dt * 3.8f);
    }

    // Check liquid status (include lower feet so surface jump boost works up to the top of the water block)
    const int32_t bx = static_cast<int32_t>(std::floor(m_position.x));
    const int32_t byFeetLow = static_cast<int32_t>(std::floor(m_position.y + 0.08f));
    const int32_t byFeetMid = static_cast<int32_t>(std::floor(m_position.y + 0.50f));
    const int32_t byWaist   = static_cast<int32_t>(std::floor(m_position.y + 0.85f));
    const int32_t byEye     = static_cast<int32_t>(std::floor(m_position.y + m_eyeHeightCurrent));
    const int32_t bz = static_cast<int32_t>(std::floor(m_position.z));

    const world::BlockId feetLow = world.getBlock(bx, byFeetLow, bz);
    const world::BlockId feetMid = world.getBlock(bx, byFeetMid, bz);
    // Both liquids are swum through; only water splashes, drowns and slows digging (Beta
    // Material.water checks).
    m_inWater = world::BlockRegistry::isLiquid(feetLow) || world::BlockRegistry::isLiquid(feetMid);
    const bool wasInWaterMaterial = m_inWaterMaterial;
    m_inWaterMaterial = world::isWaterBlock(feetLow) || world::isWaterBlock(feetMid);
    const bool atWaterSurface = m_inWater && !world::BlockRegistry::isLiquid(world.getBlock(bx, byWaist, bz));
    m_headUnderWater = world::isWaterBlock(world.getBlock(bx, byEye, bz));
    m_inLava = world::isLavaBlock(feetLow) || world::isLavaBlock(feetMid);
    const int32_t byFeet = static_cast<int32_t>(std::floor(m_position.y));
    m_onLadder = world.getBlock(bx, byFeet, bz) == world::BlockId::Ladder ||
                 world.getBlock(bx, byEye, bz) == world::BlockId::Ladder;

    if (m_inWaterMaterial && !wasInWaterMaterial && m_velocity.y < -2.5f) {
        stepEvent.splashedWater = true;
    }

    m_sneaking = input.sneak && !m_flying && !m_inWater;
    const float targetEyeHeight = m_sneaking
        ? core::EngineConfig::PLAYER_SNEAK_EYE_HEIGHT
        : core::EngineConfig::PLAYER_EYE_HEIGHT;
    m_eyeHeightCurrent += (targetEyeHeight - m_eyeHeightCurrent) * std::min(1.0f, dt * 16.0f);

    // Compute horizontal movement vector in XZ plane
    const float yawRad = glm::radians(m_yaw);
    const glm::vec3 forwardXZ(std::cos(yawRad), 0.0f, std::sin(yawRad));
    const glm::vec3 rightXZ(-forwardXZ.z, 0.0f, forwardXZ.x);

    glm::vec3 wishDir(0.0f);
    if (input.forward)  wishDir += forwardXZ;
    if (input.backward) wishDir -= forwardXZ;
    if (input.right)    wishDir += rightXZ;
    if (input.left)     wishDir -= rightXZ;
    if (glm::length(wishDir) > 1e-4f) {
        wishDir = glm::normalize(wishDir);
    }

    float moveSpeed = core::EngineConfig::WALK_SPEED;
    if (m_flying) {
        moveSpeed = input.sprint ? (core::EngineConfig::FLY_SPEED * 1.75f) : core::EngineConfig::FLY_SPEED;
    } else if (m_inWater) {
        moveSpeed = atWaterSurface ? (core::EngineConfig::WALK_SPEED * 0.85f) : core::EngineConfig::SWIM_SPEED;
    } else if (m_sneaking) {
        moveSpeed = core::EngineConfig::SNEAK_SPEED;
    } else if (input.sprint && input.forward) {
        moveSpeed = core::EngineConfig::SPRINT_SPEED;
    }

    const float accel = (m_onGround || m_flying) ? 18.0f : 6.0f;
    m_velocity.x = glm::mix(m_velocity.x, wishDir.x * moveSpeed, std::min(1.0f, dt * accel));
    m_velocity.z = glm::mix(m_velocity.z, wishDir.z * moveSpeed, std::min(1.0f, dt * accel));

    if (m_flying) {
        float verticalWish = 0.0f;
        if (input.jump)  verticalWish += 1.0f;
        if (input.sneak) verticalWish -= 1.0f;
        m_velocity.y = verticalWish * (core::EngineConfig::FLY_SPEED * 0.75f);
        m_fallDistance = 0.0f;
    } else if (m_inWater) {
        m_fallDistance = 0.0f;
        m_velocity.y -= core::EngineConfig::GRAVITY * 0.22f * dt;
        m_velocity.y *= std::pow(0.80f, dt * 20.0f);
        if (input.jump) {
            if (atWaterSurface || m_onGround) {
                m_velocity.y = core::EngineConfig::JUMP_VELOCITY;
            } else {
                m_velocity.y = 4.8f;
            }
        } else if (input.sneak) {
            m_velocity.y = -3.2f;
        }
    } else {
        // Standard gravity + jumping
        if (input.jump && m_onGround) {
            m_velocity.y = core::EngineConfig::JUMP_VELOCITY;
            m_onGround = false;
        }
        m_velocity.y -= core::EngineConfig::GRAVITY * dt;
        m_velocity.y = std::max(m_velocity.y, -core::EngineConfig::TERMINAL_VELOCITY);
        if (m_onLadder) {
            // S5.5 (Beta EntityLiving.moveEntityWithHeading): slow descent, no fall damage,
            // and a sneaking player holds on.
            m_velocity.y = std::max(m_velocity.y, -LADDER_MAX_DESCENT_SPEED);
            if (m_sneaking) {
                m_velocity.y = std::max(m_velocity.y, 0.0f);
            }
            m_fallDistance = 0.0f;
        }
    }

    glm::vec3 delta = m_velocity * dt;

    // Authentic Beta 1.7.3 Sneak Ledge Guard: prevent walking off edges while sneaking on ground
    if (m_sneaking && m_onGround) {
        constexpr float stepCheck = 0.06f;
        if (delta.x != 0.0f) {
            const AABB shiftedX = getAABBAt(m_position + glm::vec3(delta.x, -stepCheck, 0.0f));
            if (!collidesWithWorld(shiftedX, world)) {
                delta.x = 0.0f;
                m_velocity.x = 0.0f;
            }
        }
        if (delta.z != 0.0f) {
            const AABB shiftedZ = getAABBAt(m_position + glm::vec3(0.0f, -stepCheck, delta.z));
            if (!collidesWithWorld(shiftedZ, world)) {
                delta.z = 0.0f;
                m_velocity.z = 0.0f;
            }
        }
    }

    const glm::vec3 oldPos = m_position;

    // Resolve Y axis first (standard Minecraft entity movement order) with binary sub-stepping on contact
    m_onGround = false;
    if (delta.y != 0.0f) {
        const glm::vec3 candidatePos = m_position + glm::vec3(0.0f, delta.y, 0.0f);
        if (!collidesWithWorld(getAABBAt(candidatePos), world)) {
            if (delta.y < 0.0f && !m_flying && !m_inWater) {
                m_fallDistance += -delta.y;
            }
            m_position.y = candidatePos.y;
        } else {
            // Sub-step to land flush with the block surface
            float stepY = delta.y * 0.5f;
            for (int i = 0; i < 5; ++i) {
                if (!collidesWithWorld(getAABBAt(m_position + glm::vec3(0.0f, stepY, 0.0f)), world)) {
                    m_position.y += stepY;
                }
                stepY *= 0.5f;
            }
            if (delta.y < 0.0f) {
                m_onGround = true;
                // The 20 TPS game tick turns the landing into damage (gameplay::fallDamage);
                // two landings between ticks keep the larger fall.
                if (!m_flying && !m_inWater) {
                    m_pendingFall = std::max(m_pendingFall, m_fallDistance);
                    if (m_fallDistance > 0.0f) {
                        stepEvent.landedFallDistance = m_fallDistance;
                        stepEvent.landedOn = {static_cast<int32_t>(std::floor(m_position.x)),
                                              static_cast<int32_t>(std::floor(m_position.y - 0.2f)),
                                              static_cast<int32_t>(std::floor(m_position.z))};
                    }
                }
                m_fallDistance = 0.0f;
            }
            m_velocity.y = 0.0f;
        }
    } else {
        const AABB groundProbe = getAABBAt(m_position + glm::vec3(0.0f, -0.02f, 0.0f));
        m_onGround = collidesWithWorld(groundProbe, world);
    }

    bool collidedHorizontally = false;

    // Resolve X axis
    if (delta.x != 0.0f) {
        const glm::vec3 candidatePos = m_position + glm::vec3(delta.x, 0.0f, 0.0f);
        if (!collidesWithWorld(getAABBAt(candidatePos), world)) {
            m_position.x = candidatePos.x;
        } else {
            float stepX = delta.x * 0.5f;
            for (int i = 0; i < 4; ++i) {
                if (!collidesWithWorld(getAABBAt(m_position + glm::vec3(stepX, 0.0f, 0.0f)), world)) {
                    m_position.x += stepX;
                }
                stepX *= 0.5f;
            }
            m_velocity.x = 0.0f;
            collidedHorizontally = true;
        }
    }

    // Resolve Z axis
    if (delta.z != 0.0f) {
        const glm::vec3 candidatePos = m_position + glm::vec3(0.0f, 0.0f, delta.z);
        if (!collidesWithWorld(getAABBAt(candidatePos), world)) {
            m_position.z = candidatePos.z;
        } else {
            float stepZ = delta.z * 0.5f;
            for (int i = 0; i < 4; ++i) {
                if (!collidesWithWorld(getAABBAt(m_position + glm::vec3(0.0f, 0.0f, stepZ)), world)) {
                    m_position.z += stepZ;
                }
                stepZ *= 0.5f;
            }
            m_velocity.z = 0.0f;
            collidedHorizontally = true;
        }
    }

    // Pushing against the wall while on a ladder climbs it (Beta: motionY = 0.2 per tick).
    if (m_onLadder && collidedHorizontally && !m_flying && !m_inWater) {
        m_velocity.y = LADDER_CLIMB_SPEED;
    }

    // Beta 1.7.3 water-exit ledge boost when pressing against a shore block in water
    if (m_inWater && collidedHorizontally && input.jump) {
        m_velocity.y = core::EngineConfig::JUMP_VELOCITY;
    }

    // Creative safety net above the void; in Survival the void kills (game tick).
    if (isCreative() && m_position.y < -15.0f) {
        m_position.y = 95.0f;
        m_prevPosition = m_position;
        m_prevEyeHeightCurrent = m_eyeHeightCurrent;
        m_velocity.y = 0.0f;
        m_fallDistance = 0.0f;
    }

    // Beta Entity.moveEntity: every cell of the box shrunk by 0.001 gets
    // onEntityCollidedWithBlock; the cactus collision box is inset so contact reaches it.
    {
        const AABB box = getAABB();
        constexpr float shrink = 0.001f;
        const int32_t x0 = static_cast<int32_t>(std::floor(box.min.x + shrink));
        const int32_t x1 = static_cast<int32_t>(std::floor(box.max.x - shrink));
        const int32_t y0 = static_cast<int32_t>(std::floor(box.min.y + shrink));
        const int32_t y1 = static_cast<int32_t>(std::floor(box.max.y - shrink));
        const int32_t z0 = static_cast<int32_t>(std::floor(box.min.z + shrink));
        const int32_t z1 = static_cast<int32_t>(std::floor(box.max.z - shrink));
        m_touchingCactus = false;
        for (int32_t y = y0; y <= y1 && !m_touchingCactus; ++y) {
            for (int32_t z = z0; z <= z1 && !m_touchingCactus; ++z) {
                for (int32_t x = x0; x <= x1; ++x) {
                    if (world.getBlock(x, y, z) == world::BlockId::Cactus) {
                        m_touchingCactus = true;
                        break;
                    }
                }
            }
        }
    }

    // View bobbing & footsteps
    const float horizDist = glm::length(glm::vec2(m_position.x - oldPos.x, m_position.z - oldPos.z));
    if (m_onGround && horizDist > 1e-4f && !m_flying) {
        m_bobPhase += horizDist * 3.6f;
        m_bobIntensity = std::min(1.0f, m_bobIntensity + dt * 6.0f);
        m_stepAccumulator += horizDist;
        if (m_stepAccumulator >= 1.85f) {
            m_stepAccumulator = 0.0f;
            stepEvent.triggered = true;
            const int32_t gx = static_cast<int32_t>(std::floor(m_position.x));
            const int32_t gy = static_cast<int32_t>(std::floor(m_position.y - 0.1f));
            const int32_t gz = static_cast<int32_t>(std::floor(m_position.z));
            world::BlockId b = world.getBlock(gx, gy, gz);
            if (b == world::BlockId::Air) {
                b = world::BlockId::Stone;
            }
            stepEvent.groundBlock = b;
        }
    } else {
        m_bobIntensity = std::max(0.0f, m_bobIntensity - dt * 8.0f);
    }

    return stepEvent;
}

void Player::setSelectedSlot(int32_t slot) noexcept {
    m_selectedSlot = core::floorMod(slot, static_cast<int32_t>(HOTBAR_SIZE));
}

void Player::scrollSelectedSlot(int32_t delta) noexcept {
    setSelectedSlot(m_selectedSlot + delta);
}

bool Player::selectHotbarSlotHolding(gameplay::ItemId id) noexcept {
    if (id == gameplay::kNoItem) {
        return false;
    }
    const auto holds = [&](size_t i) { return !m_inventory[i].isEmpty() && m_inventory[i].id == id; };
    if (holds(static_cast<size_t>(m_selectedSlot))) {
        return true;
    }
    for (size_t i = 0; i < HOTBAR_SIZE; ++i) {
        if (holds(i)) {
            m_selectedSlot = static_cast<int32_t>(i);
            return true;
        }
    }
    return false;
}

InventorySlot Player::getHotbarSlot(size_t index) const noexcept {
    if (index >= HOTBAR_SIZE) return {};
    return m_inventory[index];
}

InventorySlot Player::getInventorySlot(size_t index) const noexcept {
    if (index >= TOTAL_INVENTORY_SIZE) return {};
    return m_inventory[index];
}

void Player::setInventorySlot(size_t index, InventorySlot slot) noexcept {
    if (index >= TOTAL_INVENTORY_SIZE) return;
    m_inventory[index] = slot.isEmpty() ? InventorySlot{} : slot;
}

void Player::setArmorSlot(size_t index, InventorySlot slot) noexcept {
    if (index >= ARMOR_SLOT_COUNT) return;
    m_armor[index] = slot.isEmpty() ? InventorySlot{} : slot;
}

int32_t Player::armorValue() const noexcept {
    std::array<gameplay::ItemStack, ARMOR_SLOT_COUNT> worn{};
    for (size_t i = 0; i < ARMOR_SLOT_COUNT; ++i) {
        if (!m_armor[i].isEmpty()) {
            worn[i] = gameplay::makeArmorStack(m_armor[i].id, m_armor[i].damage);
        }
    }
    return gameplay::totalArmorValue(worn);
}

bool Player::addItem(gameplay::ItemId id, int32_t count) noexcept {
    if (id == gameplay::kNoItem || count <= 0) return false;
    return insertStack({id, count}) == 0;
}

int32_t Player::insertStack(InventorySlot stack) noexcept {
    if (stack.isEmpty()) {
        return 0;
    }
    const int32_t maxStack = std::max(1, gameplay::maxStackSize(stack.id));
    int32_t count = stack.count;
    for (auto& slot : m_inventory) {
        if (count == 0) return 0;
        if (!slot.isEmpty() && slot.id == stack.id && slot.damage == stack.damage && slot.count < maxStack) {
            const int32_t moved = std::min(count, maxStack - slot.count);
            slot.count += moved;
            count -= moved;
        }
    }
    for (auto& slot : m_inventory) {
        if (count == 0) return 0;
        if (slot.isEmpty()) {
            const int32_t moved = std::min(count, maxStack);
            slot = InventorySlot{stack.id, moved, stack.damage};
            count -= moved;
        }
    }
    return count;
}

int32_t Player::insertStackInRange(InventorySlot stack, size_t first, size_t last, bool mergeOnly) noexcept {
    if (stack.isEmpty()) {
        return 0;
    }
    last = std::min(last, TOTAL_INVENTORY_SIZE);
    const int32_t maxStack = std::max(1, gameplay::maxStackSize(stack.id));
    int32_t count = stack.count;
    for (size_t i = first; i < last && count > 0; ++i) {
        InventorySlot& slot = m_inventory[i];
        if (!slot.isEmpty() && slot.id == stack.id && slot.damage == stack.damage && slot.count < maxStack) {
            const int32_t moved = std::min(count, maxStack - slot.count);
            slot.count += moved;
            count -= moved;
        }
    }
    for (size_t i = first; !mergeOnly && i < last && count > 0; ++i) {
        InventorySlot& slot = m_inventory[i];
        if (slot.isEmpty()) {
            const int32_t moved = std::min(count, maxStack);
            slot = InventorySlot{stack.id, moved, stack.damage};
            count -= moved;
        }
    }
    return count;
}

bool Player::consumeHeldItem(int32_t count) noexcept {
    auto& slot = m_inventory[static_cast<size_t>(m_selectedSlot)];
    if (slot.isEmpty() || slot.count < count) {
        return false;
    }
    slot.count -= count;
    if (slot.count <= 0) {
        slot = {};
    }
    return true;
}

bool Player::consumeItem(gameplay::ItemId id, int32_t count) noexcept {
    if (countItem(id) < count) {
        return false;
    }
    int32_t remaining = count;
    for (auto& slot : m_inventory) {
        if (slot.id == id && slot.count > 0) {
            const int32_t take = std::min(remaining, slot.count);
            slot.count -= take;
            remaining -= take;
            if (slot.count <= 0) {
                slot = {};
            }
            if (remaining == 0) {
                return true;
            }
        }
    }
    return remaining == 0;
}

int32_t Player::countItem(gameplay::ItemId id) const noexcept {
    int32_t total = 0;
    for (const auto& slot : m_inventory) {
        if (slot.id == id && slot.count > 0) {
            total += slot.count;
        }
    }
    return total;
}

} // namespace betacraft::physics
