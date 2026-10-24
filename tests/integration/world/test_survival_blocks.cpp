// Sprint 5 survival blocks. S5.5 ladder: placed against the side of a normal cube (metadata =
// Beta side 2..5), climbed (descent capped at 3 b/s, no fall damage, sneaking holds on,
// pushing against the wall climbs at 4 b/s), broken and dropped when its wall goes.
// S5.6 door: placed as two halves, both flip on a click, one item dropped. S5.7 farming: hoe,
// farmland moisture, seeds, crop growth, trampling, harvest. S5.8 bed: placed along the look,
// and its recipe (sleeping is a WorldSession matter: test_world_session_sleep.cpp).
#include "tests/integration/support/GameplaySupport.hpp"

#include "crafting/BetaRecipes.hpp"
#include "crafting/CraftingGrid.hpp"
#include "ecs/Systems.hpp"
#include "game/ItemUse.hpp"
#include "gameplay/Crops.hpp"
#include "gameplay/ItemIds.hpp"
#include "voxel/BlockShapes.hpp"
#include "world/BlockTicks.hpp"

using namespace betacraft;
using namespace betacraft::test::gameplay_support;

namespace {

class LadderBlock : public GameplayWorld {
protected:
    static constexpr int32_t kWallX = 6;
    static constexpr int32_t kLadderX = 5;
    static constexpr int32_t kZ = 2;
    static constexpr int32_t kTop = kY + 6; // Below the build limit (128)

    // A stone wall at x = 6 with ladders hanging on its -X side (meta 4) from kY to kTop.
    void buildLadder() {
        for (int32_t y = kY; y <= kTop; ++y) {
            ASSERT_TRUE(m_world->setBlock(kWallX, y, kZ, BlockId::Stone));
            ASSERT_TRUE(m_world->setBlockWithMetadata(kLadderX, y, kZ, BlockId::Ladder, 4));
        }
    }

    void step(const physics::PlayerInput& input, int frames = 1) {
        for (int i = 0; i < frames; ++i) {
            (void)m_player.fixedUpdate(1.0f / 60.0f, input, *m_world);
        }
    }

    [[nodiscard]] int32_t droppedCount(BlockId id) {
        int32_t n = 0;
        m_session.entities().view<ecs::ItemDropComp>().each([&](ecs::EntityId, const ecs::ItemDropComp& item) {
            n += item.id == gameplay::toItemId(id) ? item.count : 0;
        });
        return n;
    }
};

} // namespace

TEST_F(LadderBlock, PlacedOnTheClickedWallSide) {
    ASSERT_TRUE(m_world->setBlock(kWallX, kY, kZ, BlockId::Stone));
    hold({gameplay::toItemId(BlockId::Ladder), 4});
    // Clicking the -X face of the wall puts the ladder at x = 5 hanging on +X: Beta meta 4.
    ASSERT_EQ(game::placeHeldBlock(*m_world, m_player, targetAt({kWallX, kY, kZ}, BlockId::Stone)),
              std::optional<BlockId>{BlockId::Ladder});
    EXPECT_EQ(m_world->getMetadata(kLadderX, kY, kZ), 4);
    EXPECT_EQ(m_player.getHeldItem().count, 3);

    // Clicking the floor's top next to the wall picks the wall (first normal cube found).
    ASSERT_TRUE(m_world->setBlock(kWallX, kY, kZ + 2, BlockId::Stone));
    physics::RaycastHit floorHit{};
    floorHit.hit = true;
    floorHit.blockPos = {kLadderX, kFloorY, kZ + 2};
    floorHit.adjacentPos = {kLadderX, kY, kZ + 2};
    floorHit.face = core::FaceDir::PosY;
    floorHit.blockId = BlockId::Glass;
    ASSERT_TRUE(game::placeHeldBlock(*m_world, m_player, floorHit).has_value());
    EXPECT_EQ(m_world->getMetadata(kLadderX, kY, kZ + 2), 4);

    // No normal cube around (glass is not one): nothing is placed.
    physics::RaycastHit bare = floorHit;
    bare.blockPos = {9, kFloorY, 9};
    bare.adjacentPos = {9, kY, 9};
    EXPECT_FALSE(game::placeHeldBlock(*m_world, m_player, bare).has_value());
    EXPECT_EQ(m_world->getBlock(9, kY, 9), BlockId::Air);
    EXPECT_EQ(m_player.getHeldItem().count, 2);
}

TEST_F(LadderBlock, DescentIsCappedAndCostsNoFallDamage) {
    buildLadder();
    m_player.setPosition({kLadderX + 0.5f, static_cast<float>(kTop) + 0.5f, kZ + 0.5f});
    float slowest = 0.0f;
    step({}); // Leaves the floor the fixture stood on
    ASSERT_FALSE(m_player.isOnGround());
    for (int i = 0; i < 600 && !m_player.isOnGround(); ++i) {
        step({});
        EXPECT_TRUE(m_player.isOnLadder() || m_player.isOnGround());
        slowest = std::min(slowest, m_player.getVelocity().y);
    }
    EXPECT_TRUE(m_player.isOnGround());
    EXPECT_GE(slowest, -physics::Player::LADDER_MAX_DESCENT_SPEED - 1e-4f) << "vy >= -3 b/s on a ladder";
    EXPECT_LT(slowest, -2.9f) << "it still slides down";
    EXPECT_LT(m_player.takePendingFall(), 0.1f) << "a 6-block ladder ride lands without a fall";
}

TEST_F(LadderBlock, SneakingHoldsOn) {
    buildLadder();
    m_player.setPosition({kLadderX + 0.5f, static_cast<float>(kY) + 4.0f, kZ + 0.5f});
    physics::PlayerInput sneak{};
    sneak.sneak = true;
    step(sneak, 5);
    const float y0 = m_player.getPosition().y;
    step(sneak, 60);
    EXPECT_NEAR(m_player.getPosition().y, y0, 1e-3f) << "a sneaking player stays put";
    EXPECT_FALSE(m_player.isOnGround());
}

TEST_F(LadderBlock, PushingAgainstTheWallClimbs) {
    buildLadder();
    placePlayer({kLadderX + 0.5f, static_cast<float>(kY), kZ + 0.5f});
    physics::PlayerInput push{};
    push.forward = true; // yaw 0: towards +X, into the ladder plate and the wall
    step(push, 60);
    EXPECT_GT(m_player.getPosition().y, static_cast<float>(kY) + 3.0f) << "about 4 b/s for a second";
    EXPECT_NEAR(m_player.getVelocity().y, physics::Player::LADDER_CLIMB_SPEED, 1e-4f);
    EXPECT_LT(m_player.getPosition().x, static_cast<float>(kWallX) - 0.125f - 0.29f) << "the plate is solid";
}

TEST_F(LadderBlock, BreaksAndDropsWhenItsWallGoes) {
    buildLadder();
    m_player.setPosition({kLadderX + 0.5f, static_cast<float>(kY) + 5.0f, kZ + 0.5f});
    step({}, 5);
    ASSERT_TRUE(m_player.isOnLadder());

    // Removing the wall block behind the player's ladders breaks them.
    for (int32_t y = kY; y <= kTop; ++y) {
        ASSERT_TRUE(m_world->setBlock(kWallX, y, kZ, BlockId::Air));
        EXPECT_EQ(m_world->getBlock(kLadderX, y, kZ), BlockId::Air) << y;
    }
    // A ladder whose wall is elsewhere does not care.
    ASSERT_TRUE(m_world->setBlock(kLadderX, kY, kZ + 4, BlockId::Stone));
    ASSERT_TRUE(m_world->setBlockWithMetadata(kLadderX, kY, kZ + 5, BlockId::Ladder, 3)); // wall at -Z
    ASSERT_TRUE(m_world->setBlock(kLadderX + 1, kY, kZ + 5, BlockId::Stone));
    ASSERT_TRUE(m_world->setBlock(kLadderX + 1, kY, kZ + 5, BlockId::Air));
    EXPECT_EQ(m_world->getBlock(kLadderX, kY, kZ + 5), BlockId::Ladder);

    tickIdle(2); // The session drops what the edits broke
    EXPECT_EQ(droppedCount(BlockId::Ladder), kTop - kY + 1);

    // With nothing to hold on to, the player falls freely.
    float fastest = 0.0f;
    for (int i = 0; i < 40; ++i) {
        step({});
        fastest = std::min(fastest, m_player.getVelocity().y);
    }
    EXPECT_FALSE(m_player.isOnLadder());
    EXPECT_LT(fastest, -physics::Player::LADDER_MAX_DESCENT_SPEED - 1.0f);
}

// --- S5.6 door ------------------------------------------------------------------------------

namespace {

namespace items = gameplay::items;

// Counts the dropped items of `id` (whole stacks) in the session.
int32_t droppedItems(game::EntitySimulation& session, gameplay::ItemId id) {
    int32_t n = 0;
    session.entities().view<ecs::ItemDropComp>().each([&](ecs::EntityId, const ecs::ItemDropComp& item) {
        n += item.id == id ? item.count : 0;
    });
    return n;
}

physics::RaycastHit topOf(const core::BlockPos& pos, BlockId id) {
    physics::RaycastHit hit{};
    hit.hit = true;
    hit.blockPos = pos;
    hit.adjacentPos = pos.offset(0, 1, 0);
    hit.face = core::FaceDir::PosY;
    hit.blockId = id;
    hit.distance = 2.5f;
    return hit;
}

class DoorBlock : public GameplayWorld {
protected:
    static constexpr int32_t kX = 5;
    static constexpr int32_t kZ = 2;

    // A stone floor cell to stand the door on; the player looks along +X (orientation 0).
    std::optional<game::ItemUseResult> placeDoor() {
        EXPECT_TRUE(m_world->setBlock(kX, kFloorY, kZ, BlockId::Stone));
        hold({items::WoodenDoor, 1});
        return game::useHeldItemOnBlock(*m_world, m_player, topOf({kX, kFloorY, kZ}, BlockId::Stone));
    }
};

} // namespace

TEST_F(DoorBlock, PlacedAsTwoHalvesFacingTheLook) {
    const std::optional<game::ItemUseResult> used = placeDoor();
    ASSERT_TRUE(used.has_value());
    EXPECT_EQ(used->kind, game::ItemUseKind::DoorPlaced);
    EXPECT_EQ(m_world->getBlock(kX, kY, kZ), BlockId::WoodDoor);
    EXPECT_EQ(m_world->getBlock(kX, kY + 1, kZ), BlockId::WoodDoor);
    EXPECT_EQ(m_world->getMetadata(kX, kY, kZ), 0) << "looking +X: Beta orientation 0";
    EXPECT_EQ(m_world->getMetadata(kX, kY + 1, kZ), voxel::kDoorUpperBit);
    EXPECT_TRUE(m_player.getHeldItem().isEmpty()) << "the door item is used up";

    EXPECT_EQ(game::doorFacingForLook(0.0f, 1.0f), 1);
    EXPECT_EQ(game::doorFacingForLook(-1.0f, 0.2f), 2);
    EXPECT_EQ(game::doorFacingForLook(0.3f, -1.0f), 3);
}

TEST_F(DoorBlock, HingeMovesToTheWallSide) {
    // Orientation 0 runs along Z: a wall on the +Z side puts the hinge there (Beta: turned
    // orientation 3 plus the open bit, which still draws closed).
    ASSERT_TRUE(m_world->setBlock(kX, kY, kZ + 1, BlockId::Stone));
    ASSERT_TRUE(m_world->setBlock(kX, kY + 1, kZ + 1, BlockId::Stone));
    ASSERT_TRUE(placeDoor().has_value());
    EXPECT_EQ(m_world->getMetadata(kX, kY, kZ), 3 | voxel::kDoorOpenBit);
    EXPECT_EQ(m_world->getMetadata(kX, kY + 1, kZ), 3 | voxel::kDoorOpenBit | voxel::kDoorUpperBit);
    EXPECT_EQ(voxel::doorState(m_world->getMetadata(kX, kY, kZ)), voxel::doorState(0)) << "same closed box";
}

TEST_F(DoorBlock, NeedsTwoFreeCellsOverANormalCube) {
    hold({items::WoodenDoor, 1});
    // The glass floor is not a normal cube.
    EXPECT_FALSE(game::useHeldItemOnBlock(*m_world, m_player, topOf({kX, kFloorY, kZ}, BlockId::Glass)));
    ASSERT_TRUE(m_world->setBlock(kX, kFloorY, kZ, BlockId::Stone));
    ASSERT_TRUE(m_world->setBlock(kX, kY + 1, kZ, BlockId::Cobblestone)); // No room for the upper half
    EXPECT_FALSE(game::useHeldItemOnBlock(*m_world, m_player, topOf({kX, kFloorY, kZ}, BlockId::Stone)));
    ASSERT_TRUE(m_world->setBlock(kX, kY + 1, kZ, BlockId::Air));
    physics::RaycastHit side = topOf({kX, kFloorY, kZ}, BlockId::Stone);
    side.face = core::FaceDir::NegX;
    EXPECT_FALSE(game::useHeldItemOnBlock(*m_world, m_player, side)) << "doors go on top faces only";
    EXPECT_EQ(m_player.getHeldItem().count, 1);
    EXPECT_EQ(m_world->getBlock(kX, kY, kZ), BlockId::Air);
}

TEST_F(DoorBlock, ClickFlipsBothHalves) {
    ASSERT_TRUE(placeDoor().has_value());
    EXPECT_EQ(game::toggleDoor(*m_world, {kX, kY + 1, kZ}), std::optional<bool>{true}) << "clicked on the upper half";
    EXPECT_EQ(m_world->getMetadata(kX, kY, kZ), voxel::kDoorOpenBit);
    EXPECT_EQ(m_world->getMetadata(kX, kY + 1, kZ), voxel::kDoorOpenBit | voxel::kDoorUpperBit);
    EXPECT_EQ(game::toggleDoor(*m_world, {kX, kY, kZ}), std::optional<bool>{false});
    EXPECT_EQ(m_world->getMetadata(kX, kY, kZ), 0);
    EXPECT_EQ(m_world->getMetadata(kX, kY + 1, kZ), voxel::kDoorUpperBit);
    EXPECT_FALSE(game::toggleDoor(*m_world, {kX, kFloorY, kZ}).has_value()) << "not a door";
}

TEST_F(DoorBlock, BreakingEitherHalfDropsOneDoor) {
    ASSERT_TRUE(placeDoor().has_value());
    const MineResult r = mine({kX, kY + 1, kZ}); // The upper half, by hand
    ASSERT_TRUE(r.broken.has_value());
    EXPECT_EQ(m_world->getBlock(kX, kY + 1, kZ), BlockId::Air);
    EXPECT_EQ(m_world->getBlock(kX, kY, kZ), BlockId::Air) << "the lower half went with it";
    tickIdle(2);
    EXPECT_EQ(droppedItems(m_session, items::WoodenDoor), 1);

    // The floor under a second door goes: the door breaks with a single drop.
    hold({items::WoodenDoor, 1});
    ASSERT_TRUE(game::useHeldItemOnBlock(*m_world, m_player, topOf({kX, kFloorY, kZ}, BlockId::Stone)));
    ASSERT_TRUE(m_world->setBlock(kX, kFloorY, kZ, BlockId::Air));
    EXPECT_EQ(m_world->getBlock(kX, kY, kZ), BlockId::Air);
    EXPECT_EQ(m_world->getBlock(kX, kY + 1, kZ), BlockId::Air);
    tickIdle(2);
    EXPECT_EQ(droppedItems(m_session, items::WoodenDoor), 2);
}

TEST_F(DoorBlock, ACreativePistonBreaksTheDoorWithOneDropAndKeepsStairsOrientation) {
    // Beta getMobilityFlag() == 1: a pushed door breaks with its drop instead of moving, so
    // no lone half is left behind.
    ASSERT_TRUE(placeDoor().has_value());
    ASSERT_TRUE(m_world->setBlock(kX - 1, kY, kZ, BlockId::Piston));
    EXPECT_TRUE(m_world->triggerPistonPush({kX - 1, kY, kZ}, core::FaceDir::PosX));
    EXPECT_EQ(m_world->getBlock(kX, kY, kZ), BlockId::Air);
    EXPECT_EQ(m_world->getBlock(kX, kY + 1, kZ), BlockId::Air) << "the upper half went with it";
    EXPECT_EQ(m_world->getBlock(kX + 1, kY, kZ), BlockId::Air) << "nothing moved into the next cell";
    tickIdle(2);
    EXPECT_EQ(droppedItems(m_session, items::WoodenDoor), 1);

    // Moved blocks keep their metadata: a pushed stair keeps its orientation.
    ASSERT_TRUE(m_world->setBlockWithMetadata(kX, kY, kZ, BlockId::CobbleStairs, 2));
    EXPECT_TRUE(m_world->triggerPistonPush({kX - 1, kY, kZ}, core::FaceDir::PosX));
    EXPECT_EQ(m_world->getBlock(kX + 1, kY, kZ), BlockId::CobbleStairs);
    EXPECT_EQ(m_world->getMetadata(kX + 1, kY, kZ), 2);
}

// --- S5.7 farming ---------------------------------------------------------------------------

namespace {

class Farming : public GameplayWorld {
protected:
    static constexpr int32_t kX = 5;
    static constexpr int32_t kZ = 2;

    // Runs the random-tick handler of the block at `pos` `n` times (or until it changes).
    void randomTicks(const core::BlockPos& pos, int32_t n) {
        for (int32_t i = 0; i < n; ++i) {
            const BlockId id = m_world->getBlock(pos);
            if (const world::RandomTickFn fn = world::kRandomTick[static_cast<size_t>(id)]) {
                fn(*m_world, pos, m_world->getMetadata(pos), m_rng);
            }
        }
    }

    gameplay::SplitMix64 m_rng{0xFA4317ULL};
};

} // namespace

TEST_F(Farming, HoeTillsGrassAndDirtUnderAir) {
    ASSERT_TRUE(m_world->setBlock(kX, kY, kZ, BlockId::Grass));
    hold({items::WoodHoe, 1});
    const std::optional<game::ItemUseResult> tilled =
        game::useHeldItemOnBlock(*m_world, m_player, topOf({kX, kY, kZ}, BlockId::Grass));
    ASSERT_TRUE(tilled.has_value());
    EXPECT_EQ(tilled->kind, game::ItemUseKind::SoilTilled);
    EXPECT_EQ(tilled->wear, gameplay::WearResult::Worn);
    EXPECT_EQ(m_world->getBlock(kX, kY, kZ), BlockId::Farmland);
    EXPECT_EQ(m_world->getMetadata(kX, kY, kZ), 0);
    EXPECT_EQ(m_player.getHeldItem().damage, 1) << "1 point of wear";

    // Dirt with a block on it, or clicked from below, stays dirt.
    ASSERT_TRUE(m_world->setBlock(kX, kY, kZ + 2, BlockId::Dirt));
    ASSERT_TRUE(m_world->setBlock(kX, kY + 1, kZ + 2, BlockId::Stone));
    EXPECT_FALSE(game::useHeldItemOnBlock(*m_world, m_player, topOf({kX, kY, kZ + 2}, BlockId::Dirt)));
    ASSERT_TRUE(m_world->setBlock(kX, kY + 1, kZ + 2, BlockId::Air));
    physics::RaycastHit below = topOf({kX, kY, kZ + 2}, BlockId::Dirt);
    below.face = core::FaceDir::NegY;
    EXPECT_FALSE(game::useHeldItemOnBlock(*m_world, m_player, below));
    EXPECT_TRUE(game::useHeldItemOnBlock(*m_world, m_player, topOf({kX, kY, kZ + 2}, BlockId::Dirt)));
    EXPECT_EQ(m_world->getBlock(kX, kY, kZ + 2), BlockId::Farmland);
    // Stone is not soil.
    ASSERT_TRUE(m_world->setBlock(kX, kY, kZ + 4, BlockId::Stone));
    EXPECT_FALSE(game::useHeldItemOnBlock(*m_world, m_player, topOf({kX, kY, kZ + 4}, BlockId::Stone)));
}

TEST_F(Farming, SeedsGoOnFarmlandOnly) {
    ASSERT_TRUE(m_world->setBlock(kX, kY, kZ, BlockId::Farmland));
    ASSERT_TRUE(m_world->setBlock(kX, kY, kZ + 2, BlockId::Dirt));
    hold({items::Seeds, 3});
    const std::optional<game::ItemUseResult> planted =
        game::useHeldItemOnBlock(*m_world, m_player, topOf({kX, kY, kZ}, BlockId::Farmland));
    ASSERT_TRUE(planted.has_value());
    EXPECT_EQ(planted->kind, game::ItemUseKind::SeedsPlanted);
    EXPECT_EQ(m_world->getBlock(kX, kY + 1, kZ), BlockId::Crops);
    EXPECT_EQ(m_world->getMetadata(kX, kY + 1, kZ), 0);
    EXPECT_EQ(m_player.getHeldItem().count, 2);
    EXPECT_FALSE(game::useHeldItemOnBlock(*m_world, m_player, topOf({kX, kY, kZ + 2}, BlockId::Dirt)));
    EXPECT_FALSE(game::useHeldItemOnBlock(*m_world, m_player, topOf({kX, kY, kZ}, BlockId::Farmland)))
        << "already planted";
    EXPECT_EQ(m_player.getHeldItem().count, 2);
}

TEST_F(Farming, FarmlandStaysWetByWaterAndDriesBackToDirtElsewhere) {
    // Wet farmland 4 cells from water, dry farmland far from it.
    ASSERT_TRUE(m_world->setBlock(kX, kY, kZ, BlockId::Farmland));
    ASSERT_TRUE(m_world->setBlock(kX + 4, kY, kZ, BlockId::Water));
    ASSERT_TRUE(world::farmlandHasWaterNearby(*m_world, {kX, kY, kZ}));
    randomTicks({kX, kY, kZ}, 100);
    EXPECT_EQ(m_world->getBlock(kX, kY, kZ), BlockId::Farmland);
    EXPECT_EQ(m_world->getMetadata(kX, kY, kZ), gameplay::kFarmlandWetMoisture);

    const core::BlockPos dry{-4, kY, 12};
    ASSERT_TRUE(m_world->setBlockWithMetadata(dry.x, dry.y, dry.z, BlockId::Farmland, 3));
    ASSERT_FALSE(world::farmlandHasWaterNearby(*m_world, dry));
    randomTicks(dry, 400);
    EXPECT_EQ(m_world->getBlock(dry), BlockId::Dirt) << "dried out, then back to dirt";

    // Crops keep dry farmland from turning to dirt.
    const core::BlockPos sown{-4, kY, 8};
    ASSERT_TRUE(m_world->setBlockWithMetadata(sown.x, sown.y, sown.z, BlockId::Farmland, 2));
    ASSERT_TRUE(m_world->setBlock(sown.x, sown.y + 1, sown.z, BlockId::Crops));
    for (int i = 0; i < 400; ++i) {
        world::kRandomTick[static_cast<size_t>(BlockId::Farmland)](*m_world, sown, m_world->getMetadata(sown), m_rng);
    }
    EXPECT_EQ(m_world->getBlock(sown), BlockId::Farmland);
    EXPECT_EQ(m_world->getMetadata(sown), 0);
}

TEST_F(Farming, CropsGrowToRipeAndPopOffInTheDark) {
    // A wet 3x3 of farmland with one crop in the middle: rate 10, a stage 1 tick in 10.
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            ASSERT_TRUE(m_world->setBlockWithMetadata(kX + dx, kY, kZ + 4 + dz, BlockId::Farmland, 7));
        }
    }
    const core::BlockPos crop{kX, kY + 1, kZ + 4};
    ASSERT_TRUE(m_world->setBlock(crop, BlockId::Crops));
    EXPECT_FLOAT_EQ(world::cropGrowthRate(*m_world, crop), 10.0f);
    int32_t ticks = 0;
    while (m_world->getMetadata(crop) < gameplay::kCropsRipeStage && ticks < 2000) {
        randomTicks(crop, 1);
        ++ticks;
    }
    EXPECT_EQ(m_world->getBlock(crop), BlockId::Crops);
    EXPECT_EQ(m_world->getMetadata(crop), gameplay::kCropsRipeStage);
    EXPECT_GT(ticks, 7);
    EXPECT_LT(ticks, 400) << "about 70 handler calls on average";
    randomTicks(crop, 50);
    EXPECT_EQ(m_world->getMetadata(crop), gameplay::kCropsRipeStage) << "7 is the last stage";

    // Walled in and roofed with stone: no light, the crop breaks and drops seeds or wheat.
    const core::BlockPos dark{-3, kY + 1, 10};
    ASSERT_TRUE(m_world->setBlock(dark.x, dark.y - 1, dark.z, BlockId::Farmland));
    ASSERT_TRUE(m_world->setBlockWithMetadata(dark.x, dark.y, dark.z, BlockId::Crops, 7));
    for (const glm::ivec3 d : {glm::ivec3{1, 0, 0}, glm::ivec3{-1, 0, 0}, glm::ivec3{0, 0, 1}, glm::ivec3{0, 0, -1},
                               glm::ivec3{0, 1, 0}}) {
        ASSERT_TRUE(m_world->setBlock(dark.x + d.x, dark.y + d.y, dark.z + d.z, BlockId::Stone));
    }
    ASSERT_LT(m_world->getSkyLight(dark.x, dark.y, dark.z), gameplay::kCropsMinStayLight);
    randomTicks(dark, 1);
    EXPECT_EQ(m_world->getBlock(dark), BlockId::Air);
    tickIdle(1);
    EXPECT_EQ(droppedItems(m_session, items::Wheat), 1) << "a ripe crop gives its wheat";
}

TEST_F(Farming, RipeHarvestThenBread) {
    ASSERT_TRUE(m_world->setBlock(kX, kY, kZ, BlockId::Farmland));
    ASSERT_TRUE(m_world->setBlockWithMetadata(kX, kY + 1, kZ, BlockId::Crops, 7));
    const MineResult r = mine({kX, kY + 1, kZ});
    ASSERT_TRUE(r.broken.has_value());
    EXPECT_EQ(r.ticks, 1) << "crops break at once";
    EXPECT_EQ(droppedItems(m_session, items::Wheat), 1);
    EXPECT_LE(droppedItems(m_session, items::Seeds), gameplay::kCropSeedTries);

    // Three wheat in a row make a loaf, which heals 5.
    const crafting::RecipeRegistry book = crafting::makeBetaRecipeBook();
    crafting::CraftingGrid grid{crafting::GridSize::Workbench3x3};
    for (int x = 0; x < 3; ++x) {
        ASSERT_TRUE(grid.set(x, 1, crafting::SlotStack{items::Wheat, 1, 0}));
    }
    const auto match = book.match(grid);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->result, (crafting::RecipeResult{items::Bread, 1}));
    m_player.setHealth(10);
    hold({items::Bread, 1});
    EXPECT_EQ(m_session.useHeldItem(m_player), std::optional<int32_t>{5});
}

TEST_F(Farming, TramplingTurnsFarmlandBackToDirt) {
    ASSERT_TRUE(m_world->setBlock(kX, kY, kZ, BlockId::Farmland));
    ASSERT_TRUE(m_world->setBlockWithMetadata(kX, kY + 1, kZ, BlockId::Crops, 4));
    // A short hop never tramples (rand < fall - 0.5 is impossible below 0.5).
    physics::StepEvent hop{};
    hop.landedFallDistance = 0.4f;
    hop.landedOn = {kX, kY, kZ};
    for (int i = 0; i < 100; ++i) {
        EXPECT_FALSE(game::trampleFarmland(*m_world, hop, m_rng));
    }
    // A real fall onto it: the player lands on the 15/16 farmland top and reports it.
    m_player.setPosition({kX + 0.5f, static_cast<float>(kY) + 4.5f, kZ + 0.5f});
    ASSERT_TRUE(m_world->setBlock(kX, kY + 1, kZ, BlockId::Air));
    physics::StepEvent landing{};
    for (int i = 0; i < 120 && landing.landedFallDistance <= 0.0f; ++i) {
        landing = m_player.fixedUpdate(1.0f / 60.0f, {}, *m_world);
    }
    ASSERT_GT(landing.landedFallDistance, 3.0f);
    EXPECT_EQ(landing.landedOn, (core::BlockPos{kX, kY, kZ}));
    EXPECT_TRUE(game::trampleFarmland(*m_world, landing, m_rng)) << "rand < 3.5 - 0.5 always holds";
    EXPECT_EQ(m_world->getBlock(kX, kY, kZ), BlockId::Dirt);

    // Crops on farmland that turns to dirt break and drop.
    ASSERT_TRUE(m_world->setBlock(kX, kY, kZ + 3, BlockId::Farmland));
    ASSERT_TRUE(m_world->setBlockWithMetadata(kX, kY + 1, kZ + 3, BlockId::Crops, 7));
    physics::StepEvent jump{};
    jump.landedFallDistance = 2.0f;
    jump.landedOn = {kX, kY, kZ + 3};
    ASSERT_TRUE(game::trampleFarmland(*m_world, jump, m_rng));
    EXPECT_EQ(m_world->getBlock(kX, kY + 1, kZ + 3), BlockId::Air);
    tickIdle(1);
    EXPECT_EQ(droppedItems(m_session, items::Wheat), 1);
}

TEST_F(Farming, TallGrassGivesSeedsSometimes) {
    int32_t broken = 0;
    for (int32_t i = 0; i < 64; ++i) {
        ASSERT_TRUE(m_world->setBlock(kX, kY, kZ, BlockId::TallGrass));
        broken += mine({kX, kY, kZ}).broken ? 1 : 0;
    }
    EXPECT_EQ(broken, 64);
    const int32_t seeds = droppedItems(m_session, items::Seeds);
    EXPECT_GT(seeds, 0) << "1 time in 8";
    EXPECT_LT(seeds, 24);
}

// --- S5.8 bed -------------------------------------------------------------------------------

namespace {

class BedBlock : public GameplayWorld {
protected:
    static constexpr int32_t kX = 5;
    static constexpr int32_t kZ = 2;

    void SetUp() override {
        GameplayWorld::SetUp();
        m_session.setNaturalSpawning(false);
        // A stone floor around the bed: beds and respawn spots need normal cubes under them.
        for (int32_t z = kZ - 3; z <= kZ + 3; ++z) {
            for (int32_t x = kX - 2; x <= kX + 4; ++x) {
                ASSERT_TRUE(m_world->setBlock(x, kFloorY, z, BlockId::Stone));
            }
        }
    }

    // Looking along +X: foot at (5, kY, 2), head at (6, kY, 2).
    void placeBed() {
        hold({items::Bed, 1});
        const std::optional<game::ItemUseResult> used =
            game::useHeldItemOnBlock(*m_world, m_player, topOf({kX, kFloorY, kZ}, BlockId::Stone));
        ASSERT_TRUE(used.has_value());
        ASSERT_EQ(used->kind, game::ItemUseKind::BedPlaced);
    }

    const core::BlockPos m_foot{kX, kY, kZ};
    const core::BlockPos m_head{kX + 1, kY, kZ};
};

} // namespace

TEST_F(BedBlock, PlacedAlongTheLook) {
    placeBed();
    EXPECT_EQ(m_world->getBlock(m_foot), BlockId::Bed);
    EXPECT_EQ(m_world->getBlock(m_head), BlockId::Bed);
    EXPECT_EQ(m_world->getMetadata(m_foot), 3) << "Beta orientation 3: the head towards +X";
    EXPECT_EQ(m_world->getMetadata(m_head), 3 | voxel::kBedHeadBit);
    EXPECT_TRUE(m_player.getHeldItem().isEmpty());
    EXPECT_EQ(game::bedHeadAt(*m_world, m_foot), std::optional<core::BlockPos>{m_head});
    EXPECT_EQ(game::bedHeadAt(*m_world, m_head), std::optional<core::BlockPos>{m_head});

    // No room for the head: nothing placed.
    hold({items::Bed, 1});
    ASSERT_TRUE(m_world->setBlock(kX + 1, kY, kZ + 2, BlockId::Cobblestone));
    EXPECT_FALSE(game::useHeldItemOnBlock(*m_world, m_player, topOf({kX, kFloorY, kZ + 2}, BlockId::Stone)));
    EXPECT_EQ(m_world->getBlock(kX, kY, kZ + 2), BlockId::Air);
    EXPECT_EQ(game::bedFacingForLook(0.0f, 1.0f), 0);
    EXPECT_EQ(game::bedFacingForLook(-1.0f, 0.0f), 1);
    EXPECT_EQ(game::bedFacingForLook(0.0f, -1.0f), 2);
}

TEST_F(BedBlock, DoorAndBedRecipes) {
    const crafting::RecipeRegistry book = crafting::makeBetaRecipeBook();
    crafting::CraftingGrid door{crafting::GridSize::Workbench3x3};
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 2; ++x) {
            ASSERT_TRUE(door.set(x, y, crafting::SlotStack{gameplay::toItemId(BlockId::OakPlanks), 1, 0}));
        }
    }
    ASSERT_TRUE(book.match(door).has_value());
    EXPECT_EQ(book.match(door)->result, (crafting::RecipeResult{items::WoodenDoor, 1}));

    crafting::CraftingGrid bedGrid{crafting::GridSize::Workbench3x3};
    for (int x = 0; x < 3; ++x) {
        ASSERT_TRUE(bedGrid.set(x, 0, crafting::SlotStack{gameplay::toItemId(BlockId::Wool), 1, 0}));
        ASSERT_TRUE(bedGrid.set(x, 1, crafting::SlotStack{gameplay::toItemId(BlockId::OakPlanks), 1, 0}));
    }
    ASSERT_TRUE(book.match(bedGrid).has_value());
    EXPECT_EQ(book.match(bedGrid)->result, (crafting::RecipeResult{items::Bed, 1}));
    EXPECT_EQ(gameplay::maxStackSize(items::WoodenDoor), 1);
    EXPECT_EQ(gameplay::maxStackSize(items::Bed), 1);
}
