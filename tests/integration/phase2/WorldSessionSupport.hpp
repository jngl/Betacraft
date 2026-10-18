#pragma once
// Shared fixture for the GL-free game::WorldSession tests (spec #9): the counterpart of
// p2gameplay::GameplayWorld one level up, with the world, the player and the per-world state
// owned by a real session opened on a temporary folder.

#include "tests/test_helpers.hpp"

#include "P2WorldgenSupport.hpp"
#include "ecs/Components.hpp"
#include "game/WorldSession.hpp"
#include "physics/Player.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace betacraft::test::worldsession {

using world::BlockId;

// High above the generated terrain of the test seed, on a glass floor laid by the fixture.
constexpr int32_t kFloorY = 119;
constexpr int32_t kY = 120;

// Options of a deterministic session on `folder`: the worldgen test seed, chunks generated in
// place (no async generation, so no sleeps).
[[nodiscard]] inline game::WorldSession::OpenOptions openOptions(const std::filesystem::path& folder) {
    game::WorldSession::OpenOptions options{};
    options.folder = folder;
    options.seed = p2worldgen::kSeed;
    options.features.asyncWorldGen = false;
    return options;
}

class WorldSessionSupport : public ::testing::Test {
protected:
    void SetUp() override {
        world::BlockRegistry::initialize();
        m_session = game::WorldSession::open(openOptions(m_dir.path()));
        ASSERT_NE(m_session, nullptr);
        ASSERT_TRUE(m_session->awaitingGameMode());
        m_session->startNewWorld(m_mode);
        p2worldgen::loadAll(world(), p2worldgen::squareRegion({0, 0}, 1));
        for (int32_t z = -6; z <= 14; ++z) {
            for (int32_t x = -6; x <= 14; ++x) {
                (void)world().setBlock(x, kFloorY, z, BlockId::Glass);
            }
        }
        for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
            player().setInventorySlot(i, {});
        }
        player().setSelectedSlot(0);
        player().setRotation(0.0f, 0.0f); // Looking along +X
        placePlayer({2.5f, static_cast<float>(kY), 2.5f});
    }

    [[nodiscard]] world::World& world() { return m_session->world(); }
    [[nodiscard]] physics::Player& player() { return m_session->player(); }

    // Stands the player on the floor and lets physics settle so isOnGround() holds.
    void placePlayer(const glm::vec3& feet) {
        player().setPosition(feet);
        for (int i = 0; i < 30; ++i) {
            (void)player().fixedUpdate(1.0f / 60.0f, {}, world());
        }
        ASSERT_TRUE(player().isOnGround());
    }

    [[nodiscard]] size_t itemEntityCount() const {
        return m_session->simulation().entities().count<ecs::ItemDropComp>();
    }

    core::GameMode m_mode{core::GameMode::Survival};
    TempDir m_dir{"betacraft_world_session_"};
    std::unique_ptr<game::WorldSession> m_session;
};

} // namespace betacraft::test::worldsession
