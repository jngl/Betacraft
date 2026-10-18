// Phase 2 integration, P4.2 acoustics in the game loop: Application plays world sounds at their
// voxel through the AcousticsController. The fizz of a lava/water contact is the one sound that
// comes from the simulation rather than the player, so it is checked from the real fluid events.
#include "tests/test_helpers.hpp"

#include "P2WorldgenSupport.hpp"
#include "audio/AudioEngine.hpp"
#include "game/AcousticsController.hpp"
#include "game/FluidEffects.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>
#include <vector>

using namespace betacraft;
using fluids::FluidEvent;
using fluids::FluidEventKind;
using world::BlockId;

namespace {

TEST(P2AcousticsGame, LoudestFizzSoundComesFromTheNearestContact) {
    const glm::vec3 listener{0.5f, 64.5f, 0.5f};
    std::vector<FluidEvent> events{{FluidEventKind::BlockWashedAway, {1, 64, 0}, BlockId::Torch}};
    EXPECT_FALSE(game::loudestFizzSound(events, listener).has_value());

    events.push_back({FluidEventKind::Fizz, {9, 64, 0}, BlockId::Cobblestone});
    events.push_back({FluidEventKind::Fizz, {4, 64, 0}, BlockId::Obsidian});
    events.push_back({FluidEventKind::Fizz, {40, 64, 0}, BlockId::Stone});
    const std::optional<game::FizzSound> fizz = game::loudestFizzSound(events, listener);
    ASSERT_TRUE(fizz.has_value());
    EXPECT_EQ(fizz->position, glm::vec3(4.5f, 64.5f, 0.5f));
    EXPECT_NEAR(fizz->gain, 0.75f, 1e-5f);
    EXPECT_FLOAT_EQ(fizz->gain, game::loudestFizz(events, listener)); // One fall-off for both
}

class P2AcousticsGameWorld : public ::testing::Test {
protected:
    static constexpr int32_t kFloorY = 119;
    static constexpr int32_t kY = 120;

    void SetUp() override {
        world::BlockRegistry::initialize();
        m_world = std::make_unique<world::World>(test::p2worldgen::kSeed, m_dir.path());
        test::p2worldgen::loadAll(*m_world, test::p2worldgen::squareRegion({0, 0}, 1));
        for (int32_t z = -2; z <= 18; ++z)
            for (int32_t x = -2; x <= 18; ++x) (void)m_world->setBlock(x, kFloorY, z, BlockId::Glass);
        (void)m_world->takeFluidEvents();
    }

    // Lava poured next to a water source fizzes on the next ticks, as in Beta.
    [[nodiscard]] std::optional<game::FizzSound> fizzAt(int32_t x, int32_t z, const glm::vec3& listener) {
        (void)m_world->setBlock(x, kY, z, BlockId::Water);
        (void)m_world->setBlock(x + 1, kY, z, BlockId::Lava);
        for (int32_t tick = 0; tick < 40; ++tick) {
            (void)m_world->tick();
            if (std::optional<game::FizzSound> fizz = game::loudestFizzSound(m_world->takeFluidEvents(), listener)) return fizz;
        }
        return std::nullopt;
    }

    [[nodiscard]] double playedRms(const glm::vec3& listener, const game::FizzSound& fizz) {
        audio::AudioEngine engine;
        game::AcousticsController acoustics(engine);
        acoustics.update(0.0, listener, *m_world);
        const audio::PlayedVoice voice = acoustics.playAt(fizz.position, *m_world, [&](const audio::VoiceAcoustics& a) {
            return engine.playSound(audio::SoundEffect::LavaFizz, 0.5f * fizz.gain, 1.0f, a);
        });
        EXPECT_TRUE(voice.valid());
        std::vector<float> out(2 * 8192);
        EXPECT_TRUE(engine.renderFrames(out.data(), 8192));
        double sum = 0.0;
        for (const float s : out) sum += static_cast<double>(s) * s;
        return std::sqrt(sum / static_cast<double>(out.size()));
    }

    test::TempDir m_dir;
    std::unique_ptr<world::World> m_world;
};

TEST_F(P2AcousticsGameWorld, FizzBehindAWallIsMuffledComparedWithAnOpenOne) {
    const glm::vec3 listener{8.5f, kY + 1.6f, 8.5f};
    // Two contacts at the same distance, one behind a 3-thick stone wall.
    for (int32_t y = kY; y <= kY + 6; ++y)
        for (int32_t z = 4; z <= 13; ++z)
            for (int32_t x = 11; x <= 13; ++x) (void)m_world->setBlock(x, y, z, BlockId::Stone);
    const std::optional<game::FizzSound> behind = fizzAt(15, 8, listener);
    const std::optional<game::FizzSound> open = fizzAt(0, 8, listener);
    ASSERT_TRUE(behind.has_value());
    ASSERT_TRUE(open.has_value());
    ASSERT_GT(behind->position.x, 14.0f); // Each fizz comes from its own contact
    ASSERT_LT(open->position.x, 3.0f);

    const double behindRms = playedRms(listener, *behind) / behind->gain;
    const double openRms = playedRms(listener, *open) / open->gain;
    EXPECT_LT(behindRms, 0.6 * openRms);
}

} // namespace
