#include "game/Projectiles.hpp"

#include "ecs/Components.hpp"
#include "ecs/Systems.hpp"
#include "game/MobBehaviour.hpp"
#include "gameplay/MeleeCombat.hpp"
#include "physics/Raycaster.hpp"
#include "world/Block.hpp"
#include "world/World.hpp"

#include <cmath>

namespace betacraft::game {

namespace {

// java.util.Random.nextGaussian's polar method, on the session rng.
[[nodiscard]] float nextGaussian(gameplay::SplitMix64& rng) noexcept {
    for (;;) {
        const float u = rng.nextFloat() * 2.0f - 1.0f;
        const float v = rng.nextFloat() * 2.0f - 1.0f;
        const float s = u * u + v * v;
        if (s > 0.0f && s < 1.0f) {
            return u * std::sqrt(-2.0f * std::log(s) / s);
        }
    }
}

[[nodiscard]] core::BlockPos cellOf(const glm::vec3& p) noexcept {
    return {static_cast<int32_t>(std::floor(p.x)), static_cast<int32_t>(std::floor(p.y)),
            static_cast<int32_t>(std::floor(p.z))};
}

// Yaw / pitch of a flight direction, TransformComp convention (yaw 0 faces +X, 90 faces +Z;
// pitch > 0 points up).
void faceAlong(ecs::TransformComp& t, const glm::vec3& v) noexcept {
    const float horizontal = std::sqrt(v.x * v.x + v.z * v.z);
    if (horizontal > 1e-6f) {
        t.yaw = glm::degrees(std::atan2(v.z, v.x));
    }
    if (horizontal > 1e-6f || std::abs(v.y) > 1e-6f) {
        t.pitch = glm::degrees(std::atan2(v.y, horizontal));
    }
}

struct Candidate {
    ecs::EntityId entity;
    physics::AABB box;
};

} // namespace

glm::vec3 arrowHeading(const glm::vec3& direction, float speed, float spread, gameplay::SplitMix64& rng) noexcept {
    const float len = glm::length(direction);
    glm::vec3 dir = len > 1e-6f ? direction / len : glm::vec3(1.0f, 0.0f, 0.0f);
    const float sigma = kArrowSpreadScale * spread;
    dir.x += nextGaussian(rng) * sigma;
    dir.y += nextGaussian(rng) * sigma;
    dir.z += nextGaussian(rng) * sigma;
    return dir * speed;
}

ecs::EntityId spawnArrow(ecs::Registry& reg, const glm::vec3& pos, const glm::vec3& velocity, ecs::EntityId shooter,
                         int32_t damage) {
    const ecs::EntityId e = reg.create();
    if (e == ecs::NULL_ENTITY) {
        return ecs::NULL_ENTITY;
    }
    ecs::TransformComp t{pos, pos, 0.0f, 0.0f};
    faceAlong(t, velocity);
    reg.emplace<ecs::TransformComp>(e, t);
    ecs::VelocityComp v{};
    v.linear = velocity;
    v.gravityScale = 0.0f; // tickArrows applies Beta's arrow gravity and drag itself
    reg.emplace<ecs::VelocityComp>(e, v);
    reg.emplace<ecs::ArrowComp>(e, ecs::ArrowComp{.shooter = shooter, .damage = damage});
    return e;
}

ArrowTickResult tickArrows(ecs::Registry& reg, const world::World& world, ecs::EntityId proxy,
                           const ArrowPlayerHitFn& hitPlayer) {
    ArrowTickResult result;
    std::vector<ecs::EntityId> arrows;
    reg.view<ecs::ArrowComp, ecs::TransformComp, ecs::VelocityComp>().each(
        [&](ecs::EntityId e, const ecs::ArrowComp&, const ecs::TransformComp&, const ecs::VelocityComp&) {
            arrows.push_back(e);
        });
    if (arrows.empty()) {
        return result;
    }

    // Everything an arrow can hit: living mobs (HealthComp) and the player's proxy.
    std::vector<Candidate> candidates;
    reg.view<ecs::TransformComp, ecs::AABBColliderComp>().each(
        [&](ecs::EntityId e, const ecs::TransformComp& t, const ecs::AABBColliderComp& c) {
            const ecs::HealthComp* h = reg.tryGet<ecs::HealthComp>(e);
            const bool living = h != nullptr && !h->isDead();
            if (living || (e == proxy && reg.has<ecs::PlayerTagComp>(e))) {
                candidates.push_back({e, c.boundsAt(t.pos).expanded(glm::vec3(kArrowHitMargin))});
            }
        });

    std::vector<ecs::EntityId> remove;
    std::vector<physics::AABB> boxes;
    std::vector<ecs::EntityId> ids;
    for (const ecs::EntityId e : arrows) {
        if (!reg.valid(e)) {
            continue;
        }
        ecs::ArrowComp& arrow = reg.get<ecs::ArrowComp>(e);
        ecs::TransformComp& t = reg.get<ecs::TransformComp>(e);
        ecs::VelocityComp& v = reg.get<ecs::VelocityComp>(e);

        if (arrow.inGround) {
            v.linear = glm::vec3(0.0f);
            const world::BlockId holder = world.getBlock(arrow.stuckIn);
            if (!world::BlockRegistry::isSolid(holder)) {
                arrow.inGround = false; // Its block went away: it falls again
                arrow.groundTicks = 0;
                arrow.flightTicks = 0;
            } else if (++arrow.groundTicks >= kArrowGroundLifetimeTicks) {
                remove.push_back(e);
            }
            continue;
        }

        ++arrow.flightTicks;
        const glm::vec3 from = t.prevPos;
        const glm::vec3 seg = t.pos - from;
        const float len = glm::length(seg);
        if (len > 1e-6f) {
            const glm::vec3 dir = seg / len;
            const physics::RaycastHit block = physics::Raycaster::cast(world, from, dir, len);
            // Flowers, tall grass and torches let arrows through; solid blocks stop them.
            const float blockDist = block.hit && block.distance <= len && world::BlockRegistry::isSolid(block.blockId)
                                        ? block.distance
                                        : -1.0f;

            boxes.clear();
            ids.clear();
            for (const Candidate& c : candidates) {
                if (c.entity == e || (c.entity == arrow.shooter && arrow.flightTicks < kArrowShooterGraceTicks) ||
                    !reg.valid(c.entity)) {
                    continue;
                }
                boxes.push_back(c.box);
                ids.push_back(c.entity);
            }
            const auto target = gameplay::pickMeleeTarget(from, dir, boxes, blockDist >= 0.0f ? blockDist : len);
            if (target) {
                const ecs::EntityId victim = ids[target->index];
                bool landed = false;
                if (victim == proxy) {
                    const ecs::TransformComp* shooter =
                        reg.valid(arrow.shooter) ? reg.tryGet<ecs::TransformComp>(arrow.shooter) : nullptr;
                    const ArrowPlayerHit hit{e, arrow.damage, shooter != nullptr ? shooter->pos : from};
                    landed = !hitPlayer || hitPlayer(hit);
                    result.playerHits += landed ? 1 : 0;
                } else {
                    // Beta EntityLiving.knockBack from the shooter: 0.4 b/tick along the
                    // flight and 0.4 up.
                    glm::vec3 push(dir.x, 0.0f, dir.z);
                    const float pushLen = glm::length(push);
                    push = pushLen > 1e-6f ? push / pushLen * kMobKnockbackSpeed : glm::vec3(0.0f);
                    push.y = kMobKnockbackSpeed;
                    landed = ecs::HealthSystem::applyDamage(reg, victim, arrow.damage, push);
                    if (landed) {
                        result.mobsHurt.push_back(victim);
                    }
                }
                if (landed) {
                    remove.push_back(e);
                } else {
                    // Absorbed: the arrow bounces back (Beta motion *= -0.1, ticksInAir = 0).
                    t.pos = from + dir * target->distance;
                    v.linear *= -0.1f;
                    arrow.flightTicks = 0;
                    faceAlong(t, v.linear);
                }
                continue;
            }
            if (blockDist >= 0.0f) {
                // Stuck a little short of the hit point (Beta: 0.05 back along the motion).
                t.pos = from + dir * std::max(0.0f, blockDist - 0.05f);
                v.linear = glm::vec3(0.0f);
                arrow.inGround = true;
                arrow.groundTicks = 0;
                arrow.stuckIn = block.blockPos;
                ++result.stuck;
                continue;
            }
        }

        const core::BlockPos cell = cellOf(t.pos);
        const float drag = world::isWaterBlock(world.getBlock(cell.x, cell.y, cell.z)) ? kArrowWaterDrag : kArrowDrag;
        v.linear *= drag;
        v.linear.y -= kArrowGravityPerTick;
        faceAlong(t, v.linear);
    }
    for (const ecs::EntityId e : remove) {
        (void)reg.destroy(e);
    }
    return result;
}

} // namespace betacraft::game
