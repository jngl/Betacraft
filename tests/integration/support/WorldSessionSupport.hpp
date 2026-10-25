#pragma once
// Shared fixture for the GL-free game::WorldSession tests (spec #9): the counterpart of
// gameplay_support::GameplayWorld one level up, with the world, the player and the per-world state
// owned by a real session opened on a temporary folder.

#include "test_support/GLTestContext.hpp"

#include "GlassFloorSupport.hpp"
#include "WorldgenSupport.hpp"
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

using voxel::BlockId;

// High above the generated terrain of the test seed, on a glass floor laid by the fixture.
using glassfloor::kFloorY;
using glassfloor::kY;

// Options of a deterministic session on `folder`: the worldgen test seed, chunks generated in
// place (no async generation, so no sleeps).
[[nodiscard]] inline game::WorldSession::OpenOptions openOptions(const std::filesystem::path& folder) {
    game::WorldSession::OpenOptions options{};
    options.folder = folder;
    options.seed = worldgen_support::kSeed;
    options.features.asyncWorldGen = false;
    return options;
}

class WorldSessionSupport : public ::testing::Test {
protected:
    void SetUp() override {
        voxel::BlockRegistry::initialize();
        m_session = game::WorldSession::open(openOptions(m_dir.path()));
        ASSERT_NE(m_session, nullptr);
        ASSERT_TRUE(m_session->awaitingGameMode());
        m_session->startNewWorld(m_mode);
        glassfloor::layGlassFloor(world());
        glassfloor::resetPlayer(player());
        placePlayer({2.5f, static_cast<float>(kY), 2.5f});
    }

    [[nodiscard]] world::World& world() { return m_session->world(); }
    [[nodiscard]] physics::Player& player() { return m_session->player(); }
    [[nodiscard]] const physics::Player& player() const { return m_session->player(); }

    // Stands the player on the floor and lets physics settle so isOnGround() holds.
    void placePlayer(const glm::vec3& feet) { glassfloor::placePlayer(player(), world(), feet); }

    void hold(physics::InventorySlot slot) { player().setHeldItem(slot); }

    // A hit on the -X face of `pos`, as the crosshair of a player west of it reports it.
    [[nodiscard]] static physics::RaycastHit targetAt(const core::BlockPos& pos, BlockId id, float distance = 2.5f) {
        return glassfloor::targetAt(pos, id, distance);
    }

    // One game tick of the session.
    game::WorldTickEvents tick(const game::PlayerActions& actions) { return m_session->tick(actions); }
    game::WorldTickEvents tickIdle() { return tick({}); }
    // `n` idle game ticks; the events of the last one.
    game::WorldTickEvents tickIdle(int32_t n) {
        game::WorldTickEvents last{};
        for (int32_t i = 0; i < n; ++i) {
            last = tickIdle();
        }
        return last;
    }

    using MineResult = glassfloor::MineResult;

    // Clicks and holds on `pos` until it breaks or `maxTicks` pass, then releases.
    MineResult mine(const core::BlockPos& pos, int32_t maxTicks = 2000) {
        return glassfloor::mine(world(), pos, maxTicks,
                                [&](const game::PlayerActions& actions) { return tick(actions).game; });
    }

    [[nodiscard]] size_t itemEntityCount() const {
        return m_session->simulation().entities().count<ecs::ItemDropComp>();
    }

    core::GameMode m_mode{core::GameMode::Survival};
    TempDir m_dir{"betacraft_world_session_"};
    std::unique_ptr<game::WorldSession> m_session;
};

} // namespace betacraft::test::worldsession
