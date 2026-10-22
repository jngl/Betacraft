// P4.2 integration seam, game-thread side: game::AcousticsController gives each world sound its
// initial occlusion, tracks it for the 10 Hz OcclusionScheduler until it has played out, and posts
// enclosure changes. Checked against the sparse fake grid; the real-World path is covered by
// tests/integration/phase2/test_p2_acoustics_*.cpp.
#include "test_support/AcousticsTestUtils.hpp"
#include "acoustics/WorldSolidity.hpp"
#include "audio/AudioEngine.hpp"
#include "game/AcousticsController.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

using namespace betacraft;
using acoustics::test::FakeVoxelGrid;
using audio::PlayedVoice;
using audio::VoiceAcoustics;
using game::AcousticsController;

const glm::vec3 kListener{0.5f, 0.5f, 0.5f};

class AcousticsControllerTest : public ::testing::Test {
protected:
    // Stands in for an AudioEngine play call: records the acoustics and returns a fixed voice.
    [[nodiscard]] PlayedVoice play(const glm::vec3& at, PlayedVoice voice) {
        return m_controller.playAt(at, m_grid, [&](const VoiceAcoustics& a) {
            m_started.push_back(a);
            return voice;
        });
    }

    FakeVoxelGrid m_grid;
    audio::AudioEngine m_audio;
    AcousticsController m_controller{m_audio};
    std::vector<VoiceAcoustics> m_started;
};

TEST_F(AcousticsControllerTest, InitialAcousticsMatchTheOcclusionModel) {
    m_grid.fillBox({3, -2, -2}, {4, 2, 2}); // Two blocks between listener and source
    m_controller.update(0.0, kListener, m_grid);
    const glm::vec3 source{6.5f, 0.5f, 0.5f};
    (void)play(source, {7u, 0.3f});
    ASSERT_EQ(m_started.size(), 1u);
    const acoustics::OcclusionTarget expected =
        acoustics::occlusionTargetFor(acoustics::measureOcclusion(m_grid, kListener, source));
    EXPECT_FLOAT_EQ(m_started[0].occlusionGain, expected.gain);
    EXPECT_FLOAT_EQ(m_started[0].lpfCutoffHz, expected.cutoffHz);
    EXPECT_LT(m_started[0].lpfCutoffHz, 10000.0f);
    EXPECT_FLOAT_EQ(m_started[0].reverbSend, acoustics::VoiceAcousticFilter::DEFAULT_REVERB_SEND);
}

TEST_F(AcousticsControllerTest, UsesTheListenerOfTheLatestUpdate) {
    m_grid.fillBox({3, -2, -2}, {4, 2, 2});
    m_controller.update(0.0, glm::vec3(10.5f, 0.5f, 0.5f), m_grid); // Same side as the source
    EXPECT_EQ(m_controller.listener(), glm::vec3(10.5f, 0.5f, 0.5f));
    (void)play({6.5f, 0.5f, 0.5f}, {7u, 0.3f});
    ASSERT_EQ(m_started.size(), 1u);
    EXPECT_FLOAT_EQ(m_started[0].occlusionGain, 1.0f);
}

TEST_F(AcousticsControllerTest, SoundsBeyondTheAudibleRangeAreNeverStarted) {
    m_controller.update(0.0, kListener, m_grid);
    const PlayedVoice voice = play({kListener.x + acoustics::MAX_AUDIBLE_RANGE + 2.0f, 0.5f, 0.5f}, {7u, 0.3f});
    EXPECT_FALSE(voice.valid());
    EXPECT_TRUE(m_started.empty());
    EXPECT_EQ(m_controller.trackedSourceCount(), 0u);
}

TEST_F(AcousticsControllerTest, TracksASourceUntilItsSoundHasPlayedOut) {
    m_controller.update(1.0, kListener, m_grid);
    EXPECT_TRUE(play({4.5f, 0.5f, 0.5f}, {7u, 0.3f}).valid());
    EXPECT_EQ(m_controller.trackedSourceCount(), 1u);

    const double end = 1.0 + 0.3 + AcousticsController::TRACKING_GRACE_SECONDS;
    m_controller.update(end - 0.01, kListener, m_grid);
    EXPECT_EQ(m_controller.trackedSourceCount(), 1u);
    m_controller.update(end + 0.01, kListener, m_grid);
    EXPECT_EQ(m_controller.trackedSourceCount(), 0u);
}

TEST_F(AcousticsControllerTest, VoicesTheEngineDroppedAreNotTracked) {
    m_controller.update(0.0, kListener, m_grid);
    EXPECT_FALSE(play({4.5f, 0.5f, 0.5f}, PlayedVoice{}).valid());
    EXPECT_EQ(m_started.size(), 1u);
    EXPECT_EQ(m_controller.trackedSourceCount(), 0u);
}

TEST_F(AcousticsControllerTest, MoreSoundsThanSchedulerSlotsStillPlay) {
    m_controller.update(0.0, kListener, m_grid);
    for (uint32_t i = 0; i < AcousticsController::MAX_TRACKED + 5; ++i) {
        EXPECT_TRUE(play({4.5f, 0.5f, 0.5f}, {100u + i, 2.0f}).valid());
    }
    EXPECT_EQ(m_started.size(), AcousticsController::MAX_TRACKED + 5);
    EXPECT_EQ(m_controller.trackedSourceCount(), AcousticsController::MAX_TRACKED);

    // Freed slots are reused once the first wave has played out.
    m_controller.update(3.0, kListener, m_grid);
    EXPECT_EQ(m_controller.trackedSourceCount(), 0u);
    EXPECT_TRUE(play({4.5f, 0.5f, 0.5f}, {500u, 1.0f}).valid());
    EXPECT_EQ(m_controller.trackedSourceCount(), 1u);
}

TEST_F(AcousticsControllerTest, ResetForgetsSourcesAndReprobesTheSurroundings) {
    m_grid.hollowBox({-6, -6, -6}, {6, 6, 6});
    m_controller.update(0.0, kListener, m_grid);
    (void)play({2.5f, 0.5f, 0.5f}, {7u, 1.0f});
    m_controller.reset();
    EXPECT_EQ(m_controller.trackedSourceCount(), 0u);

    // A probe snaps the shared reverb to the enclosed room's decay at the next callback.
    m_controller.update(0.05, kListener, m_grid); // Not due by the clock, but reset forces it
    std::vector<float> out(64);
    ASSERT_TRUE(m_audio.renderFrames(out.data(), 32));
    const acoustics::EnclosureDetector detector;
    const acoustics::ReverbParams room = detector.analyze(kListener, m_grid);
    EXPECT_NEAR(m_audio.acousticBank().bus().reverb().feedbackGain(), acoustics::feedbackForDecay(room.decaySeconds), 1e-4f);
    EXPECT_EQ(m_controller.droppedCommands(), 0u);
}

TEST_F(AcousticsControllerTest, OcclusionUpdatesArePostedToThePlayingVoice) {
    m_controller.update(0.0, kListener, m_grid);
    const PlayedVoice voice = m_controller.playAt({6.5f, 0.5f, 0.5f}, m_grid, [&](const VoiceAcoustics& a) {
        return m_audio.playSound(audio::SoundEffect::LavaFizz, 0.5f, 1.0f, a);
    });
    ASSERT_TRUE(voice.valid());
    std::vector<float> out(2 * 512);
    ASSERT_TRUE(m_audio.renderFrames(out.data(), 512));
    EXPECT_FLOAT_EQ(m_audio.acousticBank().voice(*m_audio.acousticBank().slotOf(voice.handle)).currentGain(), 1.0f);

    m_grid.fillBox({3, -2, -2}, {4, 2, 2});
    m_controller.update(0.1, kListener, m_grid);
    for (int i = 0; i < 8; ++i) ASSERT_TRUE(m_audio.renderFrames(out.data(), 512)); // > 5 smoothing tau
    const float expected = acoustics::occlusionTargetFor(acoustics::measureOcclusion(m_grid, kListener, {6.5f, 0.5f, 0.5f})).gain;
    EXPECT_NEAR(m_audio.acousticBank().voice(*m_audio.acousticBank().slotOf(voice.handle)).currentGain(), expected, 0.02f);
}

} // namespace
