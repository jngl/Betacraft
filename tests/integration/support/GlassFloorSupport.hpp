#pragma once
// Helpers shared by the gameplay fixtures one level apart: gameplay_support::GameplayWorld (a World
// and an EntitySimulation) and worldsession::WorldSessionSupport (a real WorldSession). Both
// stand the player on a glass floor high above the generated terrain of the test seed.

#include "WorldgenSupport.hpp"
#include "game/EntitySimulation.hpp"
#include "physics/Player.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

namespace betacraft::test::glassfloor {

constexpr int32_t kFloorY = 119;
constexpr int32_t kY = 120;

// Loads the 3x3 chunks around the origin and lays the glass floor over x, z in [-6, 14].
inline void layGlassFloor(world::World& world) {
    worldgen_support::loadAll(world, worldgen_support::squareRegion({0, 0}, 1));
    for (int32_t z = -6; z <= 14; ++z) {
        for (int32_t x = -6; x <= 14; ++x) {
            (void)world.setBlock(x, kFloorY, z, voxel::BlockId::Glass);
        }
    }
}

// Empty inventory, slot 0 selected, looking along +X.
inline void resetPlayer(physics::Player& player) {
    for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
        player.setInventorySlot(i, {});
    }
    player.setSelectedSlot(0);
    player.setRotation(0.0f, 0.0f);
}

// Stands the player on the floor and lets physics settle so isOnGround() holds.
inline void placePlayer(physics::Player& player, world::World& world, const glm::vec3& feet) {
    player.setPosition(feet);
    for (int i = 0; i < 30; ++i) {
        (void)player.fixedUpdate(1.0f / 60.0f, {}, world);
    }
    ASSERT_TRUE(player.isOnGround());
}

// A hit on the -X face of `pos`, as the crosshair of a player west of it reports it.
[[nodiscard]] inline physics::RaycastHit targetAt(const core::BlockPos& pos, voxel::BlockId id,
                                                  float distance = 2.5f) {
    physics::RaycastHit hit{};
    hit.hit = true;
    hit.blockPos = pos;
    hit.adjacentPos = {pos.x - 1, pos.y, pos.z};
    hit.face = core::FaceDir::NegX;
    hit.blockId = id;
    hit.distance = distance;
    return hit;
}

struct MineResult {
    int32_t ticks{0}; // Ticks the button was held, including the click tick
    std::optional<game::BlockBreakEvent> broken;
    gameplay::WearResult wear{gameplay::WearResult::Unchanged};
};

// Clicks and holds on `pos` until it breaks or `maxTicks` pass, then releases. `tick` runs one
// game tick with the given actions and returns its game::GameTickEvents.
template <typename TickFn>
MineResult mine(const world::World& world, const core::BlockPos& pos, int32_t maxTicks, TickFn&& tick) {
    MineResult r{};
    const physics::RaycastHit target = targetAt(pos, world.getBlock(pos));
    for (int32_t t = 0; t < maxTicks && !r.broken; ++t) {
        const game::GameTickEvents ev = tick(game::PlayerActions{.attackPressed = t == 0, .attackHeld = true, .target = target});
        ++r.ticks;
        r.broken = ev.blockBroken;
        r.wear = ev.heldWear;
    }
    (void)tick(game::PlayerActions{}); // Button released
    return r;
}

} // namespace betacraft::test::glassfloor
