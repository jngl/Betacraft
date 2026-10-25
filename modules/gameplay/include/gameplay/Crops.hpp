#pragma once

#include "gameplay/SplitMix64.hpp"
#include "voxel/Block.hpp"

#include <array>
#include <cstdint>

// Wheat rules (Beta 1.7.3 BlockCrops / BlockFarmland), free of World so they can be
// unit-tested; world/BlockTicks.cpp feeds them from the world.

namespace betacraft::gameplay {

inline constexpr uint8_t kCropsRipeStage = 7;
// Beta BlockCrops.updateTick: a growth step when rand((int)(100 / rate)) == 0.
inline constexpr float kCropGrowthDivisor = 100.0f;
// Beta BlockCrops.dropBlockAsItemWithChance: 3 tries, each a seed when rand(15) <= stage.
inline constexpr int32_t kCropSeedTries = 3;
inline constexpr int32_t kCropSeedRollBound = 15;
// Beta BlockFarmland: moisture 7 next to water, then one less per dry update.
inline constexpr uint8_t kFarmlandWetMoisture = 7;
inline constexpr int32_t kFarmlandWaterReach = 4; // Water within x/z +-4, y 0..+1
inline constexpr int32_t kFarmlandUpdateOneIn = 5; // Beta: only 1 random tick in 5 does anything
// Beta BlockFarmland.onFallenUpon: a landing tramples it when rand < fall - 0.5.
inline constexpr float kFarmlandTrampleOffset = 0.5f;
// Beta BlockFlower.canBlockStay: crops need this much light, or the open sky.
inline constexpr uint8_t kCropsMinStayLight = 8;
inline constexpr uint8_t kCropsMinGrowLight = 9;

// The 3x3 cells around a crop, indexed [dx + 1][dz + 1]: the blocks on the crop's level and
// the blocks (with metadata) below them.
struct CropNeighbourhood {
    std::array<std::array<voxel::BlockId, 3>, 3> level{};
    std::array<std::array<voxel::BlockId, 3>, 3> below{};
    std::array<std::array<uint8_t, 3>, 3> belowMeta{};
};

// Beta BlockCrops.getGrowthRate: 1, plus per farmland cell under the 3x3 (1 dry, 3 wet; a
// quarter of that for the 8 outer cells), halved when another crop stands on a diagonal or
// on both the X and the Z axis. 10 at most (wet 3x3, no crowding).
[[nodiscard]] float cropGrowthRate(const CropNeighbourhood& cells) noexcept;

// The rand bound of one growth step: (int)(kCropGrowthDivisor / rate), at least 1.
[[nodiscard]] int32_t cropGrowthBound(float rate) noexcept;

struct CropHarvest {
    int32_t wheat{0}; // 1 when ripe
    int32_t seeds{0}; // 0..kCropSeedTries
};

// What breaking a crop at `stage` yields (Beta BlockCrops: wheat at stage 7, then three
// rand(15) <= stage seed tries).
[[nodiscard]] CropHarvest rollCropHarvest(uint8_t stage, SplitMix64& rng) noexcept;

} // namespace betacraft::gameplay
