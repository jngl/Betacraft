#pragma once

#include "core/Config.hpp"
#include "gameplay/Health.hpp"
#include "gameplay/ItemIds.hpp"
#include "gameplay/Survival.hpp"
#include "math/AABB.hpp"
#include "world/Block.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace betacraft::physics {

// One inventory stack. Ids follow the shared item scheme (gameplay/ItemIds.hpp): blocks keep
// their BlockId number, tools and items at 256 and above use their Beta item number.
struct InventorySlot {
    gameplay::ItemId id{gameplay::kNoItem};
    int32_t count{0};
    int32_t damage{0}; // Tool wear (Beta itemDamage); always 0 for items that do not wear

    constexpr InventorySlot() noexcept = default;
    constexpr InventorySlot(gameplay::ItemId item, int32_t n, int32_t wear = 0) noexcept
        : id(item), count(n), damage(wear) {}
    constexpr InventorySlot(world::BlockId block, int32_t n) noexcept
        : id(gameplay::toItemId(block)), count(n) {}

    [[nodiscard]] constexpr bool isEmpty() const noexcept {
        return id == gameplay::kNoItem || count <= 0;
    }

    constexpr bool operator==(const InventorySlot&) const noexcept = default;
};

struct PlayerInput {
    bool forward{false};
    bool backward{false};
    bool left{false};
    bool right{false};
    bool jump{false};
    bool sneak{false};
    bool sprint{false};
};

struct StepEvent {
    bool triggered{false};
    world::BlockId groundBlock{world::BlockId::Grass};
    bool splashedWater{false};
    // S5.7: fall distance of a landing during this step (0 without one) and the cell the feet
    // came down on (Beta Entity.fall: floor(y - 0.2)), for trampling farmland.
    float landedFallDistance{0.0f};
    core::BlockPos landedOn{};
};

class Player {
public:
    static constexpr size_t HOTBAR_SIZE = 9;
    static constexpr size_t TOTAL_INVENTORY_SIZE = 36;

    // Starts with an empty inventory, in Survival; game::applyStarterKit fills a new world's.
    Player() noexcept;

    // Survival forces flying off. Every cheat is gated on isCreative() (roadmap conventions §3).
    void setGameMode(core::GameMode mode) noexcept;
    [[nodiscard]] core::GameMode gameMode() const noexcept { return m_gameMode; }
    [[nodiscard]] bool isCreative() const noexcept { return m_gameMode == core::GameMode::Creative; }

    void setPosition(const glm::vec3& pos) noexcept;
    [[nodiscard]] const glm::vec3& getPosition() const noexcept { return m_position; }

    void setRotation(float yawDeg, float pitchDeg) noexcept;
    void rotate(float deltaYawDeg, float deltaPitchDeg) noexcept;
    [[nodiscard]] float getYaw() const noexcept { return m_yaw; }
    [[nodiscard]] float getPitch() const noexcept { return m_pitch; }

    [[nodiscard]] math::AABB getAABB() const noexcept;
    [[nodiscard]] static math::AABB getAABBAt(const glm::vec3& pos) noexcept;

    [[nodiscard]] const glm::vec3& getVelocity() const noexcept { return m_velocity; }

    // Gameplay eye (raycasts, melee, drops, acoustics): never includes the view bobbing.
    [[nodiscard]] glm::vec3 getEyePosition() const noexcept;
    [[nodiscard]] glm::vec3 getLookDirection() const noexcept;
    [[nodiscard]] glm::mat4 getViewMatrix() const noexcept;

    // Render transform interpolation
    [[nodiscard]] glm::vec3 getInterpolatedPosition(float alpha = 1.0f) const noexcept;
    [[nodiscard]] glm::vec3 getInterpolatedEyePosition(float alpha = 1.0f) const noexcept;
    // Camera eye: the gameplay eye plus the walking bob. Only rendering uses it.
    [[nodiscard]] glm::vec3 getCameraEyePosition(float alpha = 1.0f) const noexcept;
    [[nodiscard]] glm::mat4 getInterpolatedViewMatrix(float alpha = 1.0f) const noexcept;

    void setRenderAlpha(float alpha) noexcept { m_renderAlpha = std::clamp(alpha, 0.0f, 1.0f); }
    [[nodiscard]] float getRenderAlpha() const noexcept { return m_renderAlpha; }

    void resetInterpolationHistory() noexcept {
        m_prevPosition = m_position;
        m_prevEyeHeightCurrent = m_eyeHeightCurrent;
    }

    StepEvent fixedUpdate(float dt, const PlayerInput& input, const world::World& world) noexcept;
    StepEvent update(float dt, const PlayerInput& input, const world::World& world) noexcept;

    // Creative only: in Survival nothing changes and it returns false.
    bool toggleFlyMode() noexcept {
        if (!isCreative()) {
            return false;
        }
        m_flying = !m_flying;
        m_velocity.y = 0.0f;
        m_fallDistance = 0.0f;
        return true;
    }
    [[nodiscard]] bool isFlying() const noexcept { return m_flying; }
    [[nodiscard]] bool isOnGround() const noexcept { return m_onGround; }
    [[nodiscard]] bool isInWater() const noexcept { return m_inWater; }
    [[nodiscard]] bool isHeadUnderWater() const noexcept { return m_headUnderWater; }
    [[nodiscard]] bool isSneaking() const noexcept { return m_sneaking; }
    // Water only (isInWater also counts lava): what puts a burning player out.
    [[nodiscard]] bool isInWaterMaterial() const noexcept { return m_inWaterMaterial; }
    // Lava at the feet (feetLow or feetMid cell), refreshed by fixedUpdate.
    [[nodiscard]] bool isInLava() const noexcept { return m_inLava; }
    // A Cactus cell overlaps the box shrunk by 0.001 (Beta onEntityCollidedWithBlock).
    [[nodiscard]] bool isTouchingCactus() const noexcept { return m_touchingCactus; }
    // S5.5: a Ladder in the feet or head cell, refreshed by fixedUpdate.
    [[nodiscard]] bool isOnLadder() const noexcept { return m_onLadder; }

    // Beta EntityLiving ladder rules: descent capped at 0.15 blocks/tick, 0.2 blocks/tick up
    // while pushing against something.
    static constexpr float LADDER_MAX_DESCENT_SPEED = 3.0f;
    static constexpr float LADDER_CLIMB_SPEED = 4.0f;

    // View bobbing & hand swing animation
    [[nodiscard]] float getBobbingPhase() const noexcept { return m_bobPhase; }
    [[nodiscard]] float getBobbingIntensity() const noexcept { return m_bobIntensity; }
    void triggerHandSwing() noexcept { m_swingTimer = 1.0f; }
    [[nodiscard]] float getHandSwingProgress() const noexcept { return 1.0f - m_swingTimer; }

    // Health & Air (Beta 1.7.3). The game tick (game::EntitySimulation) owns them: damage goes
    // through EntitySimulation::damagePlayer only, never by writing Health::current.
    [[nodiscard]] gameplay::Health& health() noexcept { return m_health; }
    [[nodiscard]] const gameplay::Health& health() const noexcept { return m_health; }
    [[nodiscard]] gameplay::Breath& breath() noexcept { return m_breath; }
    [[nodiscard]] const gameplay::Breath& breath() const noexcept { return m_breath; }
    [[nodiscard]] gameplay::Burning& burning() noexcept { return m_burning; }
    [[nodiscard]] const gameplay::Burning& burning() const noexcept { return m_burning; }
    [[nodiscard]] bool isDead() const noexcept { return m_health.isDead(); }

    // Hurt camera (Beta EntityLiving.hurtTime / EntityRenderer.hurtCameraEffect).
    static constexpr int32_t HURT_TIME_TICKS = 10;
    // A full hit landed (EntitySimulation::damagePlayer): restarts the 10-tick camera roll.
    void onHurt() noexcept { m_hurtTime = HURT_TIME_TICKS; }
    // Knockback from a hit with a source (Beta EntityLiving.knockBack): the velocity is
    // halved, `impulse` (blocks/s) added and the upward speed capped at
    // KNOCKBACK_MAX_UPWARD_SPEED. An upward push lifts the player off the ground.
    static constexpr float KNOCKBACK_MAX_UPWARD_SPEED = 8.0f; // 0.4 blocks/tick
    void applyKnockback(const glm::vec3& impulse) noexcept;
    // S6.6 explosion push (Beta Explosion: motion += direction x impact): `delta` (blocks/s)
    // added to the velocity as is; an upward push lifts the player off the ground.
    void addVelocity(const glm::vec3& delta) noexcept;
    // One 20 TPS game tick of the hurt timer.
    void tickHurtTime() noexcept { m_hurtTime = std::max(0, m_hurtTime - 1); }
    [[nodiscard]] int32_t hurtTime() const noexcept { return m_hurtTime; }
    // Camera roll in degrees: -sin(f^4 * pi) * 14 with f = (hurtTime - alpha) / 10, 0 once
    // hurtTime - alpha < 0. `alpha` is the game-tick interpolation factor.
    [[nodiscard]] float hurtCameraRollDeg(float alpha) const noexcept;
    // Game-tick alpha used by the view matrices for the hurt roll (the Application sets it
    // each frame; the movement alpha is a different, 60 Hz clock).
    void setTickAlpha(float alpha) noexcept { m_tickAlpha = std::clamp(alpha, 0.0f, 1.0f); }
    // Fall distance of the landings since the last call (the largest one), then cleared.
    // Physics only records it; the 20 TPS game tick turns it into damage.
    [[nodiscard]] float takePendingFall() noexcept {
        const float fall = m_pendingFall;
        m_pendingFall = 0.0f;
        return fall;
    }
    [[nodiscard]] float pendingFall() const noexcept { return m_pendingFall; }
    [[nodiscard]] float getFallDistance() const noexcept { return m_fallDistance; }
    // Restores a saved fall in progress (level.dat player.fall); NaN and negatives read 0.
    void setFallDistance(float distance) noexcept { m_fallDistance = distance > 0.0f ? distance : 0.0f; }

    [[nodiscard]] int32_t getHealth() const noexcept { return m_health.current; }
    void setHealth(int32_t hp) noexcept { m_health.current = std::clamp(hp, 0, m_health.max); }
    [[nodiscard]] float getAirRatio() const noexcept {
        return static_cast<float>(std::clamp(m_breath.air, 0, gameplay::kMaxAirTicks)) /
               static_cast<float>(gameplay::kMaxAirTicks);
    }

    // Hotbar & Inventory
    [[nodiscard]] int32_t getSelectedSlot() const noexcept { return m_selectedSlot; }
    void setSelectedSlot(int32_t slot) noexcept;
    void scrollSelectedSlot(int32_t delta) noexcept;
    // Survival pick block (Beta): selects a hotbar slot already holding `id`, the selected one
    // first. Returns false (selection unchanged) when no hotbar slot holds it.
    bool selectHotbarSlotHolding(gameplay::ItemId id) noexcept;

    [[nodiscard]] InventorySlot getHotbarSlot(size_t index) const noexcept;
    [[nodiscard]] InventorySlot getInventorySlot(size_t index) const noexcept;
    void setInventorySlot(size_t index, InventorySlot slot) noexcept;

    [[nodiscard]] InventorySlot getHeldItem() const noexcept {
        return m_inventory[static_cast<size_t>(m_selectedSlot)];
    }

    // Replaces the selected hotbar stack (worn or broken tools are written back this way).
    void setHeldItem(InventorySlot slot) noexcept { setInventorySlot(static_cast<size_t>(m_selectedSlot), slot); }

    bool addItem(gameplay::ItemId id, int32_t count = 1) noexcept;
    bool addItem(world::BlockId id, int32_t count = 1) noexcept { return addItem(gameplay::toItemId(id), count); }
    // Merges into matching stacks (same id and wear) up to gameplay::maxStackSize, then fills
    // empty slots. Returns how many items did not fit; those are left to the caller.
    [[nodiscard]] int32_t insertStack(InventorySlot stack) noexcept;
    // Same merge-then-fill rule restricted to the slots [first, last) (Beta
    // Container.mergeItemStack, used by shift-click). With `mergeOnly` the empty slots are
    // left alone. Returns how many items did not fit.
    [[nodiscard]] int32_t insertStackInRange(InventorySlot stack, size_t first, size_t last,
                                             bool mergeOnly = false) noexcept;
    bool consumeHeldItem(int32_t count = 1) noexcept;
    bool consumeItem(gameplay::ItemId id, int32_t count = 1) noexcept;
    bool consumeItem(world::BlockId id, int32_t count = 1) noexcept {
        return consumeItem(gameplay::toItemId(id), count);
    }
    [[nodiscard]] int32_t countItem(gameplay::ItemId id) const noexcept;
    [[nodiscard]] int32_t countItem(world::BlockId id) const noexcept { return countItem(gameplay::toItemId(id)); }

    // S6.7 armor (Beta InventoryPlayer.armorInventory), indexed by gameplay::ArmorSlot
    // (helmet, chestplate, leggings, boots). The setter stores what it is given; the
    // inventory screen only lets the matching piece in.
    static constexpr size_t ARMOR_SLOT_COUNT = 4;
    [[nodiscard]] InventorySlot getArmorSlot(size_t index) const noexcept {
        return index < ARMOR_SLOT_COUNT ? m_armor[index] : InventorySlot{};
    }
    void setArmorSlot(size_t index, InventorySlot slot) noexcept;
    // Beta getTotalArmorValue of the worn pieces (gameplay::totalArmorValue): 0..20.
    [[nodiscard]] int32_t armorValue() const noexcept;
    // Beta EntityPlayer.damageRemainder: the armor-absorbed fraction carried to the next hit
    // (EntitySimulation::damagePlayer); not saved, as in Beta.
    [[nodiscard]] int32_t& armorDamageRemainder() noexcept { return m_armorDamageRemainder; }

private:
    [[nodiscard]] bool collidesWithWorld(const math::AABB& box, const world::World& world) const noexcept;

    glm::vec3 m_position{8.5f, 80.0f, 8.5f};
    glm::vec3 m_prevPosition{8.5f, 80.0f, 8.5f};
    glm::vec3 m_velocity{0.0f};
    float m_yaw{-90.0f};
    float m_pitch{0.0f};

    bool m_onGround{false};
    bool m_inWater{false};         // Water or lava: swimming movement
    bool m_inWaterMaterial{false}; // Water only: splash on entry
    bool m_headUnderWater{false};
    bool m_sneaking{false};
    bool m_flying{false};
    bool m_inLava{false};
    bool m_touchingCactus{false};
    bool m_onLadder{false};

    float m_eyeHeightCurrent{core::EngineConfig::PLAYER_EYE_HEIGHT};
    float m_prevEyeHeightCurrent{core::EngineConfig::PLAYER_EYE_HEIGHT};
    float m_renderAlpha{1.0f};
    float m_bobPhase{0.0f};
    float m_bobIntensity{0.0f};
    float m_swingTimer{0.0f};
    float m_stepAccumulator{0.0f};
    float m_fallDistance{0.0f};

    core::GameMode m_gameMode{core::GameMode::Survival};
    gameplay::Health m_health{gameplay::makeHealth(gameplay::kPlayerMaxHealth)}; // 10 hearts
    gameplay::Breath m_breath{};
    gameplay::Burning m_burning{};
    int32_t m_hurtTime{0};
    float m_tickAlpha{0.0f};
    float m_pendingFall{0.0f};

    int32_t m_selectedSlot{0};
    std::array<InventorySlot, TOTAL_INVENTORY_SIZE> m_inventory{};
    std::array<InventorySlot, ARMOR_SLOT_COUNT> m_armor{};
    int32_t m_armorDamageRemainder{0};
};

} // namespace betacraft::physics
