#include "ecs/Systems.hpp"

#include "world/Block.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/constants.hpp>

namespace betacraft::ecs {

namespace {

// A broken frame timer (NaN/inf) must stall the simulation for one tick rather than poison
// every component it touches: NaN survives std::clamp/std::max and would spread to the
// whole group, and +inf would despawn every item at once.
[[nodiscard]] float sanitizeDt(float dt, float maxDt) noexcept {
    return std::isfinite(dt) ? std::clamp(dt, 0.0f, maxDt) : 0.0f;
}

} // namespace

void MovementPhysicsSystem::integrate(TransformComp& t, VelocityComp& v, float dt,
                                      const MovementParams& params) noexcept {
    t.prevPos = t.pos;
    const float buoyancy = v.inWater ? params.waterGravityFactor : 1.0f;
    v.linear.y -= params.gravity * v.gravityScale * buoyancy * dt;
    v.linear.y = std::max(v.linear.y, -params.terminalVelocity);
    const float horizontalRate = v.horizontalDrag + (v.onGround ? v.groundFriction : 0.0f);
    if (horizontalRate > 0.0f) {
        const float k = std::exp(-horizontalRate * dt);
        v.linear.x *= k;
        v.linear.z *= k;
    }
    if (v.verticalDrag > 0.0f) {
        v.linear.y *= std::exp(-v.verticalDrag * dt);
    }
    t.pos += v.linear * dt;
}

void MovementPhysicsSystem::update(Registry& reg, float dt, const MovementParams& params) {
    dt = sanitizeDt(dt, params.maxDt);
    reg.group<TransformComp, VelocityComp>().each(
        [&](TransformComp& t, VelocityComp& v) { integrate(t, v, dt, params); });
}

void MovementPhysicsSystem::update(Registry& reg, std::span<const EntityId> entities, float dt,
                                   const MovementParams& params) {
    dt = sanitizeDt(dt, params.maxDt);
    for (const EntityId e : entities) {
        TransformComp* t = reg.tryGet<TransformComp>(e);
        VelocityComp* v = reg.tryGet<VelocityComp>(e);
        if (t != nullptr && v != nullptr) {
            integrate(*t, *v, dt, params);
        }
    }
}

bool VoxelCollisionSystem::WorldBoxCollides::operator()(const physics::AABB& box) const {
    // Same cell walk as gridCollides (range-checked casts), refined by the real block shape.
    // The scan reaches one cell below the box: a fence's 1.5-high box rises into the cell
    // above its own. Each solid cell then contributes its collision boxes (two for stairs).
    const physics::AABB scan{box.min - glm::vec3(0.0f, 1.0f, 0.0f), box.max};
    return gridCollides(scan, [&](int32_t x, int32_t y, int32_t z) {
        const world::BlockId id = world->getBlock(x, y, z);
        if (!world::BlockRegistry::isSolid(id)) {
            return false;
        }
        const uint8_t meta = world::shapeDependsOnMetadata(world::BlockRegistry::get(id).shape)
            ? world->getMetadata(x, y, z)
            : uint8_t{0};
        return physics::AABB::forEachBlockBox(x, y, z, id, meta,
                                              [&](const physics::AABB& b) { return box.intersects(b); });
    });
}

void VoxelCollisionSystem::update(Registry& reg, const world::World& world) {
    update(reg, WorldBoxCollides{&world});
}

void VoxelCollisionSystem::update(Registry& reg, std::span<const EntityId> entities, const world::World& world) {
    WorldBoxCollides collides{&world};
    for (const EntityId e : entities) {
        TransformComp* t = reg.tryGet<TransformComp>(e);
        VelocityComp* v = reg.tryGet<VelocityComp>(e);
        const AABBColliderComp* c = reg.tryGet<AABBColliderComp>(e);
        if (t != nullptr && v != nullptr && c != nullptr) {
            resolveBody(*t, *v, *c, collides);
        }
    }
}

bool HealthSystem::applyDamage(Registry& reg, EntityId e, int amount, const glm::vec3& knockback) {
    HealthComp* h = reg.tryGet<HealthComp>(e);
    if (h == nullptr || amount <= 0 || h->isDead()) {
        return false;
    }
    // Saturate to int16 so the "stronger than lastDamage" bar and the subtraction stay exact.
    const int hit = std::min(amount, static_cast<int>(std::numeric_limits<int16_t>::max()));
    const bool inWindow = h->hurtInvulnerabilityTimer > 0.0f;
    if (inWindow && hit <= h->lastDamage) {
        return false;
    }
    const int dealt = inWindow ? hit - h->lastDamage : hit;
    h->current = static_cast<int16_t>(std::max(0, static_cast<int>(h->current) - dealt));
    h->lastDamage = static_cast<int16_t>(hit);
    if (inWindow) {
        return true; // overriding hit: no new window, no knockback
    }
    h->hurtInvulnerabilityTimer = HURT_INVULNERABILITY_SECONDS;
    VelocityComp* v = reg.tryGet<VelocityComp>(e);
    if (v != nullptr && knockback != glm::vec3(0.0f)) {
        v->linear = v->linear * 0.5f + knockback;
        v->linear.y = std::min(v->linear.y, KNOCKBACK_MAX_UPWARD_SPEED);
        v->onGround = v->onGround && knockback.y <= 0.0f;
    }
    return true;
}

bool HealthSystem::heal(Registry& reg, EntityId e, int amount) {
    HealthComp* h = reg.tryGet<HealthComp>(e);
    if (h == nullptr || amount <= 0 || h->isDead()) {
        return false;
    }
    // Clamp the amount to the headroom first: current + amount overflows int for huge amounts.
    const int headroom = std::max(0, static_cast<int>(h->max) - static_cast<int>(h->current));
    h->current = static_cast<int16_t>(static_cast<int>(h->current) + std::min(amount, headroom));
    return true;
}

std::vector<EntityId> HealthSystem::update(Registry& reg, float dt) {
    dt = sanitizeDt(dt, std::numeric_limits<float>::max());
    std::vector<EntityId> dead;
    reg.view<HealthComp>().each([&](EntityId e, HealthComp& h) {
        h.hurtInvulnerabilityTimer = std::max(0.0f, h.hurtInvulnerabilityTimer - dt);
        if (h.isDead()) {
            dead.push_back(e);
        }
    });
    return dead;
}

namespace {

[[nodiscard]] uint32_t nextRandom(uint32_t& state) noexcept {
    uint32_t x = state;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    state = x;
    return x;
}

// Uniform in [0, 1) from the top 24 bits (exactly representable as float).
[[nodiscard]] float nextUnit(uint32_t& state) noexcept {
    return static_cast<float>(nextRandom(state) >> 8U) * (1.0f / 16777216.0f);
}

void ensureSeeded(MobAIComp& ai, EntityId e) noexcept {
    if (ai.rngState == 0U) {
        // Murmur3 finaliser: neighbouring handles give unrelated streams. Never 0 (xorshift's
        // fixed point) thanks to the final OR.
        uint32_t h = e ^ 0x9E3779B9U;
        h ^= h >> 16U;
        h *= 0x85EBCA6BU;
        h ^= h >> 13U;
        h *= 0xC2B2AE35U;
        h ^= h >> 16U;
        ai.rngState = h | 1U;
    }
}

void stopHorizontal(VelocityComp& v) noexcept {
    v.linear.x = 0.0f;
    v.linear.z = 0.0f;
}

// Walks along the horizontal direction `to` (need not be normalised, must be non-zero).
void steer(TransformComp& t, VelocityComp& v, glm::vec2 to, float speed) noexcept {
    const glm::vec2 dir = glm::normalize(to);
    v.linear.x = dir.x * speed;
    v.linear.z = dir.y * speed;
    t.yaw = glm::degrees(std::atan2(dir.y, dir.x)); // same convention as Player::getLookDirection
}

void enterIdle(MobAIComp& ai, VelocityComp& v, const MobAIParams& p) noexcept {
    ai.state = MobAIState::Idle;
    ai.target = NULL_ENTITY;
    ai.stateTimer = p.idleSecondsMin + (p.idleSecondsMax - p.idleSecondsMin) * nextUnit(ai.rngState);
    stopHorizontal(v);
}

void enterWander(MobAIComp& ai, const TransformComp& t, const MobAIParams& p) noexcept {
    // sqrt keeps goals uniform over the disc's area; the inner bound guarantees the goal is not
    // already "reached" on the next tick.
    const float inner = std::min(2.0f * p.arrivalRadius, p.wanderRadius);
    const float radius = inner + (p.wanderRadius - inner) * std::sqrt(nextUnit(ai.rngState));
    const float angle = nextUnit(ai.rngState) * glm::two_pi<float>();
    ai.state = MobAIState::Wander;
    ai.goal = t.pos + glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
    ai.stateTimer = p.wanderTimeoutSeconds;
}

[[nodiscard]] glm::vec2 horizontal(const glm::vec3& v) noexcept {
    return {v.x, v.z};
}

// Beta EntityLiving.onLivingUpdate: a walking mob stopped by a wall jumps.
void jumpIfBlocked(VelocityComp& v) noexcept {
    if (v.collidedHorizontally && v.onGround) {
        v.linear.y = MobAISystem::JUMP_SPEED;
        v.onGround = false;
    }
}

// S6.3: a mob that climbsWalls (spider) goes up a wall it walks into, on the ground or not
// (Beta EntitySpider.isOnLadder = isCollidedHorizontally, ladder climbing at 0.2 b/tick);
// any other mob jumps.
void climbOrJump(const Registry& reg, EntityId e, VelocityComp& v) noexcept {
    const MobKindComp* kind = reg.tryGet<MobKindComp>(e);
    if (kind != nullptr && gameplay::mobDef(kind->kind).climbsWalls && v.collidedHorizontally) {
        v.linear.y = std::max(v.linear.y, MobAISystem::CLIMB_SPEED);
        v.onGround = false;
        return;
    }
    jumpIfBlocked(v);
}

// S8.3: steers along the mob's A* waypoints. False when no waypoint is left (the caller
// then steers straight).
bool followPath(const Registry& reg, EntityId e, MobPathComp& path, TransformComp& t, VelocityComp& v,
                float speed) noexcept {
    const glm::ivec3 fp = ai::navFootprint(PathPlanningSystem::agentOf(reg, e).extents);
    const glm::vec2 offset(static_cast<float>(fp.x) * 0.5f, static_cast<float>(fp.z) * 0.5f);
    while (path.index < path.count) {
        const glm::ivec3& wp = path.waypoints[path.index];
        const glm::vec2 centre = glm::vec2(static_cast<float>(wp.x), static_cast<float>(wp.z)) + offset;
        const glm::vec2 to = centre - horizontal(t.pos);
        if (glm::length(to) > MobAISystem::WAYPOINT_REACHED) {
            steer(t, v, to, speed);
            // S4.11 jump, ahead of the wall contact when the next cell is a step up.
            if (wp.y > static_cast<int32_t>(std::floor(t.pos.y + 1e-3f)) && v.onGround) {
                v.linear.y = MobAISystem::JUMP_SPEED;
                v.onGround = false;
            }
            return true;
        }
        ++path.index;
    }
    return false;
}

// Whether a mob whose waypoints ran out stands next to the cell its path was planned for, so
// the last stretch can be walked straight. Short of it the path was partial (goal out of
// reach, behind lava or a closed door): walking straight on would undo what A* avoided.
[[nodiscard]] bool besideGoal(const Registry& reg, EntityId e, const MobPathComp& path,
                              const TransformComp& t) noexcept {
    const glm::ivec3 fp = ai::navFootprint(PathPlanningSystem::agentOf(reg, e).extents);
    const glm::ivec3 d = PathPlanningSystem::cornerCell(t.pos, fp) - path.goalCell;
    return std::abs(d.x) <= 1 && std::abs(d.z) <= 1 && std::abs(d.y) <= 1;
}

// Straight-line steering towards `to`, or the path when the mob has one. A mob with a path
// component whose waypoints are used up only walks straight beside its goal; elsewhere it
// waits for PathPlanningSystem's next plan.
void walkTowards(Registry& reg, EntityId e, TransformComp& t, VelocityComp& v, glm::vec2 to, float speed) noexcept {
    MobPathComp* path = reg.tryGet<MobPathComp>(e);
    if (path == nullptr) {
        steer(t, v, to, speed);
    } else if (!followPath(reg, e, *path, t, v, speed)) {
        if (!besideGoal(reg, e, *path, t)) {
            stopHorizontal(v);
            return;
        }
        steer(t, v, to, speed);
    }
    climbOrJump(reg, e, v);
}

// Feet cell snapped onto something standable: down up to 3 cells (a jumping target), or one
// up (feet inside a slab or farmland cell).
[[nodiscard]] glm::ivec3 standableNear(const ai::INavVoxelQuery& q, const glm::ivec3& cell, const ai::NavAgent& agent) {
    for (int32_t dy = 0; dy >= -3; --dy) {
        if (ai::isStandable(q, cell + glm::ivec3(0, dy, 0), agent)) {
            return cell + glm::ivec3(0, dy, 0);
        }
    }
    if (ai::isStandable(q, cell + glm::ivec3(0, 1, 0), agent)) {
        return cell + glm::ivec3(0, 1, 0);
    }
    return cell;
}

} // namespace

void MobAISystem::update(Registry& reg, float dt, const MobAIParams& params) {
    dt = sanitizeDt(dt, std::numeric_limits<float>::max());
    reg.view<MobAIComp, TransformComp, VelocityComp>().each(
        [&](EntityId e, MobAIComp& ai, TransformComp& t, VelocityComp& v) {
            ensureSeeded(ai, e);
            ai.stateTimer -= dt;

            // A leaping mob (spider) keeps its impulse until it lands.
            if (ai.leaping) {
                if (!v.onGround) {
                    return;
                }
                ai.leaping = false;
            }

            // A mob knocked back by a hit (hurt window open, airborne) has no air control:
            // steering now would cancel the impulse before it lands.
            if (!v.onGround) {
                const HealthComp* h = reg.tryGet<HealthComp>(e);
                if (h != nullptr && h->hurtInvulnerabilityTimer > 0.0f) {
                    return;
                }
            }

            if (ai.state == MobAIState::Chase || ai.state == MobAIState::Flee) {
                // valid() first: a recycled slot must not be mistaken for the old target.
                const TransformComp* target =
                    reg.valid(ai.target) ? reg.tryGet<TransformComp>(ai.target) : nullptr;
                if (target == nullptr) {
                    enterIdle(ai, v, params);
                    return;
                }
                const glm::vec3 to = target->pos - t.pos;
                if (ai.state == MobAIState::Chase) {
                    if (glm::length(to) > params.loseTargetRange) {
                        enterIdle(ai, v, params);
                    } else if (glm::length(horizontal(to)) <= params.arrivalRadius) {
                        stopHorizontal(v);
                    } else {
                        walkTowards(reg, e, t, v, horizontal(to), ai.moveSpeed);
                    }
                    return;
                }
                if (ai.stateTimer <= 0.0f) {
                    enterIdle(ai, v, params);
                    return;
                }
                glm::vec2 away = -horizontal(to);
                if (glm::dot(away, away) < 1e-8f) {
                    // Threat exactly on top of us: bolt in a random direction.
                    const float angle = nextUnit(ai.rngState) * glm::two_pi<float>();
                    away = glm::vec2(std::cos(angle), std::sin(angle));
                }
                steer(t, v, away, ai.moveSpeed);
                return;
            }

            if (ai.state == MobAIState::Wander) {
                const glm::vec2 to = horizontal(ai.goal - t.pos);
                if (glm::length(to) <= params.arrivalRadius || ai.stateTimer <= 0.0f) {
                    enterIdle(ai, v, params);
                } else {
                    walkTowards(reg, e, t, v, to, ai.moveSpeed);
                }
                return;
            }

            stopHorizontal(v); // Idle
            if (ai.stateTimer <= 0.0f) {
                enterWander(ai, t, params);
            }
        });
}

// --- S8.3 PathPlanningSystem ---------------------------------------------------------------

ai::NavAgent PathPlanningSystem::agentOf(const Registry& reg, EntityId e) noexcept {
    if (const MobKindComp* kind = reg.tryGet<MobKindComp>(e)) {
        const gameplay::MobDef& def = gameplay::mobDef(kind->kind);
        return ai::NavAgent{def.navExtents, def.canOpenDoors, def.climbsWalls};
    }
    return ai::NavAgent{};
}

glm::ivec3 PathPlanningSystem::cornerCell(const glm::vec3& feet, const glm::ivec3& footprint) noexcept {
    // The footprint is centred on the body: corner = round(centre - size / 2).
    const auto axis = [](float c, int32_t size) {
        return VoxelCollisionSystem::toCell(c - static_cast<float>(size) * 0.5f + 0.5f);
    };
    return {axis(feet.x, footprint.x), VoxelCollisionSystem::toCell(feet.y + 1e-3f), axis(feet.z, footprint.z)};
}

bool PathPlanningSystem::hasSearch(EntityId e) const noexcept {
    return std::any_of(m_searches.begin(), m_searches.end(), [e](const Active& a) { return a.entity == e; });
}

int32_t PathPlanningSystem::searchProgress(EntityId e) const noexcept {
    for (const Active& a : m_searches) {
        if (a.entity == e) {
            return a.search.expandedNodes();
        }
    }
    return -1;
}

void PathPlanningSystem::clear() noexcept {
    m_searches.clear();
    m_cursor = 0;
}

void PathPlanningSystem::update(Registry& reg, const ai::INavVoxelQuery& query, PathBudget& budget) {
    budget.spent = 0;
    const auto planning = [&reg](EntityId e) {
        if (!reg.valid(e) || !reg.has<MobPathComp>(e)) {
            return false;
        }
        const MobAIComp* ai = reg.tryGet<MobAIComp>(e);
        return ai != nullptr && (ai->state == MobAIState::Chase || ai->state == MobAIState::Wander);
    };
    // 1. Searches whose mob is gone or does something else.
    const auto keep = [&](const Active& a) { return planning(a.entity); };
    eraseKeepingCursor([&](const Active& a) { return !keep(a); });

    // 2. Who needs a (new) path.
    reg.view<MobAIComp, MobPathComp, TransformComp>().each(
        [&](EntityId e, const MobAIComp& ai, MobPathComp& path, const TransformComp& t) {
            if (path.repathCooldown > 0) {
                --path.repathCooldown;
            }
            if (ai.state != MobAIState::Chase && ai.state != MobAIState::Wander) {
                path.count = 0;
                path.index = 0;
                path.stuckTicks = 0;
                return;
            }
            const bool following = path.index < path.count;
            const glm::vec3 moved = t.pos - t.prevPos; // climbing a wall is not being stuck
            if (following && glm::dot(moved, moved) < STUCK_EPSILON * STUCK_EPSILON) {
                path.stuckTicks = static_cast<uint8_t>(std::min<int32_t>(path.stuckTicks + 1, 255));
            } else {
                path.stuckTicks = 0;
            }
            glm::vec3 goal = ai.goal;
            if (ai.state == MobAIState::Chase) {
                const TransformComp* target = reg.valid(ai.target) ? reg.tryGet<TransformComp>(ai.target) : nullptr;
                if (target == nullptr) {
                    return; // MobAISystem drops the chase
                }
                goal = target->pos;
            }
            if (path.repathCooldown > 0 || hasSearch(e)) {
                return;
            }
            const ai::NavAgent agent = agentOf(reg, e);
            const glm::ivec3 fp = ai::navFootprint(agent.extents);
            const glm::ivec3 here = cornerCell(t.pos, fp);
            const glm::ivec3 goalCell = standableNear(query, cornerCell(goal, fp), agent);
            const glm::vec3 goalShift = glm::vec3(goalCell - path.goalCell);
            const bool goalMoved = glm::dot(goalShift, goalShift) > REPATH_DISTANCE * REPATH_DISTANCE;
            const bool exhausted = !following && here != path.goalCell;
            if (!(path.count == 0 || goalMoved || exhausted || path.stuckTicks > STUCK_TICKS)) {
                return;
            }
            path.goalCell = goalCell;
            path.repathCooldown = REPATH_COOLDOWN_TICKS;
            path.stuckTicks = 0;
            m_searches.push_back(Active{e, ai::PathSearch(standableNear(query, here, agent), goalCell, agent)});
        });

    // 3. Round-robin over the searches, from where the previous tick stopped.
    const size_t n = m_searches.size();
    size_t next = n == 0 ? 0 : m_cursor % n;
    const size_t first = next;
    for (size_t k = 0; k < n; ++k) {
        const int32_t left = budget.nodesPerTick - budget.spent;
        if (left <= 0) {
            break;
        }
        Active& a = m_searches[(first + k) % n];
        const int32_t before = a.search.expandedNodes();
        (void)a.search.step(query, std::min(left, budget.nodesPerSearch));
        budget.spent += a.search.expandedNodes() - before;
        next = (first + k + 1) % n;
    }
    m_cursor = next;

    // 4. Finished searches hand their path to the mob.
    for (Active& a : m_searches) {
        if (!a.search.done()) {
            continue;
        }
        MobPathComp& path = reg.get<MobPathComp>(a.entity);
        const std::span<const glm::ivec3> cells = a.search.path();
        const size_t count = cells.empty() ? 0 : std::min(cells.size() - 1, MobPathComp::CAPACITY);
        for (size_t i = 0; i < count; ++i) {
            path.waypoints[i] = cells[i + 1];
        }
        path.count = static_cast<uint8_t>(count);
        path.index = 0;
        path.stuckTicks = 0;
    }
    eraseKeepingCursor([](const Active& a) { return a.search.done(); });
}

size_t ItemDropSystem::update(Registry& reg, float dt) {
    dt = sanitizeDt(dt, std::numeric_limits<float>::max());
    size_t despawned = 0;
    // Destroying the visited entity is supported by View::each (snapshot iteration); `item` is
    // not touched after the destroy, which invalidates it.
    reg.view<ItemDropComp>().each([&](EntityId e, ItemDropComp& item) {
        item.ageSeconds += dt;
        item.pickupDelaySeconds = std::max(0.0f, item.pickupDelaySeconds - dt);
        if (item.ageSeconds >= DESPAWN_SECONDS) {
            (void)reg.destroy(e);
            ++despawned;
        }
    });
    return despawned;
}

void EntityRenderInterpolationSystem::collect(Registry& reg, float alpha, std::vector<RenderInstance>& out) {
    // Non-finite alpha falls back to the latest simulated state, never to NaN positions.
    alpha = std::isfinite(alpha) ? std::clamp(alpha, 0.0f, 1.0f) : 1.0f;
    out.clear();
    out.reserve(reg.count<TransformComp>());
    reg.view<TransformComp>().each([&](EntityId e, TransformComp& t) {
        out.push_back(RenderInstance{e, glm::mix(t.prevPos, t.pos, alpha), t.yaw, t.pitch});
    });
}

void PlayerControlSystem::update(Registry& reg, float dt) {
    dt = sanitizeDt(dt, std::numeric_limits<float>::max());
    reg.view<PlayerInputComp, TransformComp, VelocityComp>().each(
        [dt](PlayerInputComp& in, TransformComp& t, VelocityComp& v) {
            const physics::PlayerInput& input = in.input;
            const float yawRad = glm::radians(t.yaw);
            const glm::vec3 forward(std::cos(yawRad), 0.0f, std::sin(yawRad));
            const glm::vec3 right(-forward.z, 0.0f, forward.x);

            glm::vec3 wish(0.0f);
            if (input.forward)  { wish += forward; }
            if (input.backward) { wish -= forward; }
            if (input.right)    { wish += right; }
            if (input.left)     { wish -= right; }
            if (glm::dot(wish, wish) > 1e-8f) {
                wish = glm::normalize(wish);
            }

            // Same priority as Player::fixedUpdate: no sneaking in water, sprint needs forward.
            const bool sneaking = input.sneak && !v.inWater;
            float speed = core::EngineConfig::WALK_SPEED;
            if (sneaking) {
                speed = core::EngineConfig::SNEAK_SPEED;
            } else if (input.sprint && input.forward) {
                speed = core::EngineConfig::SPRINT_SPEED;
            }
            v.edgeGuard = sneaking;

            const float accel = v.onGround ? GROUND_ACCELERATION : AIR_ACCELERATION;
            const float blend = std::min(1.0f, dt * accel);
            v.linear.x = glm::mix(v.linear.x, wish.x * speed, blend);
            v.linear.z = glm::mix(v.linear.z, wish.z * speed, blend);

            if (input.jump && v.onGround) {
                v.linear.y = core::EngineConfig::JUMP_VELOCITY;
                v.onGround = false;
            }
        });
}

EntityId EntityFactory::spawnPlayer(Registry& reg, const glm::vec3& feetPos) {
    const EntityId e = reg.create();
    if (e == NULL_ENTITY) {
        return NULL_ENTITY;
    }
    reg.emplace<TransformComp>(e, TransformComp{feetPos, feetPos, 0.0f, 0.0f});
    reg.emplace<VelocityComp>(e);
    reg.emplace<AABBColliderComp>(e);
    reg.emplace<HealthComp>(e);
    reg.emplace<PlayerInputComp>(e);
    reg.emplace<PlayerTagComp>(e);
    return e;
}

EntityId EntityFactory::spawnItemDrop(Registry& reg, const glm::vec3& pos, gameplay::ItemId id, int32_t count,
                                      const glm::vec3& initialVelocity, int32_t damage) {
    const EntityId e = reg.create();
    if (e == NULL_ENTITY) {
        return NULL_ENTITY;
    }
    reg.emplace<TransformComp>(e, TransformComp{pos, pos, 0.0f, 0.0f});
    VelocityComp v{};
    v.linear = initialVelocity;
    v.gravityScale = ITEM_GRAVITY_SCALE;
    v.horizontalDrag = ITEM_AIR_DRAG;
    v.verticalDrag = ITEM_AIR_DRAG;
    v.groundFriction = ITEM_GROUND_FRICTION;
    reg.emplace<VelocityComp>(e, v);
    reg.emplace<AABBColliderComp>(e, AABBColliderComp{ITEM_SIZE, ITEM_SIZE});
    ItemDropComp item{};
    item.id = id;
    item.count = count;
    item.damage = damage;
    reg.emplace<ItemDropComp>(e, item);
    return e;
}

EntityId EntityFactory::spawnMob(Registry& reg, const glm::vec3& feetPos, int16_t maxHealth,
                                 float width, float height) {
    const EntityId e = reg.create();
    if (e == NULL_ENTITY) {
        return NULL_ENTITY;
    }
    reg.emplace<TransformComp>(e, TransformComp{feetPos, feetPos, 0.0f, 0.0f});
    reg.emplace<VelocityComp>(e);
    reg.emplace<AABBColliderComp>(e, AABBColliderComp{width, height});
    reg.emplace<HealthComp>(e, HealthComp{maxHealth, maxHealth, 0.0f});
    reg.emplace<MobAIComp>(e);
    return e;
}

EntityId EntityFactory::spawnMob(Registry& reg, const glm::vec3& feetPos, gameplay::MobKind kind) {
    const gameplay::MobDef& def = gameplay::mobDef(kind);
    const EntityId e = spawnMob(reg, feetPos, def.maxHealth, def.width, def.height);
    if (e == NULL_ENTITY) {
        return NULL_ENTITY;
    }
    reg.get<MobAIComp>(e).moveSpeed = def.moveSpeed;
    reg.emplace<MobKindComp>(e, MobKindComp{def.kind, 0});
    if (def.category == gameplay::MobCategory::Hostile) {
        reg.emplace<MobAttackComp>(e);
    }
    if (def.explode.enabled()) {
        reg.emplace<FuseComp>(e);
    }
    return e;
}

EntityId EntityFactory::spawnPlayerProxy(Registry& reg, const glm::vec3& feetPos) {
    const EntityId e = reg.create();
    if (e == NULL_ENTITY) {
        return NULL_ENTITY;
    }
    reg.emplace<TransformComp>(e, TransformComp{feetPos, feetPos, 0.0f, 0.0f});
    reg.emplace<AABBColliderComp>(e);
    reg.emplace<PlayerTagComp>(e);
    return e;
}

} // namespace betacraft::ecs
