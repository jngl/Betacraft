#include "game/MobSpawner.hpp"

#include "ecs/Components.hpp"
#include "ecs/Systems.hpp"
#include "gameplay/MobSpawnRules.hpp"
#include "world/Block.hpp"
#include "world/Chunk.hpp"
#include "world/World.hpp"
#include "worldgen/SeededRandom.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::game {

namespace {

using core::EngineConfig;
using gameplay::MobCategory;
using gameplay::MobKind;

[[nodiscard]] core::ChunkCoord chunkOf(const glm::vec3& pos) noexcept {
    const auto bx = static_cast<int32_t>(std::floor(pos.x));
    const auto bz = static_cast<int32_t>(std::floor(pos.z));
    return {core::floorDiv(bx, EngineConfig::CHUNK_WIDTH), core::floorDiv(bz, EngineConfig::CHUNK_DEPTH)};
}

// A cell a mob can stand in: neither solid nor liquid.
[[nodiscard]] bool isFree(world::BlockId id) noexcept {
    return !world::BlockRegistry::isSolid(id) && !world::BlockRegistry::isLiquid(id);
}

// Beta isBlockNormalCube: a full opaque block to stand on (no leaves, glass or slabs).
[[nodiscard]] bool isFloor(world::BlockId id) noexcept {
    return world::BlockRegistry::isSolid(id) && world::BlockRegistry::isOpaque(id);
}

// First free cell above the highest solid block of the column, or -1 for an empty column.
[[nodiscard]] int32_t surfaceY(const world::World& world, int32_t x, int32_t z) noexcept {
    for (int32_t y = EngineConfig::CHUNK_HEIGHT - 1; y >= 0; --y) {
        if (world::BlockRegistry::isSolid(world.getBlock(x, y, z))) {
            return y + 1;
        }
    }
    return -1;
}

[[nodiscard]] MobKind pickKind(MobCategory category, gameplay::SplitMix64& rng) noexcept {
    return gameplay::pickSpawnKind(category, rng); // S6.1: MobDef::spawnWeight
}

} // namespace

void MobSpawner::tick(world::World& world, ecs::Registry& reg, ecs::CommandBuffer& commands,
                      const glm::vec3& playerFeet, float timeOfDay, core::GameMode mode,
                      gameplay::SplitMix64& rng, const world::WeatherState& weather) {
    // Spawning ignores the mode: in Creative hostiles still appear, they only ignore the
    // player (S4.5). Peaceful is out of scope.
    (void)mode;
    ++m_ticks;
    despawn(world, reg, commands, playerFeet, m_ticks % DESPAWN_INTERVAL_TICKS == 0, rng);

    const core::ChunkCoord center = chunkOf(playerFeet);
    m_eligible.clear();
    for (int32_t dz = -SPAWN_CHUNK_RADIUS; dz <= SPAWN_CHUNK_RADIUS; ++dz) {
        for (int32_t dx = -SPAWN_CHUNK_RADIUS; dx <= SPAWN_CHUNK_RADIUS; ++dx) {
            const core::ChunkCoord c{center.x + dx, center.z + dz};
            if (world.getChunk(c) != nullptr) {
                m_eligible.push_back(c);
            }
        }
    }
    m_stats.eligibleChunks = m_eligible.size();
    m_stats.hostileCap = gameplay::mobCap(MobCategory::Hostile, m_eligible.size());
    m_stats.passiveCap = gameplay::mobCap(MobCategory::Passive, m_eligible.size());
    if (m_eligible.empty()) {
        return;
    }

    const int32_t subtracted = gameplay::skylightSubtracted(timeOfDay, weather.rainStrength,
                                                            world::thunderStrengthAt(weather, 1.0f));
    const int32_t hostileSubtracted = gameplay::hostileSkylightSubtracted(subtracted, world::isThundering(weather));
    for (const MobCategory category : {MobCategory::Hostile, MobCategory::Passive}) {
        const bool hostile = category == MobCategory::Hostile;
        int32_t& count = hostile ? m_stats.hostile : m_stats.passive;
        const int32_t cap = hostile ? m_stats.hostileCap : m_stats.passiveCap;
        if (count < cap) {
            count += trySpawnPack(world, commands, playerFeet, hostile ? hostileSubtracted : subtracted, category,
                                  cap - count, rng);
        }
    }
}

void MobSpawner::despawn(world::World& world, ecs::Registry& reg, ecs::CommandBuffer& commands,
                         const glm::vec3& playerFeet, bool ageCheck, gameplay::SplitMix64& rng) {
    m_stats.passive = 0;
    m_stats.hostile = 0;
    reg.view<ecs::MobKindComp, ecs::TransformComp>().each(
        [&](ecs::EntityId e, ecs::MobKindComp& mob, const ecs::TransformComp& t) {
            bool gone = world.getChunk(chunkOf(t.pos)) == nullptr;
            if (!gone && ageCheck) {
                mob.ageTicks += DESPAWN_INTERVAL_TICKS;
                gone = gameplay::shouldDespawn(glm::length(t.pos - playerFeet), mob.ageTicks, rng);
            }
            if (gone) {
                commands.destroy(e);
            } else if (gameplay::mobDef(mob.kind).category == MobCategory::Hostile) {
                ++m_stats.hostile;
            } else {
                ++m_stats.passive;
            }
        });
}

int32_t MobSpawner::trySpawnPack(const world::World& world, ecs::CommandBuffer& commands,
                                 const glm::vec3& playerFeet, int32_t subtracted, MobCategory category,
                                 int32_t room, gameplay::SplitMix64& rng) const {
    const bool hostile = category == MobCategory::Hostile;
    const core::ChunkCoord chunk = m_eligible[static_cast<size_t>(rng.nextInt(static_cast<int32_t>(m_eligible.size())))];
    const int32_t ox = chunk.x * EngineConfig::CHUNK_WIDTH + rng.nextInt(EngineConfig::CHUNK_WIDTH);
    const int32_t oz = chunk.z * EngineConfig::CHUNK_DEPTH + rng.nextInt(EngineConfig::CHUNK_DEPTH);
    const int32_t surface = surfaceY(world, ox, oz);
    if (surface < 1) {
        return 0;
    }
    const int32_t y = hostile ? 1 + rng.nextInt(surface) : surface;

    const auto canSpawnAt = [&](int32_t x, int32_t z) {
        if (y < 1 || y + 1 >= EngineConfig::CHUNK_HEIGHT || !isFree(world.getBlock(x, y, z)) ||
            !isFree(world.getBlock(x, y + 1, z))) {
            return false;
        }
        const world::BlockId below = world.getBlock(x, y - 1, z);
        if (!isFloor(below)) {
            return false;
        }
        const glm::vec3 feet(static_cast<float>(x) + 0.5f, static_cast<float>(y), static_cast<float>(z) + 0.5f);
        if (glm::length(feet - playerFeet) < SPAWN_MIN_DISTANCE) {
            return false;
        }
        const uint8_t sky = world.getSkyLight(x, y, z);
        const uint8_t light = gameplay::effectiveLight(sky, world.getBlockLight(x, y, z), subtracted);
        return hostile ? gameplay::hostileLightOk(sky, light, rng) : gameplay::passiveSpawnOk(below, light);
    };

    const MobKind kind = pickKind(category, rng);
    const int32_t packSize = std::min(room, 1 + rng.nextInt(MAX_PACK_SIZE));
    int32_t spawned = 0;
    for (int32_t i = 0; i < packSize; ++i) {
        const int32_t x = i == 0 ? ox : ox + rng.nextInt(2 * PACK_SPREAD + 1) - PACK_SPREAD;
        const int32_t z = i == 0 ? oz : oz + rng.nextInt(2 * PACK_SPREAD + 1) - PACK_SPREAD;
        if (!canSpawnAt(x, z)) {
            if (i == 0) {
                return 0; // the pack needs a valid origin
            }
            continue;
        }
        const glm::vec3 feet(static_cast<float>(x) + 0.5f, static_cast<float>(y), static_cast<float>(z) + 0.5f);
        const float yaw = rng.nextFloat() * 360.0f;
        commands.defer([feet, kind, yaw](ecs::Registry& r) {
            const ecs::EntityId e = ecs::EntityFactory::spawnMob(r, feet, kind);
            if (e == ecs::NULL_ENTITY) {
                return false;
            }
            r.get<ecs::TransformComp>(e).yaw = yaw;
            return true;
        });
        ++spawned;
    }
    return spawned;
}

gameplay::MobKind MobSpawner::spawnerKind(const core::BlockPos& pos) noexcept {
    const uint64_t h = worldgen::mix64(worldgen::columnHash(0x5BA3E7ULL, pos.x, pos.z, static_cast<uint32_t>(pos.y)));
    switch (h % 4U) {
        case 0: return MobKind::Skeleton;
        case 3: return MobKind::Spider;
        default: return MobKind::Zombie;
    }
}

void MobSpawner::tickSpawners(const world::World& world, ecs::Registry& reg, ecs::CommandBuffer& commands,
                              const glm::vec3& playerFeet, float timeOfDay, gameplay::SplitMix64& rng,
                              const world::WeatherState& weather) {
    if (m_spawnerTicks++ % SPAWNER_SCAN_INTERVAL_TICKS == 0) {
        scanSpawners(world, playerFeet, rng);
    }
    const int32_t subtracted = gameplay::hostileSkylightSubtracted(
        gameplay::skylightSubtracted(timeOfDay, weather.rainStrength, world::thunderStrengthAt(weather, 1.0f)),
        world::isThundering(weather));
    for (auto it = m_spawnerDelays.begin(); it != m_spawnerDelays.end();) {
        const core::BlockPos pos = it->first;
        const glm::vec3 centre(static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f,
                               static_cast<float>(pos.z) + 0.5f);
        if (world.getChunk(chunkOf(centre)) == nullptr || world.getBlock(pos) != world::BlockId::MobSpawner) {
            it = m_spawnerDelays.erase(it); // Unloaded (a reload draws a new delay) or broken
            continue;
        }
        if (glm::length(centre - playerFeet) < SPAWNER_RANGE) {
            if (it->second > 0) {
                --it->second;
            } else {
                runSpawnerCycle(world, reg, commands, pos, subtracted, it->second, rng);
            }
        }
        ++it;
    }
}

void MobSpawner::scanSpawners(const world::World& world, const glm::vec3& playerFeet, gameplay::SplitMix64& rng) {
    const auto reach = static_cast<int32_t>(SPAWNER_RANGE);
    const auto fx = static_cast<int32_t>(std::floor(playerFeet.x));
    const auto fy = static_cast<int32_t>(std::floor(playerFeet.y));
    const auto fz = static_cast<int32_t>(std::floor(playerFeet.z));
    const int32_t yLo = std::max(0, fy - reach);
    const int32_t yHi = std::min(EngineConfig::CHUNK_HEIGHT - 1, fy + reach);
    const auto isSpawner = [](world::BlockId id) { return id == world::BlockId::MobSpawner; };
    for (int32_t cz = core::floorDiv(fz - reach, EngineConfig::CHUNK_DEPTH);
         cz <= core::floorDiv(fz + reach, EngineConfig::CHUNK_DEPTH); ++cz) {
        for (int32_t cx = core::floorDiv(fx - reach, EngineConfig::CHUNK_WIDTH);
             cx <= core::floorDiv(fx + reach, EngineConfig::CHUNK_WIDTH); ++cx) {
            const world::Chunk* chunk = world.getChunk({cx, cz});
            if (chunk == nullptr) {
                continue;
            }
            for (int32_t s = yLo >> 4; s <= (yHi >> 4); ++s) {
                // Only the sections whose palette may hold a spawner are read voxel by voxel.
                if (!chunk->getSection(static_cast<size_t>(s)).mayContain(isSpawner)) {
                    continue;
                }
                for (int32_t y = std::max(yLo, s * 16); y <= std::min(yHi, s * 16 + 15); ++y) {
                    for (int32_t lz = 0; lz < EngineConfig::CHUNK_DEPTH; ++lz) {
                        for (int32_t lx = 0; lx < EngineConfig::CHUNK_WIDTH; ++lx) {
                            if (chunk->getBlock(lx, y, lz) != world::BlockId::MobSpawner) {
                                continue;
                            }
                            const core::BlockPos pos{cx * EngineConfig::CHUNK_WIDTH + lx, y,
                                                     cz * EngineConfig::CHUNK_DEPTH + lz};
                            if (!m_spawnerDelays.contains(pos)) {
                                m_spawnerDelays.emplace(pos, SPAWNER_MIN_DELAY + rng.nextInt(SPAWNER_DELAY_RANGE));
                            }
                        }
                    }
                }
            }
        }
    }
}

void MobSpawner::runSpawnerCycle(const world::World& world, ecs::Registry& reg, ecs::CommandBuffer& commands,
                                 const core::BlockPos& pos, int32_t subtracted, int32_t& delay,
                                 gameplay::SplitMix64& rng) {
    const MobKind kind = spawnerKind(pos);
    const auto restart = [&]() { delay = SPAWNER_MIN_DELAY + rng.nextInt(SPAWNER_DELAY_RANGE); };
    // Beta: entities of the class in the spawner's box grown by (8, 4, 8).
    const glm::vec3 lo(static_cast<float>(pos.x - SPAWNER_AREA_XZ), static_cast<float>(pos.y - SPAWNER_AREA_Y),
                       static_cast<float>(pos.z - SPAWNER_AREA_XZ));
    const glm::vec3 hi(static_cast<float>(pos.x + 1 + SPAWNER_AREA_XZ), static_cast<float>(pos.y + 1 + SPAWNER_AREA_Y),
                       static_cast<float>(pos.z + 1 + SPAWNER_AREA_XZ));
    int32_t nearby = 0;
    reg.view<ecs::MobKindComp, ecs::TransformComp>().each(
        [&](ecs::EntityId, const ecs::MobKindComp& mob, const ecs::TransformComp& t) {
            if (mob.kind == kind && t.pos.x >= lo.x && t.pos.x <= hi.x && t.pos.y >= lo.y && t.pos.y <= hi.y &&
                t.pos.z >= lo.z && t.pos.z <= hi.z) {
                ++nearby;
            }
        });
    for (int32_t i = 0; i < SPAWNER_TRIES; ++i) {
        if (nearby >= SPAWNER_MAX_NEARBY) {
            restart();
            return;
        }
        const float x = static_cast<float>(pos.x) + (rng.nextFloat() - rng.nextFloat()) * 4.0f;
        const int32_t y = pos.y + rng.nextInt(3) - 1;
        const float z = static_cast<float>(pos.z) + (rng.nextFloat() - rng.nextFloat()) * 4.0f;
        const auto bx = static_cast<int32_t>(std::floor(x));
        const auto bz = static_cast<int32_t>(std::floor(z));
        if (y < 1 || y + 1 >= EngineConfig::CHUNK_HEIGHT || !isFree(world.getBlock(bx, y, bz)) ||
            !isFree(world.getBlock(bx, y + 1, bz))) {
            continue;
        }
        const uint8_t sky = world.getSkyLight(bx, y, bz);
        const uint8_t light = gameplay::effectiveLight(sky, world.getBlockLight(bx, y, bz), subtracted);
        if (!gameplay::hostileLightOk(sky, light, rng)) {
            continue;
        }
        const glm::vec3 feet(x, static_cast<float>(y), z);
        const float yaw = rng.nextFloat() * 360.0f;
        commands.defer([feet, kind, yaw](ecs::Registry& r) {
            const ecs::EntityId e = ecs::EntityFactory::spawnMob(r, feet, kind);
            if (e == ecs::NULL_ENTITY) {
                return false;
            }
            r.get<ecs::TransformComp>(e).yaw = yaw;
            return true;
        });
        ++nearby;
        restart();
    }
}

} // namespace betacraft::game
