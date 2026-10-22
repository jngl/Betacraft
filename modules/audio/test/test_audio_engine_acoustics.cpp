// P4.2 integration seam: AudioEngine routes its voices through the AcousticVoiceBank. Driven through
// renderFrames(), the device-less mixing path, so it runs on machines without audio hardware.
#include "acoustics/AcousticCommand.hpp"
#include "audio/AudioEngine.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace {

using namespace betacraft;
using audio::AudioEngine;
using audio::PlayedVoice;
using audio::SoundEffect;
using audio::VoiceAcoustics;

constexpr uint32_t kRate = AudioEngine::SAMPLE_RATE;

[[nodiscard]] std::vector<float> renderSeconds(AudioEngine& engine, double seconds, uint32_t period = 512) {
    const uint32_t total = static_cast<uint32_t>(seconds * kRate);
    std::vector<float> out(2 * static_cast<size_t>(total));
    for (uint32_t done = 0; done < total;) {
        const uint32_t n = std::min(period, total - done);
        EXPECT_TRUE(engine.renderFrames(out.data() + 2 * static_cast<size_t>(done), n));
        done += n;
    }
    return out;
}

[[nodiscard]] double rms(const std::vector<float>& stereo, double fromSeconds, double toSeconds) {
    const size_t first = 2 * static_cast<size_t>(fromSeconds * kRate);
    const size_t last = std::min(stereo.size(), 2 * static_cast<size_t>(toSeconds * kRate));
    double sum = 0.0;
    for (size_t i = first; i < last; ++i) sum += static_cast<double>(stereo[i]) * stereo[i];
    return last > first ? std::sqrt(sum / static_cast<double>(last - first)) : 0.0;
}

TEST(AudioEngineAcoustics, SpatialSoundsGetUniqueHandlesAndTheirDuration) {
    AudioEngine engine;
    const PlayedVoice a = engine.playSound(SoundEffect::LavaFizz, 0.5f, 1.0f, VoiceAcoustics{});
    const PlayedVoice b = engine.playSound(SoundEffect::LavaFizz, 0.5f, 2.0f, VoiceAcoustics{});
    ASSERT_TRUE(a.valid());
    ASSERT_TRUE(b.valid());
    EXPECT_NE(a.handle, b.handle);
    EXPECT_NEAR(a.durationSeconds, 0.5f, 1e-3f);  // The fizz buffer is 0.5 s long
    EXPECT_NEAR(b.durationSeconds, 0.25f, 1e-3f); // Played twice as fast

    // A non-spatial sound has no handle for acoustic updates to address.
    EXPECT_FALSE(engine.playSound(SoundEffect::UiClick, 0.5f, 1.0f).valid());
}

TEST(AudioEngineAcoustics, NonSpatialSoundsBypassTheReverbBus) {
    AudioEngine dry;
    AudioEngine wet;
    (void)dry.playSound(SoundEffect::StepStone, 0.5f, 1.0f);
    (void)wet.playSound(SoundEffect::StepStone, 0.5f, 1.0f, VoiceAcoustics{});
    const std::vector<float> dryOut = renderSeconds(dry, 0.6);
    const std::vector<float> wetOut = renderSeconds(wet, 0.6);

    EXPECT_GT(rms(dryOut, 0.0, 0.1), 1e-3);
    // The 0.11 s step is over by 0.2 s: only a reverb tail can remain.
    EXPECT_EQ(rms(dryOut, 0.2, 0.6), 0.0);
    EXPECT_GT(rms(wetOut, 0.2, 0.6), 1e-5);
}

TEST(AudioEngineAcoustics, InitialOcclusionAppliesFromTheFirstSample) {
    AudioEngine clear;
    AudioEngine muffled;
    (void)clear.playSound(SoundEffect::BreakStone, 0.5f, 1.0f, VoiceAcoustics{1.0f, 20000.0f, 0.0f});
    (void)muffled.playSound(SoundEffect::BreakStone, 0.5f, 1.0f, VoiceAcoustics{0.25f, 20000.0f, 0.0f});
    const std::vector<float> a = renderSeconds(clear, 0.01);
    const std::vector<float> b = renderSeconds(muffled, 0.01);
    EXPECT_NEAR(rms(b, 0.0, 0.01), 0.25 * rms(a, 0.0, 0.01), 0.01 * rms(a, 0.0, 0.01));
}

TEST(AudioEngineAcoustics, OcclusionUpdatesThroughTheCommandQueueReachThePlayingVoice) {
    AudioEngine reference;
    AudioEngine occluded;
    const VoiceAcoustics noSend{1.0f, 20000.0f, 0.0f};
    (void)reference.playSound(SoundEffect::LavaFizz, 0.5f, 1.0f, noSend);
    const PlayedVoice voice = occluded.playSound(SoundEffect::LavaFizz, 0.5f, 1.0f, noSend);
    std::vector<float> a = renderSeconds(reference, 0.1);
    std::vector<float> b = renderSeconds(occluded, 0.1);
    EXPECT_NEAR(rms(a, 0.0, 0.1), rms(b, 0.0, 0.1), 1e-7);

    ASSERT_TRUE(occluded.postAcoustic(acoustics::AcousticCommand::occlusion(voice.handle, {0.25f, 20000.0f})));
    a = renderSeconds(reference, 0.2);
    b = renderSeconds(occluded, 0.2);
    // Past the 20 ms gain ramp the voice plays at a quarter of its level.
    EXPECT_NEAR(rms(b, 0.1, 0.2), 0.25 * rms(a, 0.1, 0.2), 0.02 * rms(a, 0.1, 0.2));
    EXPECT_EQ(occluded.acousticBank().staleCommands(), 0u);
}

TEST(AudioEngineAcoustics, LateUpdateForAFinishedSoundNeverTouchesTheNextOneInItsSlot) {
    AudioEngine engine;
    const PlayedVoice first = engine.playSound(SoundEffect::UiClick, 0.5f, 1.0f, VoiceAcoustics{});
    (void)renderSeconds(engine, 0.1); // The 45 ms click ends, freeing slot 0
    EXPECT_EQ(engine.acousticBank().slotOf(first.handle), std::nullopt);

    const PlayedVoice second = engine.playSound(SoundEffect::StepStone, 0.5f, 1.0f, VoiceAcoustics{});
    ASSERT_TRUE(engine.postAcoustic(acoustics::AcousticCommand::occlusion(first.handle, {0.0f, 400.0f})));
    const std::vector<float> out = renderSeconds(engine, 0.05);
    EXPECT_EQ(engine.acousticBank().slotOf(second.handle), std::optional<size_t>{0});
    EXPECT_EQ(engine.acousticBank().staleCommands(), 1u);
    EXPECT_GT(rms(out, 0.0, 0.05), 1e-3);
}

TEST(AudioEngineAcoustics, StopAllUnbindsEveryHandle) {
    AudioEngine engine;
    const PlayedVoice a = engine.playSound(SoundEffect::LavaFizz, 0.5f, 1.0f, VoiceAcoustics{});
    const PlayedVoice b = engine.playSound(SoundEffect::WaterSplash, 0.5f, 1.0f, VoiceAcoustics{});
    (void)renderSeconds(engine, 0.01);
    ASSERT_TRUE(engine.acousticBank().slotOf(a.handle).has_value());
    engine.stopAll();
    (void)renderSeconds(engine, 0.01);
    EXPECT_FALSE(engine.acousticBank().slotOf(a.handle).has_value());
    EXPECT_FALSE(engine.acousticBank().slotOf(b.handle).has_value());
}

TEST(AudioEngineAcoustics, LongCallbacksKeepEveryReverbSend) {
    AudioEngine blocked;
    AudioEngine oneShot;
    for (AudioEngine* e : {&blocked, &oneShot}) {
        (void)e->playSound(SoundEffect::LavaFizz, 0.5f, 1.0f, VoiceAcoustics{});
        (void)e->postAcoustic(acoustics::AcousticCommand::environment({1.0f, 0.0f, 0.3f, 2.0f}, true));
    }
    const std::vector<float> a = renderSeconds(blocked, 0.5, 512);
    const std::vector<float> b = renderSeconds(oneShot, 0.5, 4096); // Longer than the 512-frame aux bus
    EXPECT_EQ(oneShot.acousticBank().droppedSendFrames(), 0u);
    // Same signal whatever the backend's period (the bus sub-blocks internally).
    EXPECT_NEAR(rms(b, 0.0, 0.5), rms(a, 0.0, 0.5), 1e-3 * rms(a, 0.0, 0.5));
}

TEST(AudioEngineAcoustics, EnvironmentCommandRetunesTheSharedReverb) {
    AudioEngine engine;
    const acoustics::ReverbParams cave{1.2f, 0.0f, 0.25f, 3.0f};
    ASSERT_TRUE(engine.postAcoustic(acoustics::AcousticCommand::environment(cave, true)));
    (void)renderSeconds(engine, 0.01);
    EXPECT_NEAR(engine.acousticBank().bus().reverb().feedbackGain(), acoustics::feedbackForDecay(3.0f), 1e-4f);
}

TEST(AudioEngineAcoustics, HeadlessMixingIsRefusedWhileADeviceOwnsTheCallback) {
    AudioEngine engine;
    EXPECT_FALSE(engine.hasDevice());
    std::vector<float> out(64);
    EXPECT_TRUE(engine.renderFrames(out.data(), 32));
    EXPECT_FALSE(engine.renderFrames(nullptr, 32));

    ASSERT_TRUE(engine.initialize()); // Real device, null backend, or silent: all are "working"
    EXPECT_EQ(engine.renderFrames(out.data(), 32), !engine.hasDevice());
    engine.shutdown();
    EXPECT_FALSE(engine.hasDevice());
    EXPECT_TRUE(engine.renderFrames(out.data(), 32));
}

} // namespace
