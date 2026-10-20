#include "world/Weather.hpp"

#include "voxel/Block.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::world {

namespace {

[[nodiscard]] int32_t drawDuration(bool active, int32_t activeMin, int32_t activeRange, gameplay::SplitMix64& rng) {
    return active ? activeMin + rng.nextInt(activeRange) : kClearMinTicks + rng.nextInt(kClearRangeTicks);
}

// Beta: when the timer is out a new duration is drawn; otherwise it counts down and flips the
// flag on reaching 0.
void tickTimer(bool& flag, int32_t& time, int32_t activeMin, int32_t activeRange, gameplay::SplitMix64& rng) {
    if (time <= 0) {
        time = drawDuration(flag, activeMin, activeRange, rng);
    } else if (--time <= 0) {
        flag = !flag;
    }
}

[[nodiscard]] float ramp(float value, bool up) noexcept {
    const float next = std::clamp(value + (up ? kWeatherRampPerTick : -kWeatherRampPerTick), 0.0f, 1.0f);
    // Snapped at the ends, so 400 steps land exactly on 0 or 1 despite float rounding.
    if (up && next > 1.0f - kWeatherRampPerTick * 0.5f) return 1.0f;
    if (!up && next < kWeatherRampPerTick * 0.5f) return 0.0f;
    return next;
}

[[nodiscard]] bool stopsRain(BlockId id) noexcept {
    return BlockRegistry::isSolid(id) || BlockRegistry::isLiquid(id);
}

} // namespace

void tickWeather(storage::WeatherState& state, gameplay::SplitMix64& rng) {
    tickTimer(state.thundering, state.thunderTime, kThunderMinTicks, kThunderRangeTicks, rng);
    tickTimer(state.raining, state.rainTime, kRainMinTicks, kRainRangeTicks, rng);
    state.prevRainStrength = state.rainStrength;
    state.rainStrength = ramp(state.rainStrength, state.raining);
    state.prevThunderStrength = state.thunderStrength;
    // A storm needs rain: without it the thunder flag still runs its timer but stays silent.
    state.thunderStrength = ramp(state.thunderStrength, state.thundering && state.raining);
}

void cycleWeather(storage::WeatherState& state, gameplay::SplitMix64& rng) {
    if (!state.raining) {
        state.raining = true;
        state.thundering = false;
    } else if (!state.thundering) {
        state.thundering = true;
    } else {
        state.raining = false;
        state.thundering = false;
    }
    state.rainTime = drawDuration(state.raining, kRainMinTicks, kRainRangeTicks, rng);
    state.thunderTime = drawDuration(state.thundering, kThunderMinTicks, kThunderRangeTicks, rng);
}

Precipitation precipitationAt(BiomeId biome) noexcept {
    switch (biome) {
        case BiomeId::Taiga:
        case BiomeId::Tundra: return Precipitation::Snow;
        case BiomeId::Desert: return Precipitation::None;
        default: return Precipitation::Rain;
    }
}

float rainStrengthAt(const storage::WeatherState& state, float partialTick) noexcept {
    return std::clamp(glm::mix(state.prevRainStrength, state.rainStrength, std::clamp(partialTick, 0.0f, 1.0f)), 0.0f,
                      1.0f);
}

float thunderStrengthAt(const storage::WeatherState& state, float partialTick) noexcept {
    // Beta getWeightedThunderStrength: the thunder strength scaled by the rain strength.
    const float t = std::clamp(partialTick, 0.0f, 1.0f);
    return std::clamp(glm::mix(state.prevThunderStrength, state.thunderStrength, t), 0.0f, 1.0f) *
           rainStrengthAt(state, t);
}

bool isRaining(const storage::WeatherState& state) noexcept {
    return state.rainStrength > kRainingThreshold;
}

bool isThundering(const storage::WeatherState& state) noexcept {
    return state.thunderStrength * state.rainStrength > kThunderingThreshold;
}

int32_t precipitationHeight(const World& world, int32_t x, int32_t z) noexcept {
    const Chunk* chunk = world.getChunk({core::floorDiv(x, Chunk::WIDTH), core::floorDiv(z, Chunk::DEPTH)});
    if (chunk == nullptr) {
        return 0;
    }
    const int32_t lx = core::floorMod(x, Chunk::WIDTH);
    const int32_t lz = core::floorMod(z, Chunk::DEPTH);
    for (int32_t s = static_cast<int32_t>(Chunk::SECTIONS_PER_CHUNK) - 1; s >= 0; --s) {
        // Sections of air (most of the sky) are skipped without reading a voxel.
        if (!chunk->getSection(static_cast<size_t>(s)).mayContain(stopsRain)) {
            continue;
        }
        for (int32_t y = s * PalettedSection::SIZE + PalettedSection::SIZE - 1; y >= s * PalettedSection::SIZE; --y) {
            if (stopsRain(chunk->getBlock(lx, y, lz))) {
                return y + 1;
            }
        }
    }
    return 0;
}

bool isRainingAt(const World& world, const storage::WeatherState& state, const glm::vec3& pos) noexcept {
    if (!isRaining(state)) {
        return false;
    }
    const auto x = static_cast<int32_t>(std::floor(pos.x));
    const auto y = static_cast<int32_t>(std::floor(pos.y));
    const auto z = static_cast<int32_t>(std::floor(pos.z));
    if (precipitationAt(world.getBiomeAt(x, z)) != Precipitation::Rain) {
        return false;
    }
    return y >= precipitationHeight(world, x, z);
}

bool rollLightning(const storage::WeatherState& state, size_t loadedChunks, gameplay::SplitMix64& rng) noexcept {
    if (!isThundering(state)) {
        return false;
    }
    const auto chunks = static_cast<int32_t>(std::min<size_t>(std::max(loadedChunks, kLightningMinChunks),
                                                              static_cast<size_t>(kLightningOneIn)));
    return rng.nextInt(kLightningOneIn) < chunks;
}

int32_t thunderDelayTicks(float distance) noexcept {
    if (!(distance > 0.0f)) {
        return 0;
    }
    return static_cast<int32_t>(std::lround(std::min(distance, 4096.0f) / kSpeedOfSoundBlocksPerSecond * 20.0f));
}

} // namespace betacraft::world
