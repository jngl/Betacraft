// S4.2 / S4.11: the player's ECS stand-in (EntityFactory::spawnPlayerProxy) that mobs target,
// mobs built from the gameplay::mobDef table, and the jump of a walking mob stopped by a wall
// (VelocityComp::collidedHorizontally). S4.5: hostiles acquire the proxy through
// game::acquireTargets (range, line of sight, Creative / dead player) and hit it on contact.
#include "tests/test_helpers.hpp"

#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"
#include "game/EntitySimulation.hpp"
#include "game/MobBehaviour.hpp"
#include "gameplay/MobDefs.hpp"
#include "physics/Player.hpp"
#include "world/Block.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

using namespace betacraft;
using namespace betacraft::ecs;
using gameplay::MobKind;

namespace {

constexpr float DT = 0.05f;

// Flat floor at y < 0, plus whatever extra solid cells the test sets.
struct Grid {
    int wallX{1000};    // solid column x == wallX, y in [0, wallHeight)
    int wallHeight{1};

    [[nodiscard]] bool operator()(int32_t x, int32_t y, int32_t) const noexcept {
        return y < 0 || (x == wallX && y < wallHeight);
    }
};

void runPhysics(Registry& reg, const Grid& grid) {
    MobAISystem::update(reg, DT);
    MovementPhysicsSystem::update(reg, DT);
    VoxelCollisionSystem::update(reg, [&](const math::AABB& box) {
        return VoxelCollisionSystem::gridCollides(box, grid);
    });
    (void)HealthSystem::update(reg, DT);
    (void)ItemDropSystem::update(reg, DT);
    PlayerControlSystem::update(reg, DT);
}

void settle(Registry& reg, const Grid& grid, int ticks = 10) {
    for (int i = 0; i < ticks; ++i) runPhysics(reg, grid);
}

[[nodiscard]] float horizontalDistance(const glm::vec3& a, const glm::vec3& b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

} // namespace

TEST(PlayerProxy, HasOnlyTransformColliderAndTag) {
    Registry reg;
    const EntityId proxy = EntityFactory::spawnPlayerProxy(reg, {1.0f, 2.0f, 3.0f});
    ASSERT_NE(proxy, NULL_ENTITY);
    EXPECT_TRUE(reg.has<TransformComp>(proxy));
    EXPECT_TRUE(reg.has<AABBColliderComp>(proxy));
    EXPECT_TRUE(reg.has<PlayerTagComp>(proxy));
    EXPECT_FALSE(reg.has<VelocityComp>(proxy));
    EXPECT_FALSE(reg.has<HealthComp>(proxy));
    EXPECT_FALSE(reg.has<MobAIComp>(proxy));
    EXPECT_FALSE(reg.has<PlayerInputComp>(proxy));
    EXPECT_EQ(reg.get<TransformComp>(proxy).pos, glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST(PlayerProxy, NoSystemMovesTheProxy) {
    Registry reg;
    const glm::vec3 feet(0.5f, 5.0f, 0.5f); // in the air: gravity must not apply
    const EntityId proxy = EntityFactory::spawnPlayerProxy(reg, feet);
    const EntityId zombie = EntityFactory::spawnMob(reg, {4.5f, 0.0f, 0.5f}, MobKind::Zombie);
    auto& ai = reg.get<MobAIComp>(zombie);
    ai.state = MobAIState::Chase;
    ai.target = proxy;
    (void)HealthSystem::applyDamage(reg, proxy, 5, {3.0f, 3.0f, 0.0f}); // no HealthComp: ignored
    settle(reg, Grid{}, 100);
    EXPECT_EQ(reg.get<TransformComp>(proxy).pos, feet);
    EXPECT_EQ(reg.get<TransformComp>(proxy).prevPos, feet);
}

TEST(PlayerProxy, MeleeTargetNeverReturnsTheProxy) {
    world::BlockRegistry::initialize();
    game::EntitySimulation session{3ULL};
    physics::Player player;
    player.setPosition({0.5f, 64.0f, 0.5f});
    player.setRotation(0.0f, 0.0f); // looking along +X
    const EntityId proxy = EntityFactory::spawnPlayerProxy(session.entities(), player.getPosition());
    ASSERT_NE(proxy, NULL_ENTITY);
    // The look ray starts inside the proxy's box, exactly where the player stands.
    EXPECT_FALSE(session.meleeTarget(player, {}).has_value());

    // A mob in front is still found (eye height: a zombie, not a pig the ray passes over).
    const EntityId zombie = EntityFactory::spawnMob(session.entities(), {2.5f, 64.0f, 0.5f}, MobKind::Zombie);
    const auto target = session.meleeTarget(player, {});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(*target, zombie);
}

TEST(PlayerProxy, MobsFromTheTableCarryTheirDefinition) {
    Registry reg;
    const EntityId zombie = EntityFactory::spawnMob(reg, {0.0f, 0.0f, 0.0f}, MobKind::Zombie);
    const EntityId sheep = EntityFactory::spawnMob(reg, {3.0f, 0.0f, 0.0f}, MobKind::Sheep);
    const gameplay::MobDef& zd = gameplay::mobDef(MobKind::Zombie);
    EXPECT_EQ(reg.get<MobKindComp>(zombie).kind, MobKind::Zombie);
    EXPECT_EQ(reg.get<MobKindComp>(zombie).ageTicks, 0);
    EXPECT_EQ(reg.get<HealthComp>(zombie).max, zd.maxHealth);
    EXPECT_EQ(reg.get<HealthComp>(zombie).current, zd.maxHealth);
    EXPECT_FLOAT_EQ(reg.get<AABBColliderComp>(zombie).width, zd.width);
    EXPECT_FLOAT_EQ(reg.get<AABBColliderComp>(zombie).height, zd.height);
    EXPECT_FLOAT_EQ(reg.get<MobAIComp>(zombie).moveSpeed, zd.moveSpeed);
    EXPECT_TRUE(reg.has<MobAttackComp>(zombie)); // hostiles attack
    EXPECT_EQ(reg.get<MobAttackComp>(zombie).cooldownTicks, 0);
    EXPECT_FALSE(reg.has<BurningComp>(zombie));

    EXPECT_EQ(reg.get<MobKindComp>(sheep).kind, MobKind::Sheep);
    EXPECT_EQ(reg.get<HealthComp>(sheep).max, 8);
    EXPECT_FALSE(reg.has<MobAttackComp>(sheep)); // passives do not
}

TEST(PlayerProxy, AChasingMobApproachesTheProxy) {
    Registry reg;
    const glm::vec3 feet(0.5f, 0.0f, 0.5f);
    const EntityId proxy = EntityFactory::spawnPlayerProxy(reg, feet);
    const EntityId zombie = EntityFactory::spawnMob(reg, {10.5f, 0.0f, 3.5f}, MobKind::Zombie);
    auto& ai = reg.get<MobAIComp>(zombie);
    ai.state = MobAIState::Chase;
    ai.target = proxy;
    const float start = horizontalDistance(reg.get<TransformComp>(zombie).pos, feet);
    settle(reg, Grid{}, 20);
    EXPECT_EQ(reg.get<MobAIComp>(zombie).state, MobAIState::Chase);
    const float after = horizontalDistance(reg.get<TransformComp>(zombie).pos, feet);
    EXPECT_LT(after, start - 0.5f);
    settle(reg, Grid{}, 400);
    EXPECT_LE(horizontalDistance(reg.get<TransformComp>(zombie).pos, feet), MobAIParams{}.arrivalRadius + 0.1f);
}

TEST(PlayerProxy, CollidedHorizontallyTriggersJump) {
    Registry reg;
    Grid grid;
    grid.wallX = 3; // a one-block step between the zombie and its target
    const EntityId proxy = EntityFactory::spawnPlayerProxy(reg, {8.5f, 1.0f, 0.5f});
    const EntityId zombie = EntityFactory::spawnMob(reg, {0.5f, 0.0f, 0.5f}, MobKind::Zombie);
    auto& ai = reg.get<MobAIComp>(zombie);
    ai.state = MobAIState::Chase;
    ai.target = proxy;

    bool collided = false;
    bool jumped = false;
    for (int i = 0; i < 200 && !jumped; ++i) {
        const bool wasBlocked = reg.get<VelocityComp>(zombie).collidedHorizontally &&
                                reg.get<VelocityComp>(zombie).onGround;
        collided = collided || reg.get<VelocityComp>(zombie).collidedHorizontally;
        MobAISystem::update(reg, DT);
        if (wasBlocked) {
            EXPECT_FLOAT_EQ(reg.get<VelocityComp>(zombie).linear.y, MobAISystem::JUMP_SPEED);
            jumped = true;
        }
        MovementPhysicsSystem::update(reg, DT);
        VoxelCollisionSystem::update(reg, [&](const math::AABB& box) {
            return VoxelCollisionSystem::gridCollides(box, grid);
        });
    }
    EXPECT_TRUE(collided);
    EXPECT_TRUE(jumped);

    // Left to itself it climbs the step and walks on past the wall.
    settle(reg, grid, 200);
    const glm::vec3 pos = reg.get<TransformComp>(zombie).pos;
    EXPECT_GT(pos.x, 3.5f);
    EXPECT_FALSE(reg.get<VelocityComp>(zombie).collidedHorizontally);
}

TEST(PlayerProxy, CollisionFlagFollowsTheLastSweep) {
    Registry reg;
    Grid grid;
    grid.wallX = 2;
    grid.wallHeight = 3; // too high to jump
    const EntityId mob = EntityFactory::spawnMob(reg, {0.5f, 0.0f, 0.5f}, 4, 0.6f, 1.8f);
    reg.get<MobAIComp>(mob).stateTimer = 1.0e6f; // stays Idle: no steering, no jump
    settle(reg, grid, 5);
    EXPECT_FALSE(reg.get<VelocityComp>(mob).collidedHorizontally);
    auto& t = reg.get<TransformComp>(mob);
    t.prevPos = t.pos;
    t.pos.x += 2.0f; // straight into the wall
    VoxelCollisionSystem::update(reg, [&](const math::AABB& box) {
        return VoxelCollisionSystem::gridCollides(box, grid);
    });
    EXPECT_TRUE(reg.get<VelocityComp>(mob).collidedHorizontally);
    EXPECT_NEAR(t.pos.x, 2.0f - 0.3f, 1e-3f);
    t.prevPos = t.pos;
    t.pos.x -= 0.5f; // away from it
    VoxelCollisionSystem::update(reg, [&](const math::AABB& box) {
        return VoxelCollisionSystem::gridCollides(box, grid);
    });
    EXPECT_FALSE(reg.get<VelocityComp>(mob).collidedHorizontally);

    // An Idle mob pressed against the wall does not jump.
    reg.get<VelocityComp>(mob).collidedHorizontally = true;
    reg.get<VelocityComp>(mob).onGround = true;
    MobAISystem::update(reg, DT);
    EXPECT_LE(reg.get<VelocityComp>(mob).linear.y, 0.0f);
}

namespace {

// No chunk is loaded, so World::getBlock reads air everywhere: line of sight is always clear.
struct EmptyWorld {
    EmptyWorld() {
        world::BlockRegistry::initialize();
        world = std::make_unique<world::World>(7ULL, dir.path());
    }
    test::TempDir dir{"betacraft_proxy_world_"};
    std::unique_ptr<world::World> world;
};

// Runs acquireTargets for `ticks` consecutive ticks starting at `firstTick`.
void acquire(Registry& reg, const world::World& world, EntityId proxy, core::GameMode mode, uint64_t firstTick,
             int ticks, bool playerDead = false) {
    for (int i = 0; i < ticks; ++i) {
        game::acquireTargets(reg, world, proxy, mode, firstTick + static_cast<uint64_t>(i), playerDead);
    }
}

} // namespace

TEST(PlayerProxy, AcquireTargetsSwitchesAHostileToChaseThenItApproaches) {
    EmptyWorld w;
    Registry reg;
    const glm::vec3 feet(0.5f, 0.0f, 0.5f);
    const EntityId proxy = EntityFactory::spawnPlayerProxy(reg, feet);
    const EntityId zombie = EntityFactory::spawnMob(reg, {10.5f, 0.0f, 3.5f}, MobKind::Zombie);
    const EntityId pig = EntityFactory::spawnMob(reg, {3.5f, 0.0f, 0.5f}, MobKind::Pig);
    reg.get<MobAIComp>(zombie).stateTimer = 1.0e6f; // Idle, not wandering off
    reg.get<MobAIComp>(pig).stateTimer = 1.0e6f;

    // One search every kTargetSearchIntervalTicks, staggered by the entity index.
    const uint32_t phase = entityIndex(zombie) % game::kTargetSearchIntervalTicks;
    const uint64_t searchTick = (game::kTargetSearchIntervalTicks - phase) % game::kTargetSearchIntervalTicks;
    if (searchTick > 0) {
        acquire(reg, *w.world, proxy, core::GameMode::Survival, 0, static_cast<int>(searchTick));
        EXPECT_EQ(reg.get<MobAIComp>(zombie).state, MobAIState::Idle) << "searched before its tick";
    }
    game::acquireTargets(reg, *w.world, proxy, core::GameMode::Survival, searchTick);
    EXPECT_EQ(reg.get<MobAIComp>(zombie).state, MobAIState::Chase);
    EXPECT_EQ(reg.get<MobAIComp>(zombie).target, proxy);
    EXPECT_EQ(reg.get<MobAIComp>(pig).state, MobAIState::Idle) << "passives never chase";

    const float start = horizontalDistance(reg.get<TransformComp>(zombie).pos, feet);
    settle(reg, Grid{}, 60);
    EXPECT_EQ(reg.get<MobAIComp>(zombie).state, MobAIState::Chase);
    EXPECT_LT(horizontalDistance(reg.get<TransformComp>(zombie).pos, feet), start - 2.0f);
}

TEST(PlayerProxy, NoAcquisitionOutOfRangeOrBehindAWall) {
    EmptyWorld w;
    Registry reg;
    const EntityId proxy = EntityFactory::spawnPlayerProxy(reg, {0.5f, 64.0f, 0.5f});
    const EntityId far = EntityFactory::spawnMob(reg, {17.0f, 64.0f, 0.5f}, MobKind::Zombie);
    const EntityId hidden = EntityFactory::spawnMob(reg, {0.5f, 64.0f, 8.5f}, MobKind::Zombie);
    reg.get<MobAIComp>(far).stateTimer = 1.0e6f;
    reg.get<MobAIComp>(hidden).stateTimer = 1.0e6f;
    // A stone wall between the hidden zombie and the player, from the feet to above the eyes.
    world::Chunk& chunk = w.world->ensureChunkLoaded({0, 0});
    for (int32_t y = 0; y < world::Chunk::HEIGHT; ++y) {
        for (int32_t z = 0; z < world::Chunk::DEPTH; ++z) {
            for (int32_t x = 0; x < world::Chunk::WIDTH; ++x) {
                chunk.setBlock(x, y, z, (z == 4 && y >= 64 && y <= 67) ? world::BlockId::Stone : world::BlockId::Air,
                               false);
            }
        }
    }
    acquire(reg, *w.world, proxy, core::GameMode::Survival, 0, 2 * game::kTargetSearchIntervalTicks);
    EXPECT_EQ(reg.get<MobAIComp>(far).state, MobAIState::Idle);
    EXPECT_EQ(reg.get<MobAIComp>(hidden).state, MobAIState::Idle);

    // The wall goes away: the zombie behind it now sees the player.
    for (int32_t y = 64; y <= 67; ++y) {
        for (int32_t x = 0; x < world::Chunk::WIDTH; ++x) {
            chunk.setBlock(x, y, 4, world::BlockId::Air, false);
        }
    }
    acquire(reg, *w.world, proxy, core::GameMode::Survival, 0, game::kTargetSearchIntervalTicks);
    EXPECT_EQ(reg.get<MobAIComp>(hidden).state, MobAIState::Chase);
    EXPECT_EQ(reg.get<MobAIComp>(far).state, MobAIState::Idle);
}

TEST(PlayerProxy, CreativeOrDeadPlayersAreDropped) {
    EmptyWorld w;
    Registry reg;
    const EntityId proxy = EntityFactory::spawnPlayerProxy(reg, {0.5f, 0.0f, 0.5f});
    const EntityId zombie = EntityFactory::spawnMob(reg, {4.5f, 0.0f, 0.5f}, MobKind::Zombie);
    reg.get<MobAIComp>(zombie).stateTimer = 1.0e6f;

    acquire(reg, *w.world, proxy, core::GameMode::Creative, 0, 2 * game::kTargetSearchIntervalTicks);
    EXPECT_EQ(reg.get<MobAIComp>(zombie).state, MobAIState::Idle);

    acquire(reg, *w.world, proxy, core::GameMode::Survival, 0, game::kTargetSearchIntervalTicks);
    ASSERT_EQ(reg.get<MobAIComp>(zombie).state, MobAIState::Chase);
    // Switching to Creative drops the target on the very next tick.
    game::acquireTargets(reg, *w.world, proxy, core::GameMode::Creative, 1);
    EXPECT_EQ(reg.get<MobAIComp>(zombie).target, NULL_ENTITY);
    settle(reg, Grid{}, 1);
    EXPECT_EQ(reg.get<MobAIComp>(zombie).state, MobAIState::Idle);

    acquire(reg, *w.world, proxy, core::GameMode::Survival, 0, game::kTargetSearchIntervalTicks);
    ASSERT_EQ(reg.get<MobAIComp>(zombie).state, MobAIState::Chase);
    game::acquireTargets(reg, *w.world, proxy, core::GameMode::Survival, 1, /*playerDead=*/true);
    EXPECT_EQ(reg.get<MobAIComp>(zombie).target, NULL_ENTITY);
}

TEST(PlayerProxy, MeleeContactNeedsReachYOverlapAndCooldown) {
    Registry reg;
    const EntityId proxy = EntityFactory::spawnPlayerProxy(reg, {0.5f, 0.0f, 0.5f});
    const EntityId zombie = EntityFactory::spawnMob(reg, {1.9f, 0.0f, 0.5f}, MobKind::Zombie);
    auto& ai = reg.get<MobAIComp>(zombie);
    ai.state = MobAIState::Chase;
    ai.target = proxy;

    std::vector<game::MobMeleeHit> hits = game::meleeContact(reg, proxy);
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits[0].mob, zombie);
    EXPECT_EQ(hits[0].damage, 5);
    EXPECT_EQ(hits[0].sourcePos, reg.get<TransformComp>(zombie).pos);
    EXPECT_EQ(reg.get<MobAttackComp>(zombie).cooldownTicks, MobAttackComp::COOLDOWN_TICKS);

    // Nothing until the cooldown ran out, then the next hit.
    int ticks = 0;
    do {
        hits = game::meleeContact(reg, proxy);
        ++ticks;
    } while (hits.empty() && ticks < 100);
    EXPECT_EQ(ticks, MobAttackComp::COOLDOWN_TICKS);

    // Out of reach, or stood on a pillar above the player's box: no hit.
    reg.get<MobAttackComp>(zombie).cooldownTicks = 0;
    reg.get<TransformComp>(zombie).pos = {2.6f, 0.0f, 0.5f};
    EXPECT_TRUE(game::meleeContact(reg, proxy).empty());
    reg.get<TransformComp>(zombie).pos = {1.5f, 1.85f, 0.5f};
    EXPECT_TRUE(game::meleeContact(reg, proxy).empty());
    // A zombie that is not chasing the player does not hit it.
    reg.get<TransformComp>(zombie).pos = {1.5f, 0.0f, 0.5f};
    reg.get<MobAIComp>(zombie).state = MobAIState::Idle;
    EXPECT_TRUE(game::meleeContact(reg, proxy).empty());
}

TEST(PlayerProxy, MeleeContactThroughAWallNeedsLineOfSight) {
    // Beta EntityCreature only attacks a target it can see: a zombie pressed against the
    // other side of a one-block wall must not hit the player through it.
    EmptyWorld w;
    world::Chunk& chunk = w.world->ensureChunkLoaded({0, 0});
    for (int32_t y = 0; y < world::Chunk::HEIGHT; ++y) {
        for (int32_t z = 0; z < world::Chunk::DEPTH; ++z) {
            for (int32_t x = 0; x < world::Chunk::WIDTH; ++x) {
                chunk.setBlock(x, y, z, (x == 4 && y >= 64 && y <= 66) ? world::BlockId::Stone : world::BlockId::Air,
                               false);
            }
        }
    }
    Registry reg;
    const EntityId proxy = EntityFactory::spawnPlayerProxy(reg, {3.7f, 64.0f, 0.5f});
    const EntityId zombie = EntityFactory::spawnMob(reg, {5.3f, 64.0f, 0.5f}, MobKind::Zombie);
    auto& ai = reg.get<MobAIComp>(zombie);
    ai.state = MobAIState::Chase;
    ai.target = proxy;

    EXPECT_TRUE(game::meleeContact(reg, *w.world, proxy).empty());
    EXPECT_EQ(reg.get<MobAttackComp>(zombie).cooldownTicks, 0) << "a blocked hit keeps the cooldown ready";

    // The wall goes away: the same contact now lands.
    for (int32_t y = 64; y <= 66; ++y) {
        for (int32_t z = 0; z < world::Chunk::DEPTH; ++z) {
            chunk.setBlock(4, y, z, world::BlockId::Air, false);
        }
    }
    const std::vector<game::MobMeleeHit> hits = game::meleeContact(reg, *w.world, proxy);
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits[0].damage, 5);
}
