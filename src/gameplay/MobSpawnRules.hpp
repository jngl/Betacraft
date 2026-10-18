#pragma once

#include "gameplay/MobDefs.hpp"
#include "gameplay/SplitMix64.hpp"
#include "world/Block.hpp"

#include <cstddef>
#include <cstdint>

namespace betacraft::gameplay {

// Pure Beta 1.7.3 spawning rules (SpawnerAnimals, EntityMob / EntityAnimal.getCanSpawnHere),
// free of World and render so they can be tested alone. Times of day follow the engine's
// convention (SkyRenderer): 0 = dawn, 0.25 = noon, 0.5 = dusk, 0.75 = midnight.

inline constexpr int32_t kHostileCapBase = 70; // EnumCreatureType.monster
inline constexpr int32_t kPassiveCapBase = 15; // EnumCreatureType.creature
inline constexpr int32_t kHostileHardCap = 24;
inline constexpr int32_t kPassiveHardCap = 12;
inline constexpr size_t kCapChunkDivisor = 256;
inline constexpr float kDespawnFarDistance = 128.0f; // always despawned beyond
inline constexpr float kDespawnNearDistance = 32.0f; // never despawned within
inline constexpr int32_t kDespawnMinAgeTicks = 600;
inline constexpr int32_t kDespawnChance = 800;       // 1 in 800 per check once old and far

// World.calculateSkylightSubtracted: 0 by day up to 11 at night; rain and thunder darken
// the sky by up to 5/16 each (used from S8.10).
[[nodiscard]] int32_t skylightSubtracted(float timeOfDay, float rain = 0.0f, float thunder = 0.0f) noexcept;

// EntityMob.getCanSpawnHere during a thunderstorm (World.getIsThundering): the light of the
// spawn cell is read with skylightSubtracted forced to 10, so hostiles appear in daylight
// (S8.10). Otherwise `subtracted` is kept.
inline constexpr int32_t kThunderSkylightSubtracted = 10;
[[nodiscard]] constexpr int32_t hostileSkylightSubtracted(int32_t subtracted, bool thundering) noexcept {
    return thundering ? kThunderSkylightSubtracted : subtracted;
}

// World.getBlockLightValue: the brighter of block light and the time-dimmed sky light.
[[nodiscard]] uint8_t effectiveLight(uint8_t sky, uint8_t block, int32_t subtracted) noexcept;

// EntityMob.getCanSpawnHere: false when the raw sky light beats rand(32), otherwise
// effective <= rand(8). Never true above 7.
[[nodiscard]] bool hostileLightOk(uint8_t sky, uint8_t effective, SplitMix64& rng) noexcept;

// EntityAnimal.getCanSpawnHere: standing on grass in effective light above 8.
[[nodiscard]] bool passiveSpawnOk(world::BlockId below, uint8_t effective) noexcept;

// SpawnerAnimals: base * eligibleChunks / 256, capped at the hard cap of the category.
[[nodiscard]] int32_t mobCap(MobCategory category, size_t eligibleChunks) noexcept;

// Same curve as SkyRenderer::evaluateAtmosphere(timeOfDay, false).sunBrightness, in
// [0.16, 1], without depending on the renderer.
[[nodiscard]] float sunBrightness(float timeOfDay) noexcept;

// EntityLiving.despawnEntity: always beyond 128 blocks; past 600 ticks of age and 32 blocks,
// with a 1 in 800 chance per check.
[[nodiscard]] bool shouldDespawn(float dist, int32_t ageTicks, SplitMix64& rng) noexcept;

} // namespace betacraft::gameplay
