#pragma once
// Shared fixture for the Phase 2 gameplay integration tests (P2.1 ECS entities, P2.5 tools,
// durability and combat, P2.6 crafting) through the GL-free game::EntitySimulation.

#include "test_support/GLTestContext.hpp"

#include "GlassFloorSupport.hpp"
#include "P2WorldgenSupport.hpp"
#include "ecs/Components.hpp"
#include "game/EntitySimulation.hpp"
#include "physics/Player.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace betacraft::test::p2gameplay {

using voxel::BlockId;

// High above the generated terrain of the test seed, on a glass floor laid by the fixture.
using glassfloor::kFloorY;
using glassfloor::kY;

class GameplayWorld : public ::testing::Test {
protected:
    void SetUp() override {
        voxel::BlockRegistry::initialize();
        m_world = std::make_unique<world::World>(p2worldgen::kSeed, m_dir.path());
        glassfloor::layGlassFloor(*m_world);
        glassfloor::resetPlayer(m_player);
        placePlayer({2.5f, static_cast<float>(kY), 2.5f});
    }

    // Stands the player on the floor and lets physics settle so isOnGround() holds.
    void placePlayer(const glm::vec3& feet) { glassfloor::placePlayer(m_player, *m_world, feet); }

    void hold(physics::InventorySlot slot) { m_player.setHeldItem(slot); }

    [[nodiscard]] static physics::RaycastHit targetAt(const core::BlockPos& pos, BlockId id, float distance = 2.5f) {
        return glassfloor::targetAt(pos, id, distance);
    }

    game::GameTickEvents tickIdle() { return m_session.tick(*m_world, m_player, {}); }

    void tickIdle(int32_t n) {
        for (int32_t i = 0; i < n; ++i) (void)tickIdle();
    }

    using MineResult = glassfloor::MineResult;

    // Clicks and holds on `pos` until it breaks or `maxTicks` pass, then releases.
    MineResult mine(const core::BlockPos& pos, int32_t maxTicks = 2000) {
        return glassfloor::mine(*m_world, pos, maxTicks, [&](const game::PlayerActions& actions) {
            return m_session.tick(*m_world, m_player, actions);
        });
    }

    [[nodiscard]] size_t itemEntityCount() const { return m_session.entities().count<ecs::ItemDropComp>(); }

    TempDir m_dir{"betacraft_p2_gameplay_"};
    std::unique_ptr<world::World> m_world;
    physics::Player m_player;
    game::EntitySimulation m_session{12345ULL};
};

} // namespace betacraft::test::p2gameplay
