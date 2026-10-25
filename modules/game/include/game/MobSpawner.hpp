#pragma once

#include "core/Config.hpp"
#include "core/Types.hpp"
#include "ecs/CommandBuffer.hpp"
#include "ecs/Registry.hpp"
#include "gameplay/MobDefs.hpp"
#include "gameplay/SplitMix64.hpp"
#include "world/Weather.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace betacraft::world { class World; }

namespace betacraft::game {

// Live mob numbers and caps, refreshed by every MobSpawner::tick (F3 overlay, tests).
struct MobSpawnerStats {
    int32_t passive{0};
    int32_t hostile{0};
    int32_t passiveCap{0};
    int32_t hostileCap{0};
    size_t eligibleChunks{0};
};

// Natural spawning and despawning around the player (Beta SpawnerAnimals and
// EntityLiving.despawnEntity), on the 20 TPS game tick.
//  * Spawning: each tick, for each category under its cap (gameplay::mobCap over the loaded
//    chunks within SPAWN_CHUNK_RADIUS), one attempt: a random loaded chunk in range, a random
//    column, a y under the surface (hostiles) or on it (passives). The cell needs two free
//    cells, a solid floor, the category's light rule and SPAWN_MIN_DISTANCE to the player;
//    a pack of 1..4 mobs of one kind then spreads within +-PACK_SPREAD, each member checked
//    the same way. The kind follows MobDef::spawnWeight: zombie, skeleton, spider
//    and creeper 10 each; sheep 12, pig 10, cow 8.
//  * Despawning: every DESPAWN_INTERVAL_TICKS mobs age by that much and
//    gameplay::shouldDespawn runs. A mob whose chunk is no longer loaded is destroyed at once
//    (checked every tick): World::getBlock reads air there and it would fall through.
// Structural changes go through the command buffer; the caller flushes it.
class MobSpawner {
public:
    static constexpr int32_t SPAWN_CHUNK_RADIUS = 8;
    static constexpr float SPAWN_MIN_DISTANCE = 24.0f;
    static constexpr int32_t PACK_SPREAD = 4;
    static constexpr int32_t MAX_PACK_SIZE = 4;
    static constexpr int32_t DESPAWN_INTERVAL_TICKS = 20;
    // Kept for reference; the weights live in gameplay::MobDef::spawnWeight.
    static constexpr int32_t PIG_WEIGHT = 10;
    static constexpr int32_t SHEEP_WEIGHT = 12;

    // `weather`: rain and thunder darken the sky light (gameplay::skylightSubtracted),
    // and during a thunderstorm hostiles read the light with skylightSubtracted 10
    // (gameplay::hostileSkylightSubtracted), so they also appear in daylight.
    void tick(world::World& world, ecs::Registry& reg, ecs::CommandBuffer& commands, const glm::vec3& playerFeet,
              float timeOfDay, core::GameMode mode, gameplay::SplitMix64& rng,
              const storage::WeatherState& weather = storage::WeatherState{});

    [[nodiscard]] const MobSpawnerStats& stats() const noexcept { return m_stats; }

    // Dungeon spawners (Beta TileEntityMobSpawner). The MobSpawner blocks within
    // SPAWNER_RANGE of the player are found every SPAWNER_SCAN_INTERVAL_TICKS and get a delay
    // of SPAWNER_MIN_DELAY + rand(SPAWNER_DELAY_RANGE) ticks; it is kept in memory only, so a
    // spawner whose chunk unloads draws a new one when it comes back. While the player is
    // within SPAWNER_RANGE the delay counts down; at 0 a cycle makes SPAWNER_TRIES attempts at
    // x + (rand - rand) x 4, y + rand(3) - 1, z + (rand - rand) x 4, each needing two free
    // cells and gameplay::hostileLightOk, unless SPAWNER_MAX_NEARBY mobs of the spawner's
    // kind are already within +-8 / +-4 / +-8 of it. Every spawn, and a full area, restart the
    // delay; a cycle that spawns nothing tries again on the next tick.
    static constexpr float SPAWNER_RANGE = 16.0f;
    static constexpr int32_t SPAWNER_SCAN_INTERVAL_TICKS = 20;
    static constexpr int32_t SPAWNER_MIN_DELAY = 200;
    static constexpr int32_t SPAWNER_DELAY_RANGE = 600;
    static constexpr int32_t SPAWNER_TRIES = 4;
    static constexpr int32_t SPAWNER_MAX_NEARBY = 6;
    static constexpr int32_t SPAWNER_AREA_XZ = 8;
    static constexpr int32_t SPAWNER_AREA_Y = 4;

    // The kind a dungeon spawner makes: hash(pos) % 4 picks skeleton, zombie, zombie, spider
    // (Beta WorldGenDungeons.pickMobSpawner).
    [[nodiscard]] static gameplay::MobKind spawnerKind(const core::BlockPos& pos) noexcept;

    void tickSpawners(const world::World& world, ecs::Registry& reg, ecs::CommandBuffer& commands,
                      const glm::vec3& playerFeet, float timeOfDay, gameplay::SplitMix64& rng,
                      const storage::WeatherState& weather = storage::WeatherState{});
    // Remaining delay of every known spawner, by block position.
    [[nodiscard]] const std::unordered_map<core::BlockPos, int32_t>& spawnerDelays() const noexcept {
        return m_spawnerDelays;
    }

private:
    void despawn(world::World& world, ecs::Registry& reg, ecs::CommandBuffer& commands, const glm::vec3& playerFeet,
                 bool ageCheck, gameplay::SplitMix64& rng);
    // Spawns up to `room` mobs of `category`; returns how many were queued.
    // `subtracted` is the sky light dimming of the category (hostiles: thunder rule applied).
    int32_t trySpawnPack(const world::World& world, ecs::CommandBuffer& commands, const glm::vec3& playerFeet,
                         int32_t subtracted, gameplay::MobCategory category, int32_t room,
                         gameplay::SplitMix64& rng) const;

    void scanSpawners(const world::World& world, const glm::vec3& playerFeet, gameplay::SplitMix64& rng);
    void runSpawnerCycle(const world::World& world, ecs::Registry& reg, ecs::CommandBuffer& commands,
                         const core::BlockPos& pos, int32_t subtracted, int32_t& delay, gameplay::SplitMix64& rng);

    std::vector<core::ChunkCoord> m_eligible; // reused each tick
    std::unordered_map<core::BlockPos, int32_t> m_spawnerDelays;
    int64_t m_spawnerTicks{0};
    MobSpawnerStats m_stats{};
    int64_t m_ticks{0};
};

} // namespace betacraft::game
