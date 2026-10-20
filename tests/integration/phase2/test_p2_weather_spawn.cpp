// Sprint 8 weather in a real World (S8.10 - S8.12). On the flat grass world of the mob tests:
// a thunderstorm lets hostiles spawn on the sunlit surface at noon (Beta reads their light with
// skylightSubtracted 10), zombies in the rain neither catch fire nor keep burning (Beta
// Entity.isWet) while snow does not save them, the rain and snow columns of the renderer stop
// on roofs and follow the biome, and the rain loop is muffled indoors.
#include "tests/test_helpers.hpp"

#include "P2WorldgenSupport.hpp"
#include "core/Config.hpp"
#include "ecs/CommandBuffer.hpp"
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"
#include "game/AcousticsController.hpp"
#include "game/EntitySimulation.hpp"
#include "game/MobBehaviour.hpp"
#include "game/MobSpawner.hpp"
#include "gameplay/MobDefs.hpp"
#include "gameplay/MobSpawnRules.hpp"
#include "gameplay/SplitMix64.hpp"
#include "physics/Player.hpp"
#include "render/WeatherRenderer.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "world/Weather.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace betacraft;
using gameplay::MobCategory;
using gameplay::MobKind;
using world::BlockId;

namespace {

constexpr int32_t kGroundY = 63; // grass layer; feet stand at kGroundY + 1
constexpr int32_t kRadius = 4;   // 9 x 9 loaded chunks
constexpr float kNoon = 0.25f;

[[nodiscard]] storage::WeatherState fullStorm() {
    return storage::WeatherState{.raining = true,
                               .thundering = true,
                               .rainTime = 100000,
                               .thunderTime = 100000,
                               .rainStrength = 1.0f,
                               .prevRainStrength = 1.0f,
                               .thunderStrength = 1.0f,
                               .prevThunderStrength = 1.0f};
}

[[nodiscard]] storage::WeatherState fullRain() {
    storage::WeatherState w = fullStorm();
    w.thundering = false;
    w.thunderStrength = 0.0f;
    w.prevThunderStrength = 0.0f;
    return w;
}

class P2WeatherWorld : public ::testing::Test {
protected:
    void SetUp() override {
        world::BlockRegistry::initialize();
        m_world = std::make_unique<world::World>(test::p2worldgen::kSeed, m_dir.path());
        // Everything is loaded first: decorating a neighbour could put a tree into a chunk
        // already flattened.
        for (const core::ChunkCoord c : test::p2worldgen::squareRegion({0, 0}, kRadius)) {
            (void)m_world->ensureChunkLoaded(c);
        }
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
        setBiome(world::BiomeId::Plains);
    }

    // Every loaded column gets `biome`.
    void setBiome(world::BiomeId biome) {
        for (const core::ChunkCoord c : test::p2worldgen::squareRegion({0, 0}, kRadius)) {
            world::Chunk& chunk = m_world->ensureChunkLoaded(c);
            for (int32_t z = 0; z < world::Chunk::DEPTH; ++z) {
                for (int32_t x = 0; x < world::Chunk::WIDTH; ++x) {
                    chunk.setColumnBiome(x, z, biome, chunk.getGrassTint(x, z), chunk.getFoliageTint(x, z));
                }
            }
        }
    }

    [[nodiscard]] static int32_t count(ecs::Registry& reg, MobCategory category) {
        int32_t n = 0;
        reg.view<ecs::MobKindComp>().each([&](const ecs::MobKindComp& mob) {
            n += gameplay::mobDef(mob.kind).category == category ? 1 : 0;
        });
        return n;
    }

    test::TempDir m_dir{"betacraft_p2_weather_"};
    std::unique_ptr<world::World> m_world;
    glm::vec3 m_feet{8.5f, static_cast<float>(kGroundY + 1), 8.5f};
};

// EntitySimulation on the same world with a Creative player (mobs ignore it and stay put).
class P2WeatherSession : public P2WeatherWorld {
protected:
    void SetUp() override {
        P2WeatherWorld::SetUp();
        m_session.setNaturalSpawning(false);
        m_player.setPosition(m_feet);
        m_player.setGameMode(core::GameMode::Creative);
    }

    ecs::EntityId spawnZombie(const glm::vec3& feet) {
        const ecs::EntityId e = ecs::EntityFactory::spawnMob(m_session.entities(), feet, MobKind::Zombie);
        m_session.entities().get<ecs::MobAIComp>(e).stateTimer = 1.0e6f; // stays where it is
        return e;
    }

    // Ticks at noon under m_weather; true when `zombie` caught fire at any point.
    bool burnsWithin(ecs::EntityId zombie, int32_t ticks) {
        for (int32_t t = 0; t < ticks; ++t) {
            (void)m_session.tick(*m_world, m_player, {}, kNoon, m_weather);
            if (game::isBurning(m_session.entities(), zombie)) {
                return true;
            }
        }
        return false;
    }

    game::EntitySimulation m_session{99ULL};
    physics::Player m_player;
    storage::WeatherState m_weather{}; // What the WorldSession would hand to each tick
};

} // namespace

TEST_F(P2WeatherWorld, ThunderAllowsDaytimeHostiles) {
    const auto runSpawner = [&](const storage::WeatherState& weather, int32_t ticks) {
        ecs::Registry reg;
        ecs::CommandBuffer commands;
        game::MobSpawner spawner;
        gameplay::SplitMix64 rng(2024ULL);
        int32_t surfaceHostiles = 0;
        for (int32_t i = 0; i < ticks; ++i) {
            spawner.tick(*m_world, reg, commands, m_feet, kNoon, core::GameMode::Survival, rng, weather);
            (void)commands.flush(reg);
            reg.view<ecs::MobKindComp, ecs::TransformComp>().each(
                [&](const ecs::MobKindComp& mob, const ecs::TransformComp& t) {
                    if (mob.ageTicks == 0 && gameplay::mobDef(mob.kind).category == MobCategory::Hostile) {
                        EXPECT_FLOAT_EQ(t.pos.y, static_cast<float>(kGroundY + 1)) << "on the sunlit grass";
                        ++surfaceHostiles;
                    }
                });
        }
        return std::make_pair(count(reg, MobCategory::Hostile), surfaceHostiles);
    };

    // Clear or merely rainy noon: no hostile on the sunlit surface.
    EXPECT_EQ(runSpawner(storage::WeatherState{}, 2000).first, 0);
    EXPECT_EQ(runSpawner(fullRain(), 2000).first, 0);
    // A thunderstorm: hostiles appear in broad daylight.
    const auto [stormHostiles, seen] = runSpawner(fullStorm(), 2000);
    EXPECT_GT(stormHostiles, 0);
    EXPECT_GT(seen, 0);
    EXPECT_LE(stormHostiles, gameplay::mobCap(MobCategory::Hostile, (2 * kRadius + 1) * (2 * kRadius + 1)));
}

TEST_F(P2WeatherSession, RainStopsZombieBurning) {
    // Control: clear noon, the zombie in the open catches fire.
    const ecs::EntityId sunny = spawnZombie(m_feet + glm::vec3(10.0f, 0.0f, 0.0f));
    ASSERT_TRUE(burnsWithin(sunny, 400));
    ASSERT_TRUE(m_world->getSkyLight(18, kGroundY + 2, 8) == 15);

    // Rain: the burning zombie is put out on the next tick, and no zombie catches fire again.
    m_weather = fullRain();
    ASSERT_TRUE(world::isRainingAt(*m_world, m_weather, m_feet + glm::vec3(10.0f, 0.0f, 0.0f)));
    (void)m_session.tick(*m_world, m_player, {}, kNoon, m_weather);
    EXPECT_FALSE(game::isBurning(m_session.entities(), sunny));
    const ecs::EntityId wet = spawnZombie(m_feet + glm::vec3(-10.0f, 0.0f, 0.0f));
    EXPECT_FALSE(burnsWithin(wet, 600));
    EXPECT_FALSE(game::isBurning(m_session.entities(), sunny));
    EXPECT_EQ(m_session.entities().get<ecs::HealthComp>(wet).current, gameplay::mobDef(MobKind::Zombie).maxHealth);

    // The rain stops: the sun burns them again.
    m_weather = storage::WeatherState{};
    EXPECT_TRUE(burnsWithin(wet, 600));
}

TEST_F(P2WeatherSession, SnowDoesNotSaveZombiesFromTheSun) {
    // Beta isWet only counts rain: in a snowy Taiga the sun still sets zombies on fire.
    setBiome(world::BiomeId::Taiga);
    m_weather = fullRain();
    EXPECT_FALSE(world::isRainingAt(*m_world, m_weather, m_feet));
    const ecs::EntityId zombie = spawnZombie(m_feet + glm::vec3(10.0f, 0.0f, 0.0f));
    EXPECT_TRUE(burnsWithin(zombie, 400));
}

TEST_F(P2WeatherWorld, PrecipitationColumnsFollowBiomeAndRoofs) {
    const glm::vec3 eye = m_feet + glm::vec3(0.0f, core::EngineConfig::PLAYER_EYE_HEIGHT, 0.0f);
    const auto build = [&](float rain) {
        std::vector<render::WeatherVertex> verts;
        render::WeatherRenderer::buildPrecipitation(*m_world, eye, rain, 1.0f, verts);
        return verts;
    };
    EXPECT_TRUE(build(0.0f).empty()) << "clear weather draws nothing";

    std::vector<render::WeatherVertex> rain = build(1.0f);
    ASSERT_FALSE(rain.empty());
    ASSERT_EQ(rain.size() % 6U, 0U);
    // One quad per column of the radius-10 disc (317 columns), rain kind, alpha <= strength.
    EXPECT_EQ(rain.size() / 6U, 317U);
    for (const render::WeatherVertex& v : rain) {
        ASSERT_FLOAT_EQ(v.params.z, static_cast<float>(render::WeatherVertexKind::Rain));
        ASSERT_GT(v.params.x, 0.0f);
        ASSERT_LE(v.params.x, 1.0f);
        ASSERT_GE(v.position.y, static_cast<float>(kGroundY + 1)) << "rain ends on the ground";
        ASSERT_LE(v.position.y, std::floor(eye.y) + render::WeatherRenderer::RADIUS);
    }
    const std::vector<render::WeatherVertex> drizzle = build(0.25f);
    EXPECT_LT(drizzle.front().params.x, rain.front().params.x);

    // A roof over the player: the columns under it stop on top of the roof.
    constexpr int32_t kRoofY = kGroundY + 4;
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            ASSERT_TRUE(m_world->setBlock(core::BlockPos{8 + dx, kRoofY, 8 + dz}, BlockId::Stone));
        }
    }
    EXPECT_EQ(world::precipitationHeight(*m_world, 8, 8), kRoofY + 1);
    EXPECT_EQ(world::precipitationHeight(*m_world, 12, 8), kGroundY + 1);
    EXPECT_FALSE(world::isRainingAt(*m_world, fullRain(), m_feet));
    EXPECT_TRUE(world::isRainingAt(*m_world, fullRain(), m_feet + glm::vec3(4.0f, 0.0f, 0.0f)));
    rain = build(1.0f);
    for (const render::WeatherVertex& v : rain) {
        const float cx = v.position.x;
        const float cz = v.position.z;
        if (cx > 7.6f && cx < 9.4f && cz > 7.6f && cz < 9.4f) {
            ASSERT_GE(v.position.y, static_cast<float>(kRoofY + 1)) << "no rain under the roof";
        }
    }

    // Snow in the Taiga, nothing in the desert.
    setBiome(world::BiomeId::Taiga);
    const std::vector<render::WeatherVertex> snow = build(1.0f);
    ASSERT_FALSE(snow.empty());
    for (const render::WeatherVertex& v : snow) {
        ASSERT_FLOAT_EQ(v.params.z, static_cast<float>(render::WeatherVertexKind::Snow));
    }
    setBiome(world::BiomeId::Desert);
    EXPECT_TRUE(build(1.0f).empty());
}

TEST_F(P2WeatherWorld, RainIsMuffledIndoors) {
    const glm::vec3 eye = m_feet + glm::vec3(0.0f, core::EngineConfig::PLAYER_EYE_HEIGHT, 0.0f);
    const game::RainAudibility open = game::measureRainAudibility(*m_world, eye);
    EXPECT_NEAR(open.gain, 1.0f, 1e-4f);
    EXPECT_GT(open.cutoffHz, 15000.0f);

    // A sealed stone hut around the player: walls and roof two blocks thick.
    for (int32_t y = kGroundY + 1; y <= kGroundY + 5; ++y) {
        for (int32_t dz = -3; dz <= 3; ++dz) {
            for (int32_t dx = -3; dx <= 3; ++dx) {
                const bool inside = std::abs(dx) <= 1 && std::abs(dz) <= 1 && y <= kGroundY + 3;
                if (!inside) {
                    ASSERT_TRUE(m_world->setBlock(core::BlockPos{8 + dx, y, 8 + dz}, BlockId::Stone));
                }
            }
        }
    }
    const game::RainAudibility hut = game::measureRainAudibility(*m_world, eye);
    EXPECT_LT(hut.gain, 0.7f);
    EXPECT_LT(hut.cutoffHz, open.cutoffHz * 0.5f);

    // No rain in the desert.
    setBiome(world::BiomeId::Desert);
    EXPECT_FLOAT_EQ(game::measureRainAudibility(*m_world, eye).gain, 0.0f);
}
