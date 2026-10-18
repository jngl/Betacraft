// Sprint 4 integration: natural mob spawning and despawning in a real World. S4.4's
// game::MobSpawner runs on a flat grass world (no caves, so daylight really means no dark
// spot): zombies appear at night at least 24 blocks away and never beyond their cap, pigs
// and sheep appear by day on grass, and a mob whose chunk is no longer loaded is removed.
// S4.5-S4.9 through game::EntitySimulation: zombies chase and hit the player (5 damage at most
// every 20 ticks, with knockback) but ignore a Creative player, burn in the sun but not under
// a roof, passives flee when hit, and death drops and mob sounds come out of the tick.
#include "tests/test_helpers.hpp"

#include "P2WorldgenSupport.hpp"
#include "core/Config.hpp"
#include "ecs/CommandBuffer.hpp"
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"
#include "game/EntitySimulation.hpp"
#include "game/MobBehaviour.hpp"
#include "game/MobSpawner.hpp"
#include "gameplay/Health.hpp"
#include "gameplay/ItemIds.hpp"
#include "gameplay/MobDefs.hpp"
#include "gameplay/MobSpawnRules.hpp"
#include "gameplay/SplitMix64.hpp"
#include "physics/Player.hpp"
#include "world/Block.hpp"
#include "world/Chunk.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

using namespace betacraft;
using gameplay::MobCategory;
using gameplay::MobKind;
using world::BlockId;

namespace {

constexpr int32_t kGroundY = 63;   // grass layer; feet stand at kGroundY + 1
constexpr int32_t kRadius = 4;     // 9 x 9 loaded chunks around the origin chunk
constexpr float kMidnight = 0.75f;
constexpr float kNoon = 0.25f;

class P2MobsSession : public ::testing::Test {
protected:
    void SetUp() override {
        world::BlockRegistry::initialize();
        m_world = std::make_unique<world::World>(test::p2worldgen::kSeed, m_dir.path());
        for (const core::ChunkCoord c : test::p2worldgen::squareRegion({0, 0}, kRadius)) {
            world::Chunk& chunk = m_world->ensureChunkLoaded(c);
            for (int32_t z = 0; z < world::Chunk::DEPTH; ++z) {
                for (int32_t x = 0; x < world::Chunk::WIDTH; ++x) {
                    for (int32_t y = 0; y < world::Chunk::HEIGHT; ++y) {
                        BlockId id = BlockId::Air;
                        if (y == 0) id = BlockId::Bedrock;
                        else if (y < kGroundY - 3) id = BlockId::Stone;
                        else if (y < kGroundY) id = BlockId::Dirt;
                        else if (y == kGroundY) id = BlockId::Grass;
                        chunk.setBlock(x, y, z, id, false);
                    }
                }
            }
            chunk.recalculateLighting();
        }
    }

    // Runs the spawner (and flushes its commands) for `ticks` ticks; `check` sees the
    // registry after every tick.
    template<typename Check>
    void run(int32_t ticks, float timeOfDay, Check&& check, core::GameMode mode = core::GameMode::Survival) {
        for (int32_t i = 0; i < ticks; ++i) {
            m_spawner.tick(*m_world, m_reg, m_commands, m_feet, timeOfDay, mode, m_rng);
            (void)m_commands.flush(m_reg);
            check();
        }
    }

    [[nodiscard]] int32_t count(MobCategory category) {
        int32_t n = 0;
        m_reg.view<ecs::MobKindComp>().each([&](const ecs::MobKindComp& mob) {
            n += gameplay::mobDef(mob.kind).category == category ? 1 : 0;
        });
        return n;
    }

    test::TempDir m_dir{"betacraft_p2_mobs_"};
    std::unique_ptr<world::World> m_world;
    ecs::Registry m_reg;
    ecs::CommandBuffer m_commands;
    game::MobSpawner m_spawner;
    gameplay::SplitMix64 m_rng{2024ULL};
    glm::vec3 m_feet{8.5f, static_cast<float>(kGroundY + 1), 8.5f};
};

} // namespace

TEST_F(P2MobsSession, ZombiesSpawnAtMidnightFarFromThePlayerAndUnderTheCap) {
    const int32_t cap = gameplay::mobCap(MobCategory::Hostile, (2 * kRadius + 1) * (2 * kRadius + 1));
    int32_t maxHostiles = 0;
    int32_t ticksToFirst = -1;
    int32_t tick = 0;
    run(2000, kMidnight, [&] {
        ++tick;
        const int32_t hostiles = count(MobCategory::Hostile);
        maxHostiles = std::max(maxHostiles, hostiles);
        if (hostiles > 0 && ticksToFirst < 0) ticksToFirst = tick;
        m_reg.view<ecs::MobKindComp, ecs::TransformComp>().each(
            [&](const ecs::MobKindComp& mob, const ecs::TransformComp& t) {
                if (mob.ageTicks == 0) { // fresh: still where it was spawned
                    EXPECT_GE(glm::length(t.pos - m_feet), game::MobSpawner::SPAWN_MIN_DISTANCE);
                    // S6.1: any hostile kind, by MobDef::spawnWeight.
                    EXPECT_EQ(gameplay::mobDef(mob.kind).category, MobCategory::Hostile);
                    EXPECT_EQ(m_world->getBlock(static_cast<int32_t>(std::floor(t.pos.x)), kGroundY,
                                                static_cast<int32_t>(std::floor(t.pos.z))),
                              BlockId::Grass);
                    EXPECT_FLOAT_EQ(t.pos.y, static_cast<float>(kGroundY + 1));
                }
            });
    });
    EXPECT_GT(ticksToFirst, 0);
    EXPECT_GE(maxHostiles, 1);
    EXPECT_LE(maxHostiles, cap);
    EXPECT_EQ(count(MobCategory::Passive), 0); // too dark for animals
    EXPECT_EQ(m_spawner.stats().hostileCap, cap);
    EXPECT_EQ(m_spawner.stats().hostile, count(MobCategory::Hostile));
    EXPECT_EQ(m_spawner.stats().eligibleChunks, static_cast<size_t>((2 * kRadius + 1) * (2 * kRadius + 1)));
}

TEST_F(P2MobsSession, ThePopulationNeverExceedsTheCap) {
    const int32_t hostileCap = gameplay::mobCap(MobCategory::Hostile, 81);
    const int32_t passiveCap = gameplay::mobCap(MobCategory::Passive, 81);
    run(6000, kMidnight, [&] {
        ASSERT_LE(count(MobCategory::Hostile), hostileCap);
    });
    run(2000, kNoon, [&] {
        ASSERT_LE(count(MobCategory::Hostile), hostileCap);
        ASSERT_LE(count(MobCategory::Passive), passiveCap);
    });
}

TEST_F(P2MobsSession, PassivesButNoHostilesAtNoon) {
    run(2000, kNoon, [&] {
        ASSERT_EQ(count(MobCategory::Hostile), 0);
    });
    EXPECT_GE(count(MobCategory::Passive), 1);
    EXPECT_LE(count(MobCategory::Passive), gameplay::mobCap(MobCategory::Passive, 81));
    m_reg.view<ecs::MobKindComp, ecs::TransformComp>().each([&](const ecs::MobKindComp& mob, const ecs::TransformComp& t) {
        EXPECT_EQ(gameplay::mobDef(mob.kind).category, MobCategory::Passive); // S6.1: pig, sheep or cow
        EXPECT_GE(glm::length(t.pos - m_feet), game::MobSpawner::SPAWN_MIN_DISTANCE);
    });
}

TEST_F(P2MobsSession, ZombiesSpawnInADarkCaveAtNoon) {
    // DoD: "la nuit, ou dans une grotte sombre". A sealed cave (cells y 30..32) under the
    // 7 x 7 central chunks gets hostiles at noon, while the sunlit surface gets none.
    constexpr int32_t kCaveFloorY = 30;
    for (int32_t cz = -3; cz <= 3; ++cz) {
        for (int32_t cx = -3; cx <= 3; ++cx) {
            world::Chunk& chunk = m_world->ensureChunkLoaded({cx, cz});
            for (int32_t z = 0; z < world::Chunk::DEPTH; ++z) {
                for (int32_t x = 0; x < world::Chunk::WIDTH; ++x) {
                    for (int32_t y = kCaveFloorY; y < kCaveFloorY + 3; ++y) {
                        chunk.setBlock(x, y, z, BlockId::Air, false);
                    }
                }
            }
            chunk.recalculateLighting();
        }
    }
    ASSERT_EQ(m_world->getSkyLight(8, kCaveFloorY, 8), 0);
    int32_t caveHostiles = 0;
    run(3000, kNoon, [&] {
        m_reg.view<ecs::MobKindComp, ecs::TransformComp>().each(
            [&](const ecs::MobKindComp& mob, const ecs::TransformComp& t) {
                if (gameplay::mobDef(mob.kind).category != MobCategory::Hostile || mob.ageTicks != 0) {
                    return;
                }
                ASSERT_FLOAT_EQ(t.pos.y, static_cast<float>(kCaveFloorY)) << "a hostile in daylight";
                ++caveHostiles;
            });
    });
    EXPECT_GT(caveHostiles, 0);
    EXPECT_LE(count(MobCategory::Hostile), gameplay::mobCap(MobCategory::Hostile, 81));
}

TEST_F(P2MobsSession, BothPassiveKindsAppear) {
    int32_t pigs = 0;
    int32_t sheep = 0;
    for (int round = 0; round < 20 && (pigs == 0 || sheep == 0); ++round) {
        run(200, kNoon, [] {});
        m_reg.view<ecs::MobKindComp>().each([&](ecs::EntityId e, const ecs::MobKindComp& mob) {
            pigs += mob.kind == MobKind::Pig ? 1 : 0;
            sheep += mob.kind == MobKind::Sheep ? 1 : 0;
            m_commands.destroy(e); // make room for the next round
        });
        (void)m_commands.flush(m_reg);
    }
    EXPECT_GT(pigs, 0);
    EXPECT_GT(sheep, 0);
}

// S6.1: natural spawning draws every kind of the category, the four hostiles at night and
// the pig, sheep and cow by day (MobDef::spawnWeight).
TEST_F(P2MobsSession, EveryNaturalKindAppears) {
    std::array<int32_t, static_cast<size_t>(MobKind::Count)> seen{};
    const auto allSeen = [&](MobCategory category) {
        for (size_t k = 0; k < seen.size(); ++k) {
            if (gameplay::mobDef(static_cast<MobKind>(k)).category == category && seen[k] == 0) return false;
        }
        return true;
    };
    for (const auto& [time, category] : {std::pair{kMidnight, MobCategory::Hostile}, std::pair{kNoon, MobCategory::Passive}}) {
        for (int round = 0; round < 40 && !allSeen(category); ++round) {
            run(200, time, [] {});
            m_reg.view<ecs::MobKindComp>().each([&](ecs::EntityId e, const ecs::MobKindComp& mob) {
                ++seen[static_cast<size_t>(mob.kind)];
                m_commands.destroy(e);
            });
            (void)m_commands.flush(m_reg);
        }
        EXPECT_TRUE(allSeen(category)) << static_cast<int>(category);
    }
}

TEST_F(P2MobsSession, MobsInUnloadedChunksAreDestroyed) {
    const ecs::EntityId lost = ecs::EntityFactory::spawnMob(m_reg, {1000.5f, 70.0f, 1000.5f}, MobKind::Zombie);
    const ecs::EntityId kept = ecs::EntityFactory::spawnMob(m_reg, {20.5f, 64.0f, 8.5f}, MobKind::Pig);
    ASSERT_EQ(m_world->getChunk({62, 62}), nullptr);
    run(1, kMidnight, [] {});
    EXPECT_FALSE(m_reg.valid(lost));
    EXPECT_TRUE(m_reg.valid(kept));

    // The chunks go away under a mob that was fine a moment ago.
    m_world->updateChunksAround(glm::vec3(100000.0f, 64.0f, 100000.0f), 0, 0);
    ASSERT_EQ(m_world->getChunk({1, 0}), nullptr);
    run(1, kMidnight, [] {});
    EXPECT_FALSE(m_reg.valid(kept));
}

TEST_F(P2MobsSession, FarMobsDespawnAndAgeEveryTwentyTicks) {
    // Beyond 128 blocks (in a loaded chunk) a mob despawns at the next check; a near one ages.
    m_feet = {-60.5f, static_cast<float>(kGroundY + 1), -60.5f};
    const ecs::EntityId far = ecs::EntityFactory::spawnMob(m_reg, {70.5f, 64.0f, 70.5f}, MobKind::Sheep);
    const ecs::EntityId near = ecs::EntityFactory::spawnMob(m_reg, {-55.5f, 64.0f, -60.5f}, MobKind::Pig);
    ASSERT_GT(glm::length(m_reg.get<ecs::TransformComp>(far).pos - m_feet), gameplay::kDespawnFarDistance);
    run(game::MobSpawner::DESPAWN_INTERVAL_TICKS - 1, kMidnight, [] {});
    EXPECT_TRUE(m_reg.valid(far));
    EXPECT_EQ(m_reg.get<ecs::MobKindComp>(near).ageTicks, 0);
    run(1, kMidnight, [] {});
    EXPECT_FALSE(m_reg.valid(far));
    ASSERT_TRUE(m_reg.valid(near));
    EXPECT_EQ(m_reg.get<ecs::MobKindComp>(near).ageTicks, game::MobSpawner::DESPAWN_INTERVAL_TICKS);
    // Within 32 blocks nothing despawns, however old.
    run(4000, kMidnight, [] {});
    EXPECT_TRUE(m_reg.valid(near));
}

namespace {

// The same flat world driven by a EntitySimulation (natural spawning off unless a test turns it
// on) and a player standing at m_feet.
class P2MobsEntitySimulation : public P2MobsSession {
protected:
    void SetUp() override {
        P2MobsSession::SetUp();
        m_session.setNaturalSpawning(false);
        m_player.setPosition(m_feet);
    }

    // The entity phase, then natural spawning, as WorldSession::tick runs them (clear weather).
    game::GameTickEvents tick(float timeOfDay) {
        game::GameTickEvents events = m_session.tick(*m_world, m_player, {}, timeOfDay);
        m_session.spawnMobs(*m_world, m_player, timeOfDay, {});
        return events;
    }

    [[nodiscard]] ecs::Registry& reg() { return m_session.entities(); }

    ecs::EntityId spawn(MobKind kind, const glm::vec3& feet) {
        return ecs::EntityFactory::spawnMob(reg(), feet, kind);
    }

    // Item entities of `id` in the session.
    [[nodiscard]] int32_t items(gameplay::ItemId id) {
        int32_t n = 0;
        reg().view<ecs::ItemDropComp>().each([&](const ecs::ItemDropComp& item) {
            n += item.id == id ? item.count : 0;
        });
        return n;
    }

    // Stone walls around the 1x1 column at (x, z) from the floor up, and a roof over it.
    void buildHut(int32_t x, int32_t z) {
        for (int32_t y = kGroundY + 1; y <= kGroundY + 3; ++y) {
            for (int32_t dz = -1; dz <= 1; ++dz) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0 && y <= kGroundY + 2) continue;
                    ASSERT_TRUE(m_world->setBlock(core::BlockPos{x + dx, y, z + dz}, BlockId::Stone));
                }
            }
        }
    }

    game::EntitySimulation m_session{99ULL};
    physics::Player m_player;
};

} // namespace

TEST_F(P2MobsEntitySimulation, TheProxyFollowsThePlayer) {
    (void)tick(kNoon);
    const ecs::EntityId proxy = m_session.playerProxy();
    ASSERT_TRUE(reg().valid(proxy));
    EXPECT_EQ(reg().get<ecs::TransformComp>(proxy).pos, m_feet);
    m_player.setPosition(m_feet + glm::vec3(3.0f, 0.0f, 0.0f));
    (void)tick(kNoon);
    EXPECT_EQ(m_session.playerProxy(), proxy);
    EXPECT_EQ(reg().get<ecs::TransformComp>(proxy).pos, m_feet + glm::vec3(3.0f, 0.0f, 0.0f));
    // Destroyed (e.g. below the void floor): recreated on the next tick.
    (void)reg().destroy(proxy);
    (void)tick(kNoon);
    EXPECT_TRUE(reg().valid(m_session.playerProxy()));
    EXPECT_TRUE(reg().has<ecs::PlayerTagComp>(m_session.playerProxy()));
}

TEST_F(P2MobsEntitySimulation, AZombieHitsForFiveAtMostEveryTwentyTicksWithKnockbackAndCanKill) {
    const ecs::EntityId zombie = spawn(MobKind::Zombie, m_feet + glm::vec3(3.0f, 0.0f, 0.0f));
    std::vector<int32_t> hitTicks;
    bool died = false;
    bool knockedBack = false;
    for (int32_t t = 0; t < 400 && !died; ++t) {
        m_player.setPosition(m_feet); // no movement integration here: the player stays put
        const glm::vec3 before = m_player.getVelocity();
        const game::GameTickEvents ev = tick(kMidnight);
        for (const game::PlayerHurtEvent& hurt : ev.playerHurt) {
            ASSERT_EQ(hurt.cause, game::DamageCause::Mob);
            EXPECT_LE(hurt.amount, 5);
            hitTicks.push_back(t);
            if (hitTicks.size() == 1) {
                // Away from the zombie (+X) and up, Beta's 0.4 blocks/tick both ways.
                const glm::vec3 v = m_player.getVelocity();
                EXPECT_LT(v.x, before.x * 0.5f - 7.0f);
                EXPECT_GT(v.y, 0.0f);
                EXPECT_LE(v.y, physics::Player::KNOCKBACK_MAX_UPWARD_SPEED);
                knockedBack = true;
            }
        }
        died = ev.playerDied;
    }
    EXPECT_TRUE(reg().valid(zombie));
    ASSERT_GE(hitTicks.size(), 2U);
    EXPECT_TRUE(knockedBack);
    for (size_t i = 1; i < hitTicks.size(); ++i) {
        EXPECT_GE(hitTicks[i] - hitTicks[i - 1], ecs::MobAttackComp::COOLDOWN_TICKS);
    }
    EXPECT_TRUE(died) << "four contact hits kill a player at full health";
    EXPECT_TRUE(m_player.isDead());
    EXPECT_EQ(hitTicks.size(), 4U);
}

TEST_F(P2MobsEntitySimulation, CreativePlayersTakeNoDamageAndAreIgnored) {
    m_player.setGameMode(core::GameMode::Creative);
    const ecs::EntityId zombie = spawn(MobKind::Zombie, m_feet + glm::vec3(1.2f, 0.0f, 0.0f));
    (void)tick(kMidnight); // creates the proxy
    auto& ai = reg().get<ecs::MobAIComp>(zombie);
    ai.state = ecs::MobAIState::Chase;
    ai.target = m_session.playerProxy();
    for (int32_t t = 0; t < 200; ++t) {
        const game::GameTickEvents ev = tick(kMidnight);
        ASSERT_TRUE(ev.playerHurt.empty());
        ASSERT_NE(reg().get<ecs::MobAIComp>(zombie).state, ecs::MobAIState::Chase) << "tick " << t;
    }
    EXPECT_EQ(m_player.health().current, m_player.health().max);
}

TEST_F(P2MobsEntitySimulation, ZombiesBurnInTheSunButNotUnderARoof) {
    // Creative: neither zombie cares about the player, so they stay where they burn.
    m_player.setGameMode(core::GameMode::Creative);
    const glm::vec3 hutFeet(-11.5f, static_cast<float>(kGroundY + 1), 8.5f);
    buildHut(-12, 8);
    ASSERT_LT(m_world->getSkyLight(-12, kGroundY + 2, 8), 15);
    const ecs::EntityId sunny = spawn(MobKind::Zombie, m_feet + glm::vec3(10.0f, 0.0f, 0.0f));
    const ecs::EntityId covered = spawn(MobKind::Zombie, hutFeet);

    // Beta rates: fire takes 1 health every 20 ticks, so 20 health last 400 ticks of burning.
    int32_t deathTick = -1;
    bool hurtSound = false;
    bool deathSound = false;
    for (int32_t t = 0; t < 1000 && deathTick < 0; ++t) {
        const game::GameTickEvents ev = tick(kNoon);
        for (const game::MobSoundEvent& s : ev.mobSounds) {
            hurtSound = hurtSound || s.sound == game::MobSoundKind::Hurt;
            deathSound = deathSound || s.sound == game::MobSoundKind::Death;
        }
        if (!reg().valid(sunny) || reg().get<ecs::HealthComp>(sunny).isDead()) {
            deathTick = t;
        }
    }
    ASSERT_GE(deathTick, 0) << "the zombie in the sun never died";
    EXPECT_LT(deathTick, 600);
    EXPECT_TRUE(hurtSound);
    EXPECT_TRUE(deathSound);
    ASSERT_TRUE(reg().valid(covered));
    EXPECT_FALSE(game::isBurning(reg(), covered));
    EXPECT_EQ(reg().get<ecs::HealthComp>(covered).current, gameplay::mobDef(MobKind::Zombie).maxHealth);

    // Night: nothing catches fire.
    const ecs::EntityId night = spawn(MobKind::Zombie, m_feet + glm::vec3(-6.0f, 0.0f, 6.0f));
    for (int32_t t = 0; t < 300; ++t) (void)tick(kMidnight);
    EXPECT_FALSE(game::isBurning(reg(), night));
}

TEST_F(P2MobsEntitySimulation, WaterPutsTheFireOut) {
    m_player.setGameMode(core::GameMode::Creative);
    const glm::vec3 feet = m_feet + glm::vec3(6.0f, 0.0f, 0.0f);
    const ecs::EntityId zombie = spawn(MobKind::Zombie, feet);
    reg().emplace<ecs::BurningComp>(zombie, ecs::BurningComp{game::kSunBurnTicks});
    ASSERT_TRUE(m_world->setBlock(core::BlockPos{14, kGroundY + 1, 8}, BlockId::Water));
    reg().get<ecs::MobAIComp>(zombie).stateTimer = 1.0e6f; // stays in the water
    (void)tick(kMidnight);
    EXPECT_FALSE(game::isBurning(reg(), zombie));
}

TEST_F(P2MobsEntitySimulation, DropsComeAfterTheDeathAnimation) {
    const ecs::EntityId sheep = spawn(MobKind::Sheep, m_feet + glm::vec3(5.0f, 0.0f, 0.0f));
    ASSERT_TRUE(ecs::HealthSystem::applyDamage(reg(), sheep, 100));
    const gameplay::ItemId wool = gameplay::toItemId(BlockId::Wool);
    int32_t deaths = 0;
    for (int32_t t = 1; t < gameplay::kDeathAnimationTicks; ++t) {
        const game::GameTickEvents ev = tick(kNoon);
        for (const game::MobSoundEvent& s : ev.mobSounds) deaths += s.sound == game::MobSoundKind::Death ? 1 : 0;
        ASSERT_TRUE(reg().valid(sheep)) << "tick " << t;
        EXPECT_TRUE(reg().has<game::CorpseComp>(sheep));
        EXPECT_EQ(items(wool), 0);
    }
    (void)tick(kNoon);
    EXPECT_FALSE(reg().valid(sheep));
    EXPECT_EQ(items(wool), 1);
    EXPECT_EQ(deaths, 1);
}

TEST_F(P2MobsEntitySimulation, ABurningPigDropsCookedPorkchop) {
    const auto killPigs = [&](bool burning) {
        for (int32_t i = 0; i < 12; ++i) {
            const ecs::EntityId pig = spawn(MobKind::Pig, m_feet + glm::vec3(4.0f + static_cast<float>(i), 0.0f, 3.0f));
            if (burning) reg().emplace<ecs::BurningComp>(pig, ecs::BurningComp{3}); // runs out before removal
            ASSERT_TRUE(ecs::HealthSystem::applyDamage(reg(), pig, 100));
        }
        for (int32_t t = 0; t < gameplay::kDeathAnimationTicks + 1; ++t) (void)tick(kNoon);
    };
    killPigs(false);
    EXPECT_GT(items(gameplay::items::RawPorkchop), 0);
    EXPECT_EQ(items(gameplay::items::CookedPorkchop), 0);
    std::vector<ecs::EntityId> drops;
    reg().view<ecs::ItemDropComp>().each([&](ecs::EntityId e, const ecs::ItemDropComp&) { drops.push_back(e); });
    for (const ecs::EntityId e : drops) (void)reg().destroy(e);
    killPigs(true);
    EXPECT_EQ(items(gameplay::items::RawPorkchop), 0);
    EXPECT_GT(items(gameplay::items::CookedPorkchop), 0);
}

TEST_F(P2MobsEntitySimulation, AHitPigFleesFromThePlayer) {
    const ecs::EntityId pig = spawn(MobKind::Pig, m_feet + glm::vec3(2.0f, 0.0f, 0.0f));
    reg().get<ecs::MobAIComp>(pig).stateTimer = 1.0e6f;
    m_player.setRotation(0.0f, -30.0f); // along +X, down at the pig's body
    game::PlayerActions attack{};
    attack.attackPressed = true;
    const game::GameTickEvents ev = m_session.tick(*m_world, m_player, attack, kNoon);
    ASSERT_TRUE(ev.melee.has_value());
    EXPECT_EQ(ev.melee->target, pig);
    const ecs::MobAIComp& ai = reg().get<ecs::MobAIComp>(pig);
    EXPECT_EQ(ai.state, ecs::MobAIState::Flee);
    EXPECT_EQ(ai.target, m_session.playerProxy());
    EXPECT_NEAR(ai.stateTimer, game::kFleeSeconds, 0.1f);
    const float start = glm::length(reg().get<ecs::TransformComp>(pig).pos - m_feet);
    for (int32_t t = 0; t < 30; ++t) (void)tick(kNoon);
    EXPECT_GT(glm::length(reg().get<ecs::TransformComp>(pig).pos - m_feet), start + 1.0f);
}

TEST_F(P2MobsEntitySimulation, AmbientSoundsOnlyFromMobsWithinSixteenBlocks) {
    m_player.setGameMode(core::GameMode::Creative);
    // Idle for good, so nobody wanders into or out of range.
    for (const auto& [kind, offset] : {std::pair{MobKind::Sheep, glm::vec3(4.0f, 0.0f, 0.0f)},
                                       std::pair{MobKind::Zombie, glm::vec3(0.0f, 0.0f, -4.0f)},
                                       std::pair{MobKind::Pig, glm::vec3(40.0f, 0.0f, 0.0f)}}) {
        reg().get<ecs::MobAIComp>(spawn(kind, m_feet + offset)).stateTimer = 1.0e6f;
    }
    int32_t sheep = 0;
    int32_t zombie = 0;
    for (int32_t t = 0; t < 1600; ++t) {
        for (const game::MobSoundEvent& s : tick(kMidnight).mobSounds) {
            ASSERT_EQ(s.sound, game::MobSoundKind::Ambient);
            EXPECT_LT(glm::length(s.pos - m_feet), game::kAmbientSoundRange);
            EXPECT_NE(s.kind, MobKind::Pig);
            sheep += s.kind == MobKind::Sheep ? 1 : 0;
            zombie += s.kind == MobKind::Zombie ? 1 : 0;
        }
    }
    // 1 in 80 per tick: about 20 each over 1600 ticks.
    EXPECT_GT(sheep, 5);
    EXPECT_GT(zombie, 5);
    EXPECT_LT(sheep, 60);
}

TEST_F(P2MobsEntitySimulation, TheSessionSpawnsMobsAtMidnight) {
    m_session.setNaturalSpawning(true);
    m_player.setGameMode(core::GameMode::Creative);
    for (int32_t t = 0; t < 2000 && m_session.spawner().stats().hostile == 0; ++t) (void)tick(kMidnight);
    EXPECT_GT(m_session.spawner().stats().hostile, 0);
    EXPECT_LE(m_session.spawner().stats().hostile, m_session.spawner().stats().hostileCap);
}
