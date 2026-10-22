#include "game/MobBehaviour.hpp"

#include "ecs/Components.hpp"
#include "ecs/Systems.hpp"
#include "game/Projectiles.hpp"
#include "gameplay/MobSpawnRules.hpp"
#include "physics/Raycaster.hpp"
#include "voxel/Block.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::game {

namespace {

[[nodiscard]] core::BlockPos cellOf(const glm::vec3& p) noexcept {
    return {static_cast<int32_t>(std::floor(p.x)), static_cast<int32_t>(std::floor(p.y)),
            static_cast<int32_t>(std::floor(p.z))};
}

[[nodiscard]] bool waterAt(const world::World& world, const glm::vec3& p) noexcept {
    const core::BlockPos c = cellOf(p);
    return voxel::isWaterBlock(world.getBlock(c.x, c.y, c.z));
}

[[nodiscard]] bool isAlive(const ecs::Registry& reg, ecs::EntityId e) noexcept {
    const ecs::HealthComp* h = reg.tryGet<ecs::HealthComp>(e);
    return h != nullptr && !h->isDead() && reg.has<ecs::MobAIComp>(e);
}

[[nodiscard]] float mobHeight(const ecs::Registry& reg, ecs::EntityId e, gameplay::MobKind kind) noexcept {
    const ecs::AABBColliderComp* c = reg.tryGet<ecs::AABBColliderComp>(e);
    return c != nullptr ? c->height : gameplay::mobDef(kind).height;
}

// True when no block stops the ray from `from` to `to` (Beta World.rayTraceBlocks).
[[nodiscard]] bool lineOfSight(const world::World& world, const glm::vec3& from, const glm::vec3& to) {
    const glm::vec3 d = to - from;
    const float dist = glm::length(d);
    if (dist <= 1e-4f) {
        return true;
    }
    const physics::RaycastHit hit = physics::Raycaster::cast(world, from, d / dist, dist);
    return !(hit.hit && hit.distance < dist);
}

} // namespace

void acquireTargets(ecs::Registry& reg, const world::World& world, ecs::EntityId proxy, core::GameMode mode,
                    uint64_t tick, bool playerDead, float sunBrightness) {
    const ecs::TransformComp* player = reg.valid(proxy) ? reg.tryGet<ecs::TransformComp>(proxy) : nullptr;
    const bool ignorePlayer = mode == core::GameMode::Creative || playerDead || player == nullptr;
    const glm::vec3 playerFeet = player != nullptr ? player->pos : glm::vec3(0.0f);
    const glm::vec3 playerEye = playerFeet + glm::vec3(0.0f, core::EngineConfig::PLAYER_EYE_HEIGHT, 0.0f);

    reg.view<ecs::MobKindComp, ecs::MobAIComp, ecs::TransformComp>().each(
        [&](ecs::EntityId e, const ecs::MobKindComp& mob, ecs::MobAIComp& ai, const ecs::TransformComp& t) {
            const gameplay::MobDef& def = gameplay::mobDef(mob.kind);
            if (def.category != gameplay::MobCategory::Hostile) {
                return;
            }
            if (ignorePlayer) {
                if (ai.state == ecs::MobAIState::Chase) {
                    ai.target = ecs::NULL_ENTITY; // MobAISystem idles on a stale target
                }
                return;
            }
            if (ai.state != ecs::MobAIState::Idle && ai.state != ecs::MobAIState::Wander) {
                return;
            }
            if (def.neutralInDaylight && sunBrightness > kSpiderNeutralBrightness) {
                return; // Beta EntitySpider: no hunting in daylight
            }
            if ((tick + ecs::entityIndex(e)) % kTargetSearchIntervalTicks != 0) {
                return;
            }
            if (glm::length(playerFeet - t.pos) >= kTargetSearchRange) {
                return;
            }
            const glm::vec3 eye = t.pos + glm::vec3(0.0f, mobEyeHeight(mobHeight(reg, e, mob.kind)), 0.0f);
            if (!lineOfSight(world, eye, playerEye)) {
                return; // a block in the way
            }
            ai.state = ecs::MobAIState::Chase;
            ai.target = proxy;
        });
}

namespace {

std::vector<MobMeleeHit> meleeContactImpl(ecs::Registry& reg, const world::World* world, ecs::EntityId proxy) {
    std::vector<MobMeleeHit> hits;
    const ecs::TransformComp* player = reg.valid(proxy) ? reg.tryGet<ecs::TransformComp>(proxy) : nullptr;
    const ecs::AABBColliderComp* playerBox = reg.valid(proxy) ? reg.tryGet<ecs::AABBColliderComp>(proxy) : nullptr;
    reg.view<ecs::MobAttackComp, ecs::MobKindComp, ecs::TransformComp>().each(
        [&](ecs::EntityId e, ecs::MobAttackComp& attack, const ecs::MobKindComp& mob, const ecs::TransformComp& t) {
            if (gameplay::mobDef(mob.kind).attackDamage <= 0) {
                return; // No melee attack; a skeleton's cooldown belongs to its bow
            }
            attack.cooldownTicks = std::max(0, attack.cooldownTicks - 1);
            if (attack.cooldownTicks > 0 || player == nullptr || playerBox == nullptr || !isAlive(reg, e)) {
                return;
            }
            const ecs::MobAIComp& ai = reg.get<ecs::MobAIComp>(e);
            if (ai.state != ecs::MobAIState::Chase || ai.target != proxy) {
                return;
            }
            const glm::vec2 d(player->pos.x - t.pos.x, player->pos.z - t.pos.z);
            if (glm::length(d) >= kMobMeleeReach) {
                return;
            }
            const float height = mobHeight(reg, e, mob.kind);
            const math::AABB target = playerBox->boundsAt(player->pos);
            if (!(target.max.y > t.pos.y && target.min.y < t.pos.y + height)) {
                return;
            }
            if (world != nullptr &&
                !lineOfSight(*world, t.pos + glm::vec3(0.0f, mobEyeHeight(height), 0.0f),
                             player->pos + glm::vec3(0.0f, core::EngineConfig::PLAYER_EYE_HEIGHT, 0.0f))) {
                return; // a wall in between: the cooldown stays ready
            }
            attack.cooldownTicks = ecs::MobAttackComp::COOLDOWN_TICKS;
            hits.push_back({e, gameplay::mobDef(mob.kind).attackDamage, t.pos});
        });
    return hits;
}

} // namespace

std::vector<MobMeleeHit> meleeContact(ecs::Registry& reg, ecs::EntityId proxy) {
    return meleeContactImpl(reg, nullptr, proxy);
}

std::vector<MobMeleeHit> meleeContact(ecs::Registry& reg, const world::World& world, ecs::EntityId proxy) {
    return meleeContactImpl(reg, &world, proxy);
}

namespace {

void sunBurnImpl(ecs::Registry& reg, const world::World& world, float timeOfDay, gameplay::SplitMix64& rng,
                 const storage::WeatherState* weather) {
    const float brightness = gameplay::sunBrightness(timeOfDay);
    if (!(brightness > 0.5f)) {
        return;
    }
    std::vector<ecs::EntityId> ignite;
    reg.view<ecs::MobKindComp, ecs::TransformComp>().each(
        [&](ecs::EntityId e, const ecs::MobKindComp& mob, const ecs::TransformComp& t) {
            if (!gameplay::mobDef(mob.kind).burnsInDaylight || !isAlive(reg, e)) {
                return;
            }
            const glm::vec3 eye = t.pos + glm::vec3(0.0f, mobEyeHeight(mobHeight(reg, e, mob.kind)), 0.0f);
            const core::BlockPos c = cellOf(eye);
            if (world.getSkyLight(c.x, c.y, c.z) != 15 || waterAt(world, eye)) {
                return;
            }
            if (weather != nullptr &&
                (world::isRainingAt(world, *weather, t.pos) || world::isRainingAt(world, *weather, eye))) {
                return; // Wet: Beta's fire goes out in the rain before it can do anything
            }
            if (rng.nextFloat() * 30.0f < (brightness - 0.4f) * 2.0f) {
                ignite.push_back(e);
            }
        });
    for (const ecs::EntityId e : ignite) {
        if (ecs::BurningComp* burning = reg.tryGet<ecs::BurningComp>(e); burning != nullptr && burning->ticks > 0) {
            // Topped up in whole damage periods, so the 1-per-20-ticks rhythm is kept.
            burning->ticks += ((kSunBurnTicks - burning->ticks) / kBurnDamageIntervalTicks) * kBurnDamageIntervalTicks;
        } else {
            (void)reg.emplaceOrReplace<ecs::BurningComp>(e, ecs::BurningComp{kSunBurnTicks});
        }
    }
}

std::vector<ecs::EntityId> tickBurningImpl(ecs::Registry& reg, const world::World& world,
                                           const storage::WeatherState* weather) {
    std::vector<ecs::EntityId> hurt;
    std::vector<ecs::EntityId> putOut;
    std::vector<ecs::EntityId> damaged;
    reg.view<ecs::BurningComp, ecs::TransformComp>().each(
        [&](ecs::EntityId e, ecs::BurningComp& burning, const ecs::TransformComp& t) {
            const ecs::AABBColliderComp* c = reg.tryGet<ecs::AABBColliderComp>(e);
            const float height = c != nullptr ? c->height : 1.0f;
            const glm::vec3 eye = t.pos + glm::vec3(0.0f, mobEyeHeight(height), 0.0f);
            if (burning.ticks <= 0 || waterAt(world, t.pos) || waterAt(world, eye) ||
                (weather != nullptr &&
                 (world::isRainingAt(world, *weather, t.pos) || world::isRainingAt(world, *weather, eye)))) {
                putOut.push_back(e);
                return;
            }
            if (burning.ticks % kBurnDamageIntervalTicks == 0) {
                damaged.push_back(e);
            }
            if (--burning.ticks <= 0) {
                putOut.push_back(e);
            }
        });
    // Damage after the walk: applyDamage only touches HealthComp / VelocityComp, but the
    // removals below change the BurningComp pool.
    for (const ecs::EntityId e : damaged) {
        if (ecs::HealthSystem::applyDamage(reg, e, 1)) {
            hurt.push_back(e);
        }
    }
    for (const ecs::EntityId e : putOut) {
        (void)reg.remove<ecs::BurningComp>(e);
    }
    return hurt;
}

} // namespace

void sunBurn(ecs::Registry& reg, const world::World& world, float timeOfDay, gameplay::SplitMix64& rng) {
    sunBurnImpl(reg, world, timeOfDay, rng, nullptr);
}

void sunBurn(ecs::Registry& reg, const world::World& world, float timeOfDay, gameplay::SplitMix64& rng,
             const storage::WeatherState& weather) {
    sunBurnImpl(reg, world, timeOfDay, rng, &weather);
}

std::vector<ecs::EntityId> tickBurning(ecs::Registry& reg, const world::World& world) {
    return tickBurningImpl(reg, world, nullptr);
}

std::vector<ecs::EntityId> tickBurning(ecs::Registry& reg, const world::World& world,
                                       const storage::WeatherState& weather) {
    return tickBurningImpl(reg, world, &weather);
}

bool isBurning(const ecs::Registry& reg, ecs::EntityId e) {
    const ecs::BurningComp* burning = reg.tryGet<ecs::BurningComp>(e);
    return burning != nullptr && burning->ticks > 0;
}

namespace {

[[nodiscard]] glm::vec3 proxyEye(const ecs::TransformComp& player) noexcept {
    return player.pos + glm::vec3(0.0f, core::EngineConfig::PLAYER_EYE_HEIGHT, 0.0f);
}

// Stops the mob where it stands, facing `to` (horizontal part).
void holdAndFace(ecs::TransformComp& t, ecs::VelocityComp& v, const glm::vec3& to) noexcept {
    v.linear.x = 0.0f;
    v.linear.z = 0.0f;
    if (to.x * to.x + to.z * to.z > 1e-8f) {
        t.yaw = glm::degrees(std::atan2(to.z, to.x));
    }
}

[[nodiscard]] const ecs::TransformComp* proxyTransform(const ecs::Registry& reg, ecs::EntityId proxy) noexcept {
    return reg.valid(proxy) ? reg.tryGet<ecs::TransformComp>(proxy) : nullptr;
}

[[nodiscard]] bool chasing(const ecs::MobAIComp& ai, ecs::EntityId proxy) noexcept {
    return ai.state == ecs::MobAIState::Chase && ai.target == proxy && proxy != ecs::NULL_ENTITY;
}

} // namespace

std::vector<ecs::EntityId> rangedAttacks(ecs::Registry& reg, const world::World& world, ecs::EntityId proxy,
                                         gameplay::SplitMix64& rng) {
    struct Shot {
        ecs::EntityId shooter;
        glm::vec3 pos;
        glm::vec3 direction;
        const gameplay::RangedAttack* attack;
    };
    std::vector<Shot> shots;
    const ecs::TransformComp* player = proxyTransform(reg, proxy);
    reg.view<ecs::MobKindComp, ecs::MobAttackComp, ecs::TransformComp, ecs::VelocityComp>().each(
        [&](ecs::EntityId e, const ecs::MobKindComp& mob, ecs::MobAttackComp& cooldown, ecs::TransformComp& t,
            ecs::VelocityComp& v) {
            const gameplay::MobDef& def = gameplay::mobDef(mob.kind);
            if (!def.ranged.enabled()) {
                return;
            }
            cooldown.cooldownTicks = std::max(0, cooldown.cooldownTicks - 1);
            const ecs::MobAIComp* ai = reg.tryGet<ecs::MobAIComp>(e);
            if (player == nullptr || ai == nullptr || !chasing(*ai, proxy) || !isAlive(reg, e)) {
                return;
            }
            // Beta EntitySkeleton.attackEntity: within range of the player, who must be seen.
            if (!(glm::length(player->pos - t.pos) < def.ranged.range)) {
                return;
            }
            const glm::vec3 eye = t.pos + glm::vec3(0.0f, mobEyeHeight(mobHeight(reg, e, mob.kind)), 0.0f);
            if (!lineOfSight(world, eye, proxyEye(*player))) {
                return;
            }
            const glm::vec3 aim = proxyEye(*player) - glm::vec3(0.0f, kArrowAimBelowEyes, 0.0f);
            const glm::vec3 to = aim - eye;
            holdAndFace(t, v, to);
            if (cooldown.cooldownTicks > 0) {
                return;
            }
            cooldown.cooldownTicks = def.ranged.intervalTicks;
            const float horizontalDistance = std::sqrt(to.x * to.x + to.z * to.z);
            shots.push_back({e, eye - glm::vec3(0.0f, 0.1f, 0.0f),
                             glm::vec3(to.x, to.y + horizontalDistance * def.ranged.elevation, to.z), &def.ranged});
        });
    std::vector<ecs::EntityId> arrows;
    for (const Shot& shot : shots) {
        const glm::vec3 velocity = arrowHeading(shot.direction, shot.attack->speed, shot.attack->spread, rng);
        const ecs::EntityId arrow = spawnArrow(reg, shot.pos, velocity, shot.shooter, shot.attack->damage);
        if (arrow != ecs::NULL_ENTITY) {
            arrows.push_back(arrow);
        }
    }
    return arrows;
}

void spiderBehaviour(ecs::Registry& reg, const world::World& world, ecs::EntityId proxy, float sunBrightness,
                     gameplay::SplitMix64& rng) {
    const ecs::TransformComp* player = proxyTransform(reg, proxy);
    reg.view<ecs::MobKindComp, ecs::MobAIComp, ecs::TransformComp, ecs::VelocityComp>().each(
        [&](ecs::EntityId e, const ecs::MobKindComp& mob, ecs::MobAIComp& ai, ecs::TransformComp& t,
            ecs::VelocityComp& v) {
            const gameplay::MobDef& def = gameplay::mobDef(mob.kind);
            if (!(def.neutralInDaylight || mob.kind == gameplay::MobKind::Spider) || !isAlive(reg, e) ||
                !chasing(ai, proxy) || player == nullptr) {
                return;
            }
            // Beta EntitySpider.attackEntity: bright light makes it forget the player.
            if (def.neutralInDaylight && sunBrightness > kSpiderNeutralBrightness &&
                rng.nextInt(kSpiderForgetChance) == 0) {
                ai.target = ecs::NULL_ENTITY; // MobAISystem idles on the stale target
                return;
            }
            if (mob.kind != gameplay::MobKind::Spider || !v.onGround) {
                return;
            }
            const glm::vec3 to = player->pos - t.pos;
            const float distance = glm::length(to);
            if (!(distance > kSpiderLeapMinDistance && distance < kSpiderLeapMaxDistance) ||
                rng.nextInt(kSpiderLeapChance) != 0) {
                return;
            }
            const glm::vec3 eye = t.pos + glm::vec3(0.0f, mobEyeHeight(mobHeight(reg, e, mob.kind)), 0.0f);
            const float horizontal = std::sqrt(to.x * to.x + to.z * to.z);
            if (horizontal < 1e-4f || !lineOfSight(world, eye, proxyEye(*player))) {
                return;
            }
            v.linear.x = to.x / horizontal * kSpiderLeapSpeed + v.linear.x * 0.2f;
            v.linear.z = to.z / horizontal * kSpiderLeapSpeed + v.linear.z * 0.2f;
            v.linear.y = kSpiderLeapSpeed;
            v.onGround = false;
            ai.leaping = true;
        });
}

std::vector<CreeperBlast> tickCreeperFuses(ecs::Registry& reg, const world::World& world, ecs::EntityId proxy) {
    std::vector<CreeperBlast> blasts;
    const ecs::TransformComp* player = proxyTransform(reg, proxy);
    reg.view<ecs::FuseComp, ecs::MobKindComp, ecs::TransformComp, ecs::VelocityComp>().each(
        [&](ecs::EntityId e, ecs::FuseComp& fuse, const ecs::MobKindComp& mob, ecs::TransformComp& t,
            ecs::VelocityComp& v) {
            const gameplay::ExplodeAttack& attack = gameplay::mobDef(mob.kind).explode;
            if (!attack.enabled() || !isAlive(reg, e)) {
                return;
            }
            bool lit = false;
            glm::vec3 to(0.0f);
            if (player != nullptr && chasing(reg.get<ecs::MobAIComp>(e), proxy)) {
                to = player->pos - t.pos;
                const float distance = glm::length(to);
                // Beta EntityCreeper.attackEntity: lit under 3 blocks, kept lit under 7.
                if ((fuse.dir <= 0 && distance < attack.igniteRange) || (fuse.dir > 0 && distance < attack.cancelRange)) {
                    const glm::vec3 eye = t.pos + glm::vec3(0.0f, mobEyeHeight(mobHeight(reg, e, mob.kind)), 0.0f);
                    lit = lineOfSight(world, eye, proxyEye(*player));
                }
            }
            fuse.dir = lit ? int8_t{1} : int8_t{-1};
            fuse.ticks = std::max(0, fuse.ticks + fuse.dir);
            if (lit) {
                holdAndFace(t, v, to);
            }
            if (fuse.ticks >= attack.fuseTicks) {
                blasts.push_back({e, t.pos, attack.power});
            }
        });
    return blasts;
}

void onMobHurtByPlayer(ecs::Registry& reg, ecs::EntityId mob, ecs::EntityId proxy) {
    const ecs::MobKindComp* kind = reg.tryGet<ecs::MobKindComp>(mob);
    ecs::MobAIComp* ai = reg.tryGet<ecs::MobAIComp>(mob);
    if (kind == nullptr || ai == nullptr || !reg.valid(proxy) ||
        gameplay::mobDef(kind->kind).category != gameplay::MobCategory::Passive) {
        return;
    }
    ai->state = ecs::MobAIState::Flee;
    ai->target = proxy;
    ai->stateTimer = kFleeSeconds;
}

void ambientMobSounds(ecs::Registry& reg, const glm::vec3& listener, gameplay::SplitMix64& rng,
                      std::vector<MobSoundEvent>& out) {
    reg.view<ecs::MobKindComp, ecs::MobAIComp, ecs::TransformComp>().each(
        [&](const ecs::MobKindComp& mob, const ecs::MobAIComp&, const ecs::TransformComp& t) {
            if (glm::length(t.pos - listener) >= kAmbientSoundRange) {
                return;
            }
            if (rng.nextInt(kAmbientSoundChance) == 0) {
                out.push_back({mob.kind, MobSoundKind::Ambient, t.pos});
            }
        });
}

} // namespace betacraft::game
