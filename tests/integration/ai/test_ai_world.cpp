// Integration: through the GL-free EntitySimulation on a real World, a zombie
// chasing the player goes around a 6-block wall and climbs a one-block step in under 200
// ticks; straight-line steering (--no-pathfinding) stays stuck against the wall.
#include "tests/integration/support/GameplaySupport.hpp"

#include "ai/WorldNavQuery.hpp"
#include "ecs/Systems.hpp"
#include "voxel/BlockShapes.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace betacraft;
using namespace betacraft::test::gameplay_support;

namespace {

const glm::vec3 kPlayerFeet{10.5f, static_cast<float>(kY + 1), 2.5f};

class AiWorld : public GameplayWorld {
protected:
    void SetUp() override {
        GameplayWorld::SetUp();
        fill({8, kY, 0}, {12, kY, 4}, BlockId::Stone);       // the step the player stands on
        fill({7, kY, -1}, {7, kY + 2, 4}, BlockId::Stone);   // a 6-block wall, 3 high
        placePlayer(kPlayerFeet);
        m_session.setNaturalSpawning(false);
    }

    void fill(const core::BlockPos& lo, const core::BlockPos& hi, BlockId id) {
        for (int32_t x = lo.x; x <= hi.x; ++x)
            for (int32_t y = lo.y; y <= hi.y; ++y)
                for (int32_t z = lo.z; z <= hi.z; ++z) ASSERT_TRUE(m_world->setBlock(x, y, z, id));
    }

    // Ticks at midnight with the zombie kept on the player (behind the wall it cannot see
    // them); returns the tick it reached the player on the step, or -1.
    int32_t chaseTicks(int32_t maxTicks) {
        const ecs::EntityId zombie =
            ecs::EntityFactory::spawnMob(m_session.entities(), {4.5f, static_cast<float>(kY), 2.5f},
                                         gameplay::MobKind::Zombie);
        (void)tickIdle(); // creates the player's proxy
        ecs::Registry& reg = m_session.entities();
        for (int32_t t = 0; t < maxTicks; ++t) {
            auto& ai = reg.get<ecs::MobAIComp>(zombie);
            ai.state = ecs::MobAIState::Chase;
            ai.target = m_session.playerProxy();
            (void)m_session.tick(*m_world, m_player, {}, 0.75f);
            (void)m_world->tick();
            const glm::vec3 p = reg.get<ecs::TransformComp>(zombie).pos;
            EXPECT_FALSE(p.x > 6.75f && p.x < 8.25f && p.z < 5.0f && p.y < static_cast<float>(kY) + 2.9f)
                << "inside the wall at tick " << t << ": " << p.x << "," << p.y << "," << p.z;
            if (std::hypot(p.x - kPlayerFeet.x, p.z - kPlayerFeet.z) < 1.3f && p.y > static_cast<float>(kY) + 0.9f) {
                return t;
            }
        }
        return -1;
    }
};

} // namespace

TEST_F(AiWorld, WorldNavQueryReadsBlocks) {
    const ai::WorldNavQuery query(*m_world);
    EXPECT_TRUE(query.isLoaded({7, kY, 0}));
    EXPECT_FALSE(query.isLoaded({7, -1, 0}));
    EXPECT_FALSE(query.isLoaded({7000, kY, 0}));
    EXPECT_EQ(query.classify({7, kY, 0}), ai::NavCell::Solid);
    EXPECT_EQ(query.classify({5, kY, 0}), ai::NavCell::Passable);
    EXPECT_EQ(ai::classifyBlock(BlockId::Lava, 0), ai::NavCell::Hazard);
    EXPECT_EQ(ai::classifyBlock(BlockId::Cactus, 0), ai::NavCell::Hazard);
    EXPECT_EQ(ai::classifyBlock(BlockId::Water, 0), ai::NavCell::Liquid);
    EXPECT_EQ(ai::classifyBlock(BlockId::Ladder, 2), ai::NavCell::Ladder);
    EXPECT_EQ(ai::classifyBlock(BlockId::Fence, 0), ai::NavCell::Fence);
    EXPECT_EQ(ai::classifyBlock(BlockId::Torch, 0), ai::NavCell::Passable);
    EXPECT_EQ(ai::classifyBlock(BlockId::WoodDoor, 0), ai::NavCell::DoorClosed);
    EXPECT_EQ(ai::classifyBlock(BlockId::WoodDoor, voxel::kDoorOpenBit | voxel::kDoorUpperBit),
              ai::NavCell::DoorOpen);
    // The iron door reads the same open bit, which only redstone changes.
    EXPECT_EQ(ai::classifyBlock(BlockId::IronDoor, 0), ai::NavCell::DoorClosed);
    EXPECT_EQ(ai::classifyBlock(BlockId::IronDoor, voxel::kDoorOpenBit), ai::NavCell::DoorOpen);
    EXPECT_EQ(ai::classifyBlock(BlockId::IronDoor, voxel::kDoorOpenBit | voxel::kDoorUpperBit), ai::NavCell::DoorOpen);
    // The other redstone parts never collide: mobs walk over wire and plates.
    for (const BlockId id : {BlockId::RedstoneWire, BlockId::StonePressurePlate, BlockId::WoodPressurePlate,
                             BlockId::Lever, BlockId::StoneButton, BlockId::RedstoneTorchOn}) {
        EXPECT_EQ(ai::classifyBlock(id, 0), ai::NavCell::Passable) << static_cast<int>(id);
    }
}

TEST_F(AiWorld, ZombieGoesAroundWallAndUpStep) {
    ASSERT_TRUE(m_session.pathfinding());
    const int32_t ticks = chaseTicks(200);
    EXPECT_GE(ticks, 0) << "the zombie never reached the player";
    EXPECT_LT(ticks, 200);
    RecordProperty("ticks", ticks);
}

TEST_F(AiWorld, StraightLineStaysStuck) {
    m_session.setPathfinding(false);
    EXPECT_EQ(chaseTicks(200), -1);
    EXPECT_EQ(m_session.entities().count<ecs::MobPathComp>(), 0U);
}

TEST_F(AiWorld, ZombieStopsAtTheEdgeOfALavaMoat) {
    // The player stands on an island ringed by a lava moat 2 wide (sunk in the floor): the
    // zombie cannot reach them and must stay on the near bank instead of walking in.
    fill({7, kY, -1}, {7, kY + 2, 4}, BlockId::Air);
    fill({8, kY, 0}, {12, kY, 4}, BlockId::Air);
    const glm::vec3 island{10.5f, static_cast<float>(kY), 2.5f};
    for (int32_t x = 6; x <= 15; ++x) {
        for (int32_t z = -2; z <= 7; ++z) {
            const bool inner = x >= 8 && x <= 13 && z >= 0 && z <= 5;
            if (!inner) {
                ASSERT_TRUE(m_world->setBlockWithMetadata(x, kFloorY, z, BlockId::Lava, 0));
            }
        }
    }
    placePlayer(island);
    const ecs::EntityId zombie = ecs::EntityFactory::spawnMob(
        m_session.entities(), {3.5f, static_cast<float>(kY), 2.5f}, gameplay::MobKind::Zombie);
    (void)tickIdle();
    ecs::Registry& reg = m_session.entities();
    for (int32_t t = 0; t < 600; ++t) {
        auto& ai = reg.get<ecs::MobAIComp>(zombie);
        ai.state = ecs::MobAIState::Chase;
        ai.target = m_session.playerProxy();
        (void)m_session.tick(*m_world, m_player, {}, 0.75f);
        (void)m_world->tick();
        ASSERT_TRUE(reg.valid(zombie)) << "burnt in the lava at tick " << t;
        const glm::vec3 p = reg.get<ecs::TransformComp>(zombie).pos;
        ASSERT_TRUE(p.x < 6.0f && p.y > static_cast<float>(kY) - 0.05f)
            << "walked into the lava at tick " << t << ": " << p.x << "," << p.y << "," << p.z;
    }
    ASSERT_TRUE(reg.valid(zombie));
    EXPECT_GT(reg.get<ecs::TransformComp>(zombie).pos.x, 4.5f) << "it came up to the bank";
}
