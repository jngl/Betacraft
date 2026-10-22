#include "gameplay/Crops.hpp"

#include <algorithm>

namespace betacraft::gameplay {

float cropGrowthRate(const CropNeighbourhood& cells) noexcept {
    using voxel::BlockId;
    const auto crop = [&](int dx, int dz) {
        return cells.level[static_cast<size_t>(dx + 1)][static_cast<size_t>(dz + 1)] == BlockId::Crops;
    };
    const bool alongX = crop(-1, 0) || crop(1, 0);
    const bool alongZ = crop(0, -1) || crop(0, 1);
    const bool diagonal = crop(-1, -1) || crop(1, -1) || crop(1, 1) || crop(-1, 1);

    float rate = 1.0f;
    for (size_t x = 0; x < 3; ++x) {
        for (size_t z = 0; z < 3; ++z) {
            float soil = 0.0f;
            if (cells.below[x][z] == BlockId::Farmland) {
                soil = cells.belowMeta[x][z] > 0 ? 3.0f : 1.0f;
            }
            if (x != 1 || z != 1) {
                soil /= 4.0f;
            }
            rate += soil;
        }
    }
    if (diagonal || (alongX && alongZ)) {
        rate /= 2.0f;
    }
    return rate;
}

int32_t cropGrowthBound(float rate) noexcept {
    if (rate <= 0.0f) {
        return 1;
    }
    return std::max(1, static_cast<int32_t>(kCropGrowthDivisor / rate));
}

CropHarvest rollCropHarvest(uint8_t stage, SplitMix64& rng) noexcept {
    CropHarvest out{};
    out.wheat = stage >= kCropsRipeStage ? 1 : 0;
    for (int32_t i = 0; i < kCropSeedTries; ++i) {
        if (rng.nextInt(kCropSeedRollBound) <= static_cast<int32_t>(stage)) {
            ++out.seeds;
        }
    }
    return out;
}

} // namespace betacraft::gameplay
