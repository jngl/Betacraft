// Phase 2 integration, P4.2 acoustics in AudioEngine: the game-thread AcousticsController
// ray-marches the real World (occlusion at start and at 10 Hz, enclosure probes), posts through
// AudioEngine's command queue, and the mixer routes every voice through the AcousticVoiceBank
// (occlusion LPF + gain, shared FDN aux bus). AudioEngine::renderFrames runs that mixing path
// without an audio device, so these tests hear exactly what the callback would play.
#include "test_support/GLTestContext.hpp"

#include "tests/integration/support/WorldgenSupport.hpp"
#include "audio/AudioEngine.hpp"
#include "game/AcousticsController.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace betacraft;
using voxel::BlockId;

namespace {

constexpr uint32_t kRate = audio::AudioEngine::SAMPLE_RATE;
// Well above the generated terrain and trees of the test seed: open air in every direction.
constexpr float kSkyY = 121.5f;

[[nodiscard]] uint32_t frames(double seconds) { return static_cast<uint32_t>(seconds * kRate); }

[[nodiscard]] double rms(const std::vector<float>& stereo, uint32_t first, uint32_t last) {
    double sum = 0.0;
    for (uint32_t f = first; f < last; ++f) {
        sum += static_cast<double>(stereo[2 * f]) * stereo[2 * f] + static_cast<double>(stereo[2 * f + 1]) * stereo[2 * f + 1];
    }
    return last > first ? std::sqrt(sum / (2.0 * (last - first))) : 0.0;
}

// Renders in 512-frame callbacks, like a typical miniaudio period, appending to `out`.
void render(audio::AudioEngine& engine, std::vector<float>& out, uint32_t frameCount) {
    constexpr uint32_t kPeriod = 512;
    const size_t start = out.size();
    out.resize(start + 2 * static_cast<size_t>(frameCount));
    for (uint32_t done = 0; done < frameCount;) {
        const uint32_t n = std::min(kPeriod, frameCount - done);
        ASSERT_TRUE(engine.renderFrames(out.data() + start + 2 * static_cast<size_t>(done), n));
        done += n;
    }
}

void fillBox(world::World& w, core::BlockPos lo, core::BlockPos hi, BlockId id, bool hollow) {
    for (int32_t y = lo.y; y <= hi.y; ++y)
        for (int32_t z = lo.z; z <= hi.z; ++z)
            for (int32_t x = lo.x; x <= hi.x; ++x) {
                const bool shell = x == lo.x || x == hi.x || y == lo.y || y == hi.y || z == lo.z || z == hi.z;
                if (!hollow || shell) (void)w.setBlock(x, y, z, id);
            }
}

class P2AcousticsMixing : public ::testing::Test {
protected:
    void SetUp() override {
        voxel::BlockRegistry::initialize();
        m_world = std::make_unique<world::World>(test::p2worldgen::kSeed, m_dir.path());
        test::p2worldgen::loadAll(*m_world, test::p2worldgen::squareRegion({0, 0}, 3));
    }

    // One voice of `effect` at `source` heard from `listener` through the controller, as Application
    // plays world sounds. Returns the rendered stereo signal.
    [[nodiscard]] std::vector<float> playAndRender(const glm::vec3& listener, const glm::vec3& source,
                                                   audio::SoundEffect effect, double seconds) {
        audio::AudioEngine engine;
        game::AcousticsController acoustics(engine);
        acoustics.update(0.0, listener, *m_world);
        const audio::PlayedVoice voice = acoustics.playAt(source, *m_world, [&](const audio::VoiceAcoustics& a) {
            return engine.playSound(effect, 0.5f, 1.0f, a);
        });
        EXPECT_TRUE(voice.valid());
        std::vector<float> out;
        render(engine, out, frames(seconds));
        return out;
    }

    test::TempDir m_dir;
    std::unique_ptr<world::World> m_world;
};

TEST_F(P2AcousticsMixing, SoundBehindAStoneWallIsAttenuatedInTheMix) {
    const glm::vec3 listener{8.5f, kSkyY, 8.5f};
    // A 3-thick stone wall east of the listener; the west side stays open.
    fillBox(*m_world, {11, 112, -2}, {13, 131, 19}, BlockId::Stone, false);
    const glm::vec3 behindWall{16.5f, kSkyY, 8.5f};
    const glm::vec3 inTheOpen{0.5f, kSkyY, 8.5f};

    const std::vector<float> occluded = playAndRender(listener, behindWall, audio::SoundEffect::BreakStone, 0.3);
    const std::vector<float> open = playAndRender(listener, inTheOpen, audio::SoundEffect::BreakStone, 0.3);

    const double occludedRms = rms(occluded, 0, frames(0.3));
    const double openRms = rms(open, 0, frames(0.3));
    ASSERT_GT(openRms, 1.0e-3);
    // 3 solid blocks: gain 0.8^3 ~ 0.51 and a ~5.5 kHz low-pass on a noisy break sound.
    EXPECT_LT(occludedRms, 0.6 * openRms);
    EXPECT_GT(occludedRms, 0.05 * openRms); // Muffled, not silenced
}

TEST_F(P2AcousticsMixing, WallBuiltWhileASoundPlaysMufflesItAtTheNextOcclusionUpdate) {
    const glm::vec3 listener{8.5f, kSkyY, 8.5f};
    const glm::vec3 east{16.5f, kSkyY, 8.5f};
    const glm::vec3 west{0.5f, kSkyY, 8.5f};

    audio::AudioEngine eastEngine;
    audio::AudioEngine westEngine;
    game::AcousticsController eastAcoustics(eastEngine);
    game::AcousticsController westAcoustics(westEngine);
    std::vector<float> eastOut;
    std::vector<float> westOut;

    for (auto* a : {&eastAcoustics, &westAcoustics}) a->update(0.0, listener, *m_world);
    EXPECT_TRUE(eastAcoustics.playAt(east, *m_world, [&](const audio::VoiceAcoustics& a) {
        return eastEngine.playSound(audio::SoundEffect::LavaFizz, 0.5f, 1.0f, a);
    }).valid());
    EXPECT_TRUE(westAcoustics.playAt(west, *m_world, [&](const audio::VoiceAcoustics& a) {
        return westEngine.playSound(audio::SoundEffect::LavaFizz, 0.5f, 1.0f, a);
    }).valid());
    EXPECT_EQ(eastAcoustics.trackedSourceCount(), 1u);

    render(eastEngine, eastOut, frames(0.1));
    render(westEngine, westOut, frames(0.1));
    // Mirror-image positions in open air start identically.
    EXPECT_NEAR(rms(eastOut, 0, frames(0.1)), rms(westOut, 0, frames(0.1)), 1.0e-6);

    fillBox(*m_world, {11, 112, -2}, {13, 131, 19}, BlockId::Stone, false);
    for (auto* a : {&eastAcoustics, &westAcoustics}) a->update(0.1, listener, *m_world);
    render(eastEngine, eastOut, frames(0.3));
    render(westEngine, westOut, frames(0.3));

    // After the 20 ms smoothing ramp the east voice runs through the new occlusion target.
    const uint32_t from = frames(0.15);
    const uint32_t to = frames(0.4);
    EXPECT_LT(rms(eastOut, from, to), 0.6 * rms(westOut, from, to));
}

TEST_F(P2AcousticsMixing, EnclosedCaveSendsFarMoreReverbThanOpenAir) {
    // A sealed stone hall (21 x 11 x 21 inside) high in the sky, and an open-air spot more than
    // the 24-block probe range away from it.
    const core::BlockPos lo{-6, 115, -6};
    const core::BlockPos hi{16, 127, 16};
    fillBox(*m_world, lo, hi, BlockId::Stone, true);
    const glm::vec3 caveListener{5.5f, 121.5f, 5.5f};
    const glm::vec3 openListener{5.5f, kSkyY, 48.5f};
    const glm::vec3 offset{1.0f, 0.0f, 1.0f};

    const std::vector<float> cave = playAndRender(caveListener, caveListener + offset, audio::SoundEffect::StepStone, 1.0);
    const std::vector<float> open = playAndRender(openListener, openListener + offset, audio::SoundEffect::StepStone, 1.0);

    // The dry step lasts 0.11 s: what remains from 0.3 s on is the shared FDN's tail.
    const double caveTail = rms(cave, frames(0.3), frames(1.0));
    const double openTail = rms(open, frames(0.3), frames(1.0));
    EXPECT_GT(caveTail, 1.0e-4);
    EXPECT_GT(caveTail, 20.0 * openTail);
    // The dry onset is the same in both places (no occlusion, same voice gain).
    EXPECT_NEAR(rms(cave, 0, frames(0.05)), rms(open, 0, frames(0.05)), 0.25 * rms(open, 0, frames(0.05)));
}

TEST_F(P2AcousticsMixing, SoundsBeyondTheAudibleRangeAreCulledBeforeTheyReachTheMixer) {
    audio::AudioEngine engine;
    game::AcousticsController acoustics(engine);
    const glm::vec3 listener{8.5f, kSkyY, 8.5f};
    acoustics.update(0.0, listener, *m_world);
    bool played = false;
    const audio::PlayedVoice voice = acoustics.playAt(listener + glm::vec3(60.0f, 0.0f, 0.0f), *m_world,
                                                      [&](const audio::VoiceAcoustics& a) {
                                                          played = true;
                                                          return engine.playSound(audio::SoundEffect::BreakStone, 0.5f, 1.0f, a);
                                                      });
    EXPECT_FALSE(played);
    EXPECT_FALSE(voice.valid());
    EXPECT_EQ(acoustics.trackedSourceCount(), 0u);
    std::vector<float> out;
    render(engine, out, frames(0.05));
    EXPECT_EQ(rms(out, 0, frames(0.05)), 0.0);
}

} // namespace
