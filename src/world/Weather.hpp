#pragma once

#include "gameplay/SplitMix64.hpp"
#include "world/Chunk.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

namespace betacraft::world {

class World;

// S8.9 Beta weather (WorldInfo raining / thundering and their timers, World.updateWeather).
// rainTime / thunderTime count down to the next flip of their flag; at 0 a new duration is
// drawn: clear 12000..180000 ticks, rain 12000..24000, thunder 3600..15600. The strengths move
// by kWeatherRampPerTick towards their flag, so a transition takes 400 ticks (20 s); thunder
// only builds up while it rains. A default state (level.dat without `weather.*` keys) is clear
// and draws its first duration on the first tick.
struct WeatherState {
    bool raining{false};
    bool thundering{false};
    int32_t rainTime{0};
    int32_t thunderTime{0};
    float rainStrength{0.0f};
    float prevRainStrength{0.0f};
    float thunderStrength{0.0f};
    float prevThunderStrength{0.0f};
};

inline constexpr int32_t kClearMinTicks = 12000;
inline constexpr int32_t kClearRangeTicks = 168000; // clear: 12000 + rand(168000)
inline constexpr int32_t kRainMinTicks = 12000;
inline constexpr int32_t kRainRangeTicks = 12000;   // rain: 12000 + rand(12000)
inline constexpr int32_t kThunderMinTicks = 3600;
inline constexpr int32_t kThunderRangeTicks = 12000; // thunder: 3600 + rand(12000)
inline constexpr float kWeatherRampPerTick = 1.0f / 400.0f;
// Beta World.isRaining: rain strength above 0.2; getIsThundering: thunder x rain above 0.9.
inline constexpr float kRainingThreshold = 0.2f;
inline constexpr float kThunderingThreshold = 0.9f;

// S8.10 lightning: Beta tries 1 in 100000 per loaded chunk and tick while it thunders. Fewer
// chunks are loaded around the player here than in Beta's 400, so the count is raised to at
// least kLightningMinChunks: about one strike every 250 ticks (12.5 s) during a storm.
inline constexpr int32_t kLightningOneIn = 100000;
inline constexpr size_t kLightningMinChunks = 400;
// Thunder reaches the player at the speed of sound (343 m/s, a block being 1 m).
inline constexpr float kSpeedOfSoundBlocksPerSecond = 343.0f;

enum class Precipitation : uint8_t { None, Rain, Snow };

// One weather tick (Beta World.updateWeather, with the roadmap's 1/400 ramp).
void tickWeather(WeatherState& state, gameplay::SplitMix64& rng);

// S8.13 debug cycle: clear -> rain -> thunderstorm -> clear. The flags flip at once with fresh
// durations; the strengths still ramp over 400 ticks.
void cycleWeather(WeatherState& state, gameplay::SplitMix64& rng);

// Snow in Taiga and Tundra, nothing in the Desert, rain everywhere else.
[[nodiscard]] Precipitation precipitationAt(BiomeId biome) noexcept;

// Strengths interpolated between the last two ticks, for rendering.
[[nodiscard]] float rainStrengthAt(const WeatherState& state, float partialTick) noexcept;
[[nodiscard]] float thunderStrengthAt(const WeatherState& state, float partialTick) noexcept;
[[nodiscard]] bool isRaining(const WeatherState& state) noexcept;
[[nodiscard]] bool isThundering(const WeatherState& state) noexcept;

// Beta getPrecipitationHeight: the cell above the highest block of the column that stops rain
// (solid or liquid); 0 for an empty or unloaded column.
[[nodiscard]] int32_t precipitationHeight(const World& world, int32_t x, int32_t z) noexcept;

// Beta canLightningStrikeAt / Entity.isWet: it is raining (not snowing) on the cell of `pos`,
// which is open to the sky.
[[nodiscard]] bool isRainingAt(const World& world, const WeatherState& state, const glm::vec3& pos) noexcept;

// One lightning roll for this tick: true while thundering with a chance of
// max(loadedChunks, kLightningMinChunks) in kLightningOneIn.
[[nodiscard]] bool rollLightning(const WeatherState& state, size_t loadedChunks, gameplay::SplitMix64& rng) noexcept;

// Ticks before the thunder of a strike `distance` blocks away is heard (speed of sound).
[[nodiscard]] int32_t thunderDelayTicks(float distance) noexcept;

} // namespace betacraft::world
