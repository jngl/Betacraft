#pragma once

#include <cstdint>

namespace betacraft::storage {

// Beta weather (WorldInfo raining / thundering and their timers, World.updateWeather).
// rainTime / thunderTime count down to the next flip of their flag; at 0 a new duration is
// drawn: clear 12000..180000 ticks, rain 12000..24000, thunder 3600..15600. The strengths move
// by 1/400 per tick towards their flag, so a transition takes 400 ticks (20 s); thunder
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

} // namespace betacraft::storage
