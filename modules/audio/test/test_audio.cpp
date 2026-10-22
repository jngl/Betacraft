#include "audio/AudioEngine.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

using namespace betacraft::audio;
using namespace betacraft::voxel;
using namespace testing;

TEST(AudioEngineTest, LifecycleAndPlaybackCalls) {
    AudioEngine engine;
    EXPECT_TRUE(engine.initialize());

    EXPECT_FALSE(engine.isMusicEnabled());
    engine.toggleMusic();
    EXPECT_TRUE(engine.isMusicEnabled());
    engine.toggleMusic();
    EXPECT_FALSE(engine.isMusicEnabled());

    // Trigger each sound method to verify non-crashing execution
    EXPECT_NO_THROW({
        engine.playBlockStep(SoundCategory::Stone);
        engine.playBlockStep(SoundCategory::Grass);
        engine.playBlockStep(SoundCategory::Wood);
        engine.playBlockDigHit(SoundCategory::Stone);
        engine.playBlockBreak(SoundCategory::Glass);
        engine.playBlockBreak(SoundCategory::Wood);
        engine.playBlockPlace(SoundCategory::Stone);
        engine.playWaterSplash();
        engine.playPistonSound();
        engine.playUiClick();
    });

    engine.shutdown();
}

// S3.3: the hurt, fall, pop and eat sounds are synthesised with the rest of the bank,
// appended before Count so earlier SoundEffect values keep their numbers.
TEST(AudioEngineTest, NewSoundBuffersAreNonEmptyAndFinite) {
    EXPECT_EQ(static_cast<int>(SoundEffect::LavaFizz), 13);
    EXPECT_EQ(static_cast<int>(SoundEffect::Hurt), 14);
    EXPECT_EQ(static_cast<int>(SoundEffect::Eat), 18);
    EXPECT_EQ(static_cast<int>(SoundEffect::Count), 29); // S4.9 appended the five mob sounds, S5.6 the doors, S6.6 the explosion, S8.7 the click, S8.12 the thunder

    const AudioEngine engine; // No device needed: the bank is built in the constructor
    for (const SoundEffect effect : {SoundEffect::Hurt, SoundEffect::FallSmall, SoundEffect::FallBig,
                                     SoundEffect::Pop, SoundEffect::Eat}) {
        const std::span<const float> samples = engine.soundBuffer(effect);
        ASSERT_FALSE(samples.empty()) << static_cast<int>(effect);
        float peak = 0.0f;
        for (const float s : samples) {
            ASSERT_TRUE(std::isfinite(s)) << static_cast<int>(effect);
            ASSERT_LE(std::abs(s), 1.0f);
            peak = std::max(peak, std::abs(s));
        }
        EXPECT_GT(peak, 0.05f) << "audible: " << static_cast<int>(effect);
        EXPECT_LT(samples.size(), static_cast<size_t>(AudioEngine::SAMPLE_RATE)) << "a short effect";
    }
    // The big fall lasts longer than the small one.
    EXPECT_GT(engine.soundBuffer(SoundEffect::FallBig).size(), engine.soundBuffer(SoundEffect::FallSmall).size());
    EXPECT_TRUE(engine.soundBuffer(SoundEffect::Count).empty());
    // Every buffer of the bank exists.
    for (int i = 0; i < static_cast<int>(SoundEffect::Count); ++i) {
        EXPECT_FALSE(engine.soundBuffer(static_cast<SoundEffect>(i)).empty()) << i;
    }
}

// S4.9: the mob sounds are appended after Eat, so every earlier value keeps its number.
TEST(AudioEngineTest, MobSoundsAreAppendedAndAudible) {
    EXPECT_EQ(static_cast<int>(SoundEffect::ZombieGroan), 19);
    EXPECT_EQ(static_cast<int>(SoundEffect::PigOink), 20);
    EXPECT_EQ(static_cast<int>(SoundEffect::SheepBaa), 21);
    EXPECT_EQ(static_cast<int>(SoundEffect::MobHurt), 22);
    EXPECT_EQ(static_cast<int>(SoundEffect::MobDeath), 23);
    EXPECT_EQ(static_cast<int>(SoundEffect::Count), 29); // S5.6 appended the two door sounds, S6.6 the explosion, S8.7 the click, S8.12 the thunder

    const AudioEngine engine;
    for (const SoundEffect effect : {SoundEffect::ZombieGroan, SoundEffect::PigOink, SoundEffect::SheepBaa,
                                     SoundEffect::MobHurt, SoundEffect::MobDeath}) {
        const std::span<const float> samples = engine.soundBuffer(effect);
        ASSERT_FALSE(samples.empty()) << static_cast<int>(effect);
        float peak = 0.0f;
        for (const float s : samples) {
            ASSERT_TRUE(std::isfinite(s)) << static_cast<int>(effect);
            ASSERT_LE(std::abs(s), 1.0f);
            peak = std::max(peak, std::abs(s));
        }
        EXPECT_GT(peak, 0.05f) << "audible: " << static_cast<int>(effect);
        EXPECT_LT(samples.size(), static_cast<size_t>(AudioEngine::SAMPLE_RATE)) << "a short effect";
    }
    // The groan drags on, the hurt yelp is short.
    EXPECT_GT(engine.soundBuffer(SoundEffect::ZombieGroan).size(), engine.soundBuffer(SoundEffect::MobHurt).size());
}

// S5.6: the door sounds are appended after the mob sounds, before Count.
TEST(AudioEngineTest, DoorSoundsAreAppendedAndAudible) {
    EXPECT_EQ(static_cast<int>(SoundEffect::DoorOpen), 24);
    EXPECT_EQ(static_cast<int>(SoundEffect::DoorClose), 25);
    EXPECT_EQ(static_cast<int>(SoundEffect::Count), 29); // S6.6 appended the explosion, S8.7 the click, S8.12 the thunder

    const AudioEngine engine;
    for (const SoundEffect effect : {SoundEffect::DoorOpen, SoundEffect::DoorClose}) {
        const std::span<const float> samples = engine.soundBuffer(effect);
        ASSERT_FALSE(samples.empty()) << static_cast<int>(effect);
        float peak = 0.0f;
        for (const float s : samples) {
            ASSERT_TRUE(std::isfinite(s)) << static_cast<int>(effect);
            ASSERT_LE(std::abs(s), 1.0f);
            peak = std::max(peak, std::abs(s));
        }
        EXPECT_GT(peak, 0.05f) << "audible: " << static_cast<int>(effect);
        EXPECT_LT(samples.size(), static_cast<size_t>(AudioEngine::SAMPLE_RATE)) << "a short effect";
    }
    EXPECT_NE(std::vector<float>(engine.soundBuffer(SoundEffect::DoorOpen).begin(),
                                 engine.soundBuffer(SoundEffect::DoorOpen).end()),
              std::vector<float>(engine.soundBuffer(SoundEffect::DoorClose).begin(),
                                 engine.soundBuffer(SoundEffect::DoorClose).end()));
}

// S6.6: the explosion (Beta random.explode) is appended after the doors, before Count.
TEST(AudioEngineTest, ExplosionSoundIsAppendedAndAudible) {
    EXPECT_EQ(static_cast<int>(SoundEffect::Explosion), 26);
    EXPECT_EQ(static_cast<int>(SoundEffect::Count), 29); // S8.7 appended the click, S8.12 the thunder

    const AudioEngine engine;
    const std::span<const float> samples = engine.soundBuffer(SoundEffect::Explosion);
    ASSERT_FALSE(samples.empty());
    float peak = 0.0f;
    for (const float s : samples) {
        ASSERT_TRUE(std::isfinite(s));
        ASSERT_LE(std::abs(s), 1.0f);
        peak = std::max(peak, std::abs(s));
    }
    EXPECT_GT(peak, 0.2f);
    // A long rumble, longer than any door.
    EXPECT_GT(samples.size(), engine.soundBuffer(SoundEffect::DoorClose).size());
}

// S8.7: the redstone click (Beta random.click) is appended after the explosion, before Count.
TEST(AudioEngineTest, ClickSoundIsAppendedAndAudible) {
    EXPECT_EQ(static_cast<int>(SoundEffect::Click), 27);
    EXPECT_EQ(static_cast<int>(SoundEffect::Count), 29); // S8.12 appended the thunder

    const AudioEngine engine;
    const std::span<const float> samples = engine.soundBuffer(SoundEffect::Click);
    ASSERT_FALSE(samples.empty());
    float peak = 0.0f;
    for (const float s : samples) {
        ASSERT_TRUE(std::isfinite(s));
        ASSERT_LE(std::abs(s), 1.0f);
        peak = std::max(peak, std::abs(s));
    }
    EXPECT_GT(peak, 0.05f);
    // A tick: much shorter than the door sounds.
    EXPECT_LT(samples.size(), engine.soundBuffer(SoundEffect::DoorOpen).size());
}

// S8.12: the thunder (Beta ambient.weather.thunder) is appended after the click, before Count.
TEST(AudioEngineTest, ThunderSoundIsAppendedAndAudible) {
    EXPECT_EQ(static_cast<int>(SoundEffect::Thunder), 28);
    EXPECT_EQ(static_cast<int>(SoundEffect::Count), 29);

    const AudioEngine engine;
    const std::span<const float> samples = engine.soundBuffer(SoundEffect::Thunder);
    ASSERT_FALSE(samples.empty());
    float peak = 0.0f;
    for (const float s : samples) {
        ASSERT_TRUE(std::isfinite(s));
        ASSERT_LE(std::abs(s), 1.0f);
        peak = std::max(peak, std::abs(s));
    }
    EXPECT_GT(peak, 0.1f);
    // A long rumble, longer than the explosion.
    EXPECT_GT(samples.size(), engine.soundBuffer(SoundEffect::Explosion).size());
}

namespace {

[[nodiscard]] float rms(const std::vector<float>& buffer, size_t from) {
    double sum = 0.0;
    for (size_t i = from; i < buffer.size(); ++i) {
        sum += static_cast<double>(buffer[i]) * buffer[i];
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(buffer.size() - from)));
}

} // namespace

// S8.12: the rain loop follows its gain (silent at 0) and is darker when occluded.
TEST(AudioEngineTest, RainLoopFollowsGainAndOcclusion) {
    AudioEngine engine; // no device: renderFrames drives the mixer
    constexpr uint32_t kFrames = 44100 / 2;
    std::vector<float> out(static_cast<size_t>(kFrames) * 2);

    ASSERT_TRUE(engine.renderFrames(out.data(), kFrames));
    EXPECT_FLOAT_EQ(rms(out, 0), 0.0f) << "no rain, no sound";

    engine.setRainLoop(1.0f);
    EXPECT_FLOAT_EQ(engine.rainLoopGain(), 1.0f);
    ASSERT_TRUE(engine.renderFrames(out.data(), kFrames));
    EXPECT_NEAR(engine.rainLoopLevel(), 1.0f, 0.01f) << "glides to the gain within half a second";
    const float open = rms(out, out.size() / 2);
    EXPECT_GT(open, 0.01f);
    for (const float s : out) {
        ASSERT_TRUE(std::isfinite(s));
        ASSERT_LE(std::abs(s), 1.0f);
    }

    // Under a roof: low-passed, so quieter (the hiss is mostly high frequencies).
    engine.setRainLoop(1.0f, 400.0f);
    ASSERT_TRUE(engine.renderFrames(out.data(), kFrames));
    ASSERT_TRUE(engine.renderFrames(out.data(), kFrames));
    EXPECT_LT(rms(out, out.size() / 2), open * 0.6f);

    engine.setRainLoop(0.0f);
    ASSERT_TRUE(engine.renderFrames(out.data(), kFrames));
    ASSERT_TRUE(engine.renderFrames(out.data(), kFrames));
    EXPECT_LT(rms(out, out.size() / 2), 1.0e-3f);

    // Garbage in, silence out.
    engine.setRainLoop(std::nanf(""), std::nanf(""));
    EXPECT_FLOAT_EQ(engine.rainLoopGain(), 0.0f);
    EXPECT_FLOAT_EQ(engine.rainLoopCutoffHz(), 20000.0f);
}
