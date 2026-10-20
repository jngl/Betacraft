// S8.9 - S8.13 weather: Beta durations and 400-tick ramps (world::tickWeather), thunder only
// while it rains, precipitation by biome, the level.dat weather.* keys, the darker sky
// (SkyRenderer::evaluateAtmosphere overload), the lightning helpers, the debug cycle and its
// unbound GameAction, and the weather shaders on disk matching the embedded copies.
#include "tests/test_helpers.hpp"

#include "gameplay/MobSpawnRules.hpp"
#include "gameplay/SplitMix64.hpp"
#include "render/SkyRenderer.hpp"
#include "render/WeatherRenderer.hpp"
#include "storage/WorldStorage.hpp"
#include "ui/InputManager.hpp"
#include "world/Weather.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace betacraft;
using world::Precipitation;
using storage::WeatherState;

namespace {

// Ticks until `flag` flips, at most `limit`; -1 when it never did.
template <typename Flag>
int32_t ticksUntilFlip(storage::WeatherState& state, gameplay::SplitMix64& rng, Flag&& flag, int32_t limit) {
    const bool start = flag(state);
    for (int32_t t = 1; t <= limit; ++t) {
        world::tickWeather(state, rng);
        if (flag(state) != start) {
            return t;
        }
    }
    return -1;
}

std::string readRepoFile(const char* rel) {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    std::ifstream in(root / rel, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST(WeatherTest, WeatherDurationsInBetaRanges) {
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        gameplay::SplitMix64 rng(seed);
        // A fresh (clear) state draws its clear spells on the first tick.
        storage::WeatherState state{};
        world::tickWeather(state, rng);
        EXPECT_FALSE(state.raining);
        EXPECT_GE(state.rainTime, 12000);
        EXPECT_LT(state.rainTime, 180000);
        EXPECT_GE(state.thunderTime, 12000);
        EXPECT_LT(state.thunderTime, 180000);

        // Rain lasts 12000..24000 ticks, thunder 3600..15600.
        storage::WeatherState wet{.raining = true, .thundering = true};
        world::tickWeather(wet, rng);
        EXPECT_GE(wet.rainTime, 12000);
        EXPECT_LT(wet.rainTime, 24000);
        EXPECT_GE(wet.thunderTime, 3600);
        EXPECT_LT(wet.thunderTime, 15600);
    }

    // The timer counts down and flips the flag when it runs out: a rain spell of rainTime.
    gameplay::SplitMix64 rng(7);
    storage::WeatherState state{.raining = false, .rainTime = 5, .thunderTime = 1000000};
    EXPECT_EQ(ticksUntilFlip(state, rng, [](const storage::WeatherState& s) { return s.raining; }, 10), 5);
    ASSERT_TRUE(state.raining);
    world::tickWeather(state, rng); // draws the rain's duration
    const int32_t rainSpell = state.rainTime;
    EXPECT_EQ(ticksUntilFlip(state, rng, [](const storage::WeatherState& s) { return s.raining; }, 30000), rainSpell);
    EXPECT_FALSE(state.raining);
}

TEST(WeatherTest, RainStrengthRampsOver400Ticks) {
    gameplay::SplitMix64 rng(3);
    storage::WeatherState state{.raining = true, .rainTime = 100000, .thunderTime = 100000};
    for (int32_t t = 1; t <= 399; ++t) {
        world::tickWeather(state, rng);
        ASSERT_LT(state.rainStrength, 1.0f) << t;
        ASSERT_NEAR(state.rainStrength - state.prevRainStrength, world::kWeatherRampPerTick, 1e-5f);
    }
    EXPECT_NEAR(state.rainStrength, 399.0f / 400.0f, 1e-4f);
    world::tickWeather(state, rng);
    EXPECT_NEAR(state.rainStrength, 1.0f, 1e-4f);
    world::tickWeather(state, rng);
    EXPECT_FLOAT_EQ(state.rainStrength, 1.0f); // clamped
    EXPECT_TRUE(world::isRaining(state));

    // Interpolated between ticks for rendering.
    state.prevRainStrength = 0.5f;
    state.rainStrength = 0.6f;
    EXPECT_NEAR(world::rainStrengthAt(state, 0.5f), 0.55f, 1e-6f);

    // And back down over the same 400 ticks.
    state.rainStrength = 1.0f;
    state.raining = false;
    for (int32_t t = 0; t < 200; ++t) world::tickWeather(state, rng);
    EXPECT_NEAR(state.rainStrength, 0.5f, 1e-3f);
    for (int32_t t = 0; t < 201; ++t) world::tickWeather(state, rng);
    EXPECT_FLOAT_EQ(state.rainStrength, 0.0f);
    EXPECT_FALSE(world::isRaining(state));
}

TEST(WeatherTest, ThunderOnlyWhenRaining) {
    gameplay::SplitMix64 rng(11);
    storage::WeatherState state{.raining = false, .thundering = true, .rainTime = 100000, .thunderTime = 100000};
    for (int32_t t = 0; t < 1000; ++t) world::tickWeather(state, rng);
    EXPECT_FLOAT_EQ(state.thunderStrength, 0.0f);
    EXPECT_FALSE(world::isThundering(state));
    EXPECT_FALSE(world::rollLightning(state, 10000, rng));

    state.raining = true;
    for (int32_t t = 0; t < 400; ++t) world::tickWeather(state, rng);
    EXPECT_NEAR(state.thunderStrength, 1.0f, 1e-4f);
    EXPECT_TRUE(world::isThundering(state));
    EXPECT_NEAR(world::thunderStrengthAt(state, 1.0f), 1.0f, 1e-3f);

    // The rain stops: the storm fades with it, even though the thunder flag is still on.
    state.raining = false;
    for (int32_t t = 0; t < 400; ++t) world::tickWeather(state, rng);
    EXPECT_TRUE(state.thundering);
    EXPECT_FLOAT_EQ(state.thunderStrength, 0.0f);
    EXPECT_FALSE(world::isThundering(state));
}

TEST(WeatherTest, PrecipitationByBiome) {
    EXPECT_EQ(world::precipitationAt(voxel::BiomeId::Taiga), Precipitation::Snow);
    EXPECT_EQ(world::precipitationAt(voxel::BiomeId::Tundra), Precipitation::Snow);
    EXPECT_EQ(world::precipitationAt(voxel::BiomeId::Desert), Precipitation::None);
    for (const voxel::BiomeId biome :
         {voxel::BiomeId::Rainforest, voxel::BiomeId::Swampland, voxel::BiomeId::SeasonalForest, voxel::BiomeId::Forest,
          voxel::BiomeId::Savanna, voxel::BiomeId::Shrubland, voxel::BiomeId::Plains}) {
        EXPECT_EQ(world::precipitationAt(biome), Precipitation::Rain) << voxel::biomeName(biome);
    }
}

TEST(WeatherTest, LevelDataRoundtripWeather) {
    storage::WorldLevelData data{};
    data.seed = 42ULL;
    data.weather = storage::WeatherState{.raining = true,
                                         .thundering = true,
                                         .rainTime = 12345,
                                         .thunderTime = 678,
                                         .rainStrength = 0.4375f,
                                         .prevRainStrength = 0.435f,
                                         .thunderStrength = 0.1f,
                                         .prevThunderStrength = 0.0975f};
    const std::string text = storage::encodeLevelData(data);
    EXPECT_EQ(text.rfind("BETACRAFT_LEVEL_V2\n", 0), 0U) << "new keys, no version change";
    EXPECT_NE(text.find("weather.raining 1\n"), std::string::npos) << text;
    EXPECT_NE(text.find("weather.rainTime 12345\n"), std::string::npos) << text;

    storage::WorldLevelData read{};
    ASSERT_EQ(storage::decodeLevelData(text, read), storage::LevelLoadStatus::Ok);
    EXPECT_TRUE(read.weather.raining);
    EXPECT_TRUE(read.weather.thundering);
    EXPECT_EQ(read.weather.rainTime, 12345);
    EXPECT_EQ(read.weather.thunderTime, 678);
    EXPECT_FLOAT_EQ(read.weather.rainStrength, 0.4375f);
    EXPECT_FLOAT_EQ(read.weather.thunderStrength, 0.1f);
    // No jump on the first frame after loading.
    EXPECT_FLOAT_EQ(read.weather.prevRainStrength, read.weather.rainStrength);

    // Through the file, too.
    test::TempDir dir;
    storage::WorldStorage storage(dir.path());
    ASSERT_TRUE(storage.saveLevelData(data));
    storage::WorldLevelData loaded{};
    ASSERT_EQ(storage.loadLevelDataStatus(loaded), storage::LevelLoadStatus::Ok);
    EXPECT_TRUE(loaded.weather.raining);
    EXPECT_EQ(loaded.weather.rainTime, 12345);

    // Out-of-range values are clamped.
    storage::WorldLevelData clamped{};
    ASSERT_EQ(storage::decodeLevelData("BETACRAFT_LEVEL_V2\nweather.rainStrength 7\nweather.rainTime -5\n", clamped),
              storage::LevelLoadStatus::Ok);
    EXPECT_FLOAT_EQ(clamped.weather.rainStrength, 1.0f);
    EXPECT_EQ(clamped.weather.rainTime, 0);
}

TEST(WeatherTest, LegacyLevelDataDefaultsClear) {
    const auto expectClear = [](const storage::WeatherState& w) {
        EXPECT_FALSE(w.raining);
        EXPECT_FALSE(w.thundering);
        EXPECT_EQ(w.rainTime, 0);
        EXPECT_EQ(w.thunderTime, 0);
        EXPECT_FLOAT_EQ(w.rainStrength, 0.0f);
        EXPECT_FLOAT_EQ(w.thunderStrength, 0.0f);
    };
    storage::WorldLevelData v2{};
    v2.weather.raining = true; // overwritten by the decode
    ASSERT_EQ(storage::decodeLevelData("BETACRAFT_LEVEL_V2\nseed 5\ntime 0.5\n", v2), storage::LevelLoadStatus::Ok);
    expectClear(v2.weather);

    storage::WorldLevelData v1{};
    ASSERT_EQ(storage::decodeLevelData("BETACRAFT_LEVEL_V1\n173173\n5.5 77 8.5\n0 0\n0.25\n", v1),
              storage::LevelLoadStatus::LegacyV1);
    expectClear(v1.weather);
}

TEST(WeatherTest, CycleWeatherGoesClearRainThunderClear) {
    gameplay::SplitMix64 rng(5);
    storage::WeatherState state{};
    world::cycleWeather(state, rng);
    EXPECT_TRUE(state.raining);
    EXPECT_FALSE(state.thundering);
    EXPECT_GE(state.rainTime, world::kRainMinTicks);
    EXPECT_LT(state.rainTime, world::kRainMinTicks + world::kRainRangeTicks);
    world::cycleWeather(state, rng);
    EXPECT_TRUE(state.raining);
    EXPECT_TRUE(state.thundering);
    EXPECT_GE(state.thunderTime, world::kThunderMinTicks);
    EXPECT_LT(state.thunderTime, world::kThunderMinTicks + world::kThunderRangeTicks);
    world::cycleWeather(state, rng);
    EXPECT_FALSE(state.raining);
    EXPECT_FALSE(state.thundering);
    EXPECT_GE(state.rainTime, world::kClearMinTicks);
}

TEST(WeatherTest, CycleWeatherActionIsAppendedWithoutDefaultKey) {
    EXPECT_EQ(static_cast<int>(ui::GameAction::CycleWeather), static_cast<int>(ui::GameAction::DropItem) + 1);
    EXPECT_EQ(static_cast<int>(ui::GameAction::Count), static_cast<int>(ui::GameAction::CycleWeather) + 1);
    ui::InputManager input;
    EXPECT_EQ(input.getPrimaryBinding(ui::GameAction::CycleWeather), SDL_SCANCODE_UNKNOWN);
    EXPECT_EQ(ui::InputManager::getActionDisplayName(ui::GameAction::CycleWeather), "Cycle Weather");

    // A key bound in options.txt survives a save / load.
    test::TempDir dir;
    input.setBinding(ui::GameAction::CycleWeather, SDL_SCANCODE_F7);
    ASSERT_TRUE(input.saveToFile(dir.path() / "options.txt"));
    ui::InputManager reloaded;
    ASSERT_TRUE(reloaded.loadFromFile(dir.path() / "options.txt"));
    EXPECT_EQ(reloaded.getPrimaryBinding(ui::GameAction::CycleWeather), SDL_SCANCODE_F7);
}

TEST(WeatherTest, RainAndThunderDarkenTheSky) {
    const render::SkyAtmosphere clear = render::SkyRenderer::evaluateAtmosphere(0.25f, false);
    const render::SkyAtmosphere same = render::SkyRenderer::evaluateAtmosphere(0.25f, false, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(same.sunBrightness, clear.sunBrightness);
    EXPECT_FLOAT_EQ(same.zenithColor.b, clear.zenithColor.b);

    const render::SkyAtmosphere rain = render::SkyRenderer::evaluateAtmosphere(0.25f, false, 1.0f, 0.0f);
    EXPECT_NEAR(rain.sunBrightness, clear.sunBrightness * (1.0f - 0.3125f), 1e-5f);
    const render::SkyAtmosphere storm = render::SkyRenderer::evaluateAtmosphere(0.25f, false, 1.0f, 1.0f);
    EXPECT_NEAR(storm.sunBrightness, clear.sunBrightness * (1.0f - 0.625f), 1e-5f);
    // Greyer: the blue sky loses most of its saturation.
    EXPECT_LT(rain.zenithColor.b - rain.zenithColor.r, (clear.zenithColor.b - clear.zenithColor.r) * 0.5f);
    EXPECT_LT(storm.horizonColor.g, rain.horizonColor.g);
    // A lightning flash brightens the stormy sky.
    const render::SkyAtmosphere flash = render::SkyRenderer::evaluateAtmosphere(0.25f, false, 1.0f, 1.0f, 1.0f);
    EXPECT_GT(flash.zenithColor.b, storm.zenithColor.b);
    // Under water the weather is not seen.
    const render::SkyAtmosphere water = render::SkyRenderer::evaluateAtmosphere(0.25f, true, 1.0f, 1.0f);
    EXPECT_FLOAT_EQ(water.sunBrightness, render::SkyRenderer::evaluateAtmosphere(0.25f, true).sunBrightness);

    // Spawning light: +3 under the rain at noon, +5 in a storm (Beta skylightSubtracted).
    EXPECT_EQ(gameplay::skylightSubtracted(0.25f), 0);
    EXPECT_EQ(gameplay::skylightSubtracted(0.25f, 1.0f, 0.0f), 3);
    EXPECT_EQ(gameplay::skylightSubtracted(0.25f, 1.0f, 1.0f), 5);
    EXPECT_EQ(gameplay::hostileSkylightSubtracted(0, true), 10);
    EXPECT_EQ(gameplay::hostileSkylightSubtracted(3, false), 3);
}

TEST(WeatherTest, LightningOddsAndThunderDelay) {
    storage::WeatherState storm{.raining = true, .thundering = true, .rainTime = 100000, .thunderTime = 100000,
                                .rainStrength = 1.0f, .prevRainStrength = 1.0f, .thunderStrength = 1.0f,
                                .prevThunderStrength = 1.0f};
    gameplay::SplitMix64 rng(99);
    int32_t strikes = 0;
    constexpr int32_t kTicks = 200000;
    for (int32_t t = 0; t < kTicks; ++t) {
        strikes += world::rollLightning(storm, 25, rng) ? 1 : 0; // few chunks: raised to 400
    }
    // 400 in 100000 per tick: ~800 strikes, about one every 250 ticks.
    EXPECT_GT(strikes, 650);
    EXPECT_LT(strikes, 950);
    storage::WeatherState clear{};
    EXPECT_FALSE(world::rollLightning(clear, 100000, rng));

    EXPECT_EQ(world::thunderDelayTicks(0.0f), 0);
    EXPECT_EQ(world::thunderDelayTicks(343.0f), 20); // one second at the speed of sound
    EXPECT_EQ(world::thunderDelayTicks(34.3f), 2);
    EXPECT_LT(world::thunderDelayTicks(1.0e9f), 300); // clamped
}

TEST(WeatherTest, WeatherShadersMatchEmbeddedCopies) {
    const std::string vert = readRepoFile("shaders/weather.vert");
    const std::string frag = readRepoFile("shaders/weather.frag");
    ASSERT_FALSE(vert.empty());
    ASSERT_FALSE(frag.empty());
    EXPECT_EQ(std::string(render::WeatherRenderer::vertexShaderSource()), vert)
        << "shaders/weather.vert changed: update the copy in src/render/WeatherRenderer.cpp";
    EXPECT_EQ(std::string(render::WeatherRenderer::fragmentShaderSource()), frag)
        << "shaders/weather.frag changed: update the copy in src/render/WeatherRenderer.cpp";
}
