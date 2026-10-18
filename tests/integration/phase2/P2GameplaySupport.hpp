#pragma once
// Shared fixture for the Phase 2 gameplay integration tests (P2.1 ECS entities, P2.5 tools,
// durability and combat, P2.6 crafting) through the GL-free game::GameSession.

#include "tests/test_helpers.hpp"

#include "P2WorldgenSupport.hpp"
#include "ecs/Components.hpp"
#include "game/GameSession.hpp"
#include "physics/Player.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace betacraft::test::p2gameplay {

using world::BlockId;

// High above the generated terrain of the test seed, on a glass floor laid by the fixture.
constexpr int32_t kFloorY = 119;
constexpr int32_t kY = 120;

class GameplayWorld : public ::testing::Test {
protected:
    void SetUp() override {
        world::BlockRegistry::initialize();
        m_world = std::make_unique<world::World>(p2worldgen::kSeed, m_dir.path());
        p2worldgen::loadAll(*m_world, p2worldgen::squareRegion({0, 0}, 1));
        for (int32_t z = -6; z <= 14; ++z) {
            for (int32_t x = -6; x <= 14; ++x) {
                (void)m_world->setBlock(x, kFloorY, z, BlockId::Glass);
            }
        }
        for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
            m_player.setInventorySlot(i, {});
        }
        m_player.setSelectedSlot(0);
        m_player.setRotation(0.0f, 0.0f); // Looking along +X
        placePlayer({2.5f, static_cast<float>(kY), 2.5f});
    }

    // Stands the player on the floor and lets physics settle so isOnGround() holds.
    void placePlayer(const glm::vec3& feet) {
        m_player.setPosition(feet);
        for (int i = 0; i < 30; ++i) {
            (void)m_player.fixedUpdate(1.0f / 60.0f, {}, *m_world);
        }
        ASSERT_TRUE(m_player.isOnGround());
    }

    void hold(physics::InventorySlot slot) { m_player.setHeldItem(slot); }

    [[nodiscard]] static physics::RaycastHit targetAt(const core::BlockPos& pos, BlockId id, float distance = 2.5f) {
        physics::RaycastHit hit{};
        hit.hit = true;
        hit.blockPos = pos;
        hit.adjacentPos = {pos.x - 1, pos.y, pos.z};
        hit.face = core::FaceDir::NegX;
        hit.blockId = id;
        hit.distance = distance;
        return hit;
    }

    game::GameTickEvents tickIdle() { return m_session.tick(*m_world, m_player, {}); }

    void tickIdle(int32_t n) {
        for (int32_t i = 0; i < n; ++i) (void)tickIdle();
    }

    struct MineResult {
        int32_t ticks{0}; // Ticks the button was held, including the click tick
        std::optional<game::BlockBreakEvent> broken;
        gameplay::WearResult wear{gameplay::WearResult::Unchanged};
    };

    // Clicks and holds on `pos` until it breaks or `maxTicks` pass, then releases.
    MineResult mine(const core::BlockPos& pos, int32_t maxTicks = 2000) {
        MineResult r{};
        const physics::RaycastHit target = targetAt(pos, m_world->getBlock(pos));
        for (int32_t t = 0; t < maxTicks && !r.broken; ++t) {
            const game::GameTickEvents ev =
                m_session.tick(*m_world, m_player, {.attackPressed = t == 0, .attackHeld = true, .target = target});
            ++r.ticks;
            r.broken = ev.blockBroken;
            r.wear = ev.heldWear;
        }
        (void)tickIdle(); // Button released
        return r;
    }

    [[nodiscard]] size_t itemEntityCount() const { return m_session.entities().count<ecs::ItemDropComp>(); }

    TempDir m_dir{"betacraft_p2_gameplay_"};
    std::unique_ptr<world::World> m_world;
    physics::Player m_player;
    game::GameSession m_session{12345ULL};
};

} // namespace betacraft::test::p2gameplay
