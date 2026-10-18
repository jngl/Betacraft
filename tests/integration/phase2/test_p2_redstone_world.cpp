// Sprint 8 integration (S8.5-S8.8): the redstone engine on a real World and through the
// GL-free EntitySimulation: a lever opens an iron door through wire (and the AI sees the door
// open), a pressure plate under the player fires a piston, circuits survive a save and
// reload, and chunks read from disk get their torches and buttons scheduled again.
#include "P2GameplaySupport.hpp"

#include "ai/WorldNavQuery.hpp"
#include "game/ItemUse.hpp"
#include "gameplay/ItemIds.hpp"
#include "redstone/RedstoneEngine.hpp"
#include "world/BlockShapes.hpp"

#include <gtest/gtest.h>

using namespace betacraft;
using namespace betacraft::test::p2gameplay;

namespace {

class P2RedstoneWorld : public GameplayWorld {
protected:
    void SetUp() override {
        GameplayWorld::SetUp();
        // A stone floor (normal cubes) under the circuits, instead of the fixture's glass.
        for (int32_t z = -4; z <= 12; ++z)
            for (int32_t x = -4; x <= 14; ++x) ASSERT_TRUE(m_world->setBlock(x, kFloorY, z, BlockId::Stone));
        m_session.setNaturalSpawning(false);
    }

    void put(const core::BlockPos& p, BlockId id, uint8_t meta = 0) {
        ASSERT_TRUE(m_world->setBlockWithMetadata(p.x, p.y, p.z, id, meta));
    }

    void wireLine(int32_t x0, int32_t x1, int32_t z) {
        for (int32_t x = x0; x <= x1; ++x) put({x, kY, z}, BlockId::RedstoneWire);
    }

    void ironDoor(const core::BlockPos& lower) {
        put(lower, BlockId::IronDoor, 0);
        put(lower.offset(0, 1, 0), BlockId::IronDoor, world::kDoorUpperBit);
    }

    void worldTicks(int32_t n) {
        for (int32_t i = 0; i < n; ++i) (void)m_world->tick();
    }

    [[nodiscard]] bool doorOpen(const core::BlockPos& lower) const {
        return (m_world->getMetadata(lower) & world::kDoorOpenBit) != 0 &&
               (m_world->getMetadata(lower.offset(0, 1, 0)) & world::kDoorOpenBit) != 0;
    }
};

const core::BlockPos kLever{0, kY, 0};
const core::BlockPos kDoor{11, kY, 0};

} // namespace

TEST_F(P2RedstoneWorld, LeverOpensDoorThroughWire) {
    put(kLever, BlockId::Lever, world::kAttachFloor);
    wireLine(1, 10, 0); // 10 wires, the last one points into the door
    ironDoor(kDoor);
    worldTicks(5);
    ASSERT_FALSE(doorOpen(kDoor));
    const ai::WorldNavQuery nav(*m_world);
    EXPECT_EQ(nav.classify(kDoor), ai::NavCell::DoorClosed) << "S8.8: a closed iron door stops the AI";

    ASSERT_TRUE(m_world->toggleLever(kLever));
    worldTicks(3);
    EXPECT_EQ(m_world->getMetadata({10, kY, 0}), 6) << "15 at the lever, one less per wire";
    EXPECT_TRUE(doorOpen(kDoor));
    EXPECT_EQ(nav.classify(kDoor), ai::NavCell::DoorOpen);
    EXPECT_EQ(nav.classify(kDoor.offset(0, 1, 0)), ai::NavCell::DoorOpen);
    bool click = false;
    bool opened = false;
    for (const auto& ev : m_world->takeRedstoneEvents()) {
        click |= ev.kind == redstone::RedstoneEventKind::Click && ev.pos == kLever;
        opened |= ev.kind == redstone::RedstoneEventKind::DoorOpen && ev.pos == kDoor;
    }
    EXPECT_TRUE(click);
    EXPECT_TRUE(opened);

    // A right click does not move an iron door (only redstone does).
    EXPECT_FALSE(game::toggleDoor(*m_world, kDoor).has_value());
    EXPECT_TRUE(doorOpen(kDoor));

    ASSERT_TRUE(m_world->toggleLever(kLever));
    worldTicks(3);
    EXPECT_FALSE(doorOpen(kDoor));
    EXPECT_EQ(m_world->getMetadata({1, kY, 0}), 0);

    // Breaking the wire in the middle cuts the door off.
    ASSERT_TRUE(m_world->toggleLever(kLever));
    worldTicks(3);
    ASSERT_TRUE(doorOpen(kDoor));
    ASSERT_TRUE(m_world->setBlock(5, kY, 0, BlockId::Air));
    worldTicks(3);
    EXPECT_FALSE(doorOpen(kDoor));
}

TEST_F(P2RedstoneWorld, PlateTriggersPiston) {
    const core::BlockPos plate{4, kY, 2};
    const core::BlockPos piston{5, kY, 2};
    put(plate, BlockId::StonePressurePlate);
    put(piston, BlockId::Piston, 5); // facing +X
    put({6, kY, 2}, BlockId::Cobblestone);
    tickIdle(2);
    (void)m_world->tick();
    ASSERT_EQ(m_world->getBlock(6, kY, 2), BlockId::Cobblestone) << "nobody on the plate yet";

    placePlayer({4.5f, static_cast<float>(kY), 2.5f});
    for (int32_t i = 0; i < 4; ++i) {
        (void)tickIdle();
        (void)m_world->tick();
    }
    EXPECT_NE(m_world->getMetadata(plate) & world::kPlatePressedBit, 0) << "the player stands on it";
    EXPECT_EQ(m_world->getBlock(6, kY, 2), BlockId::Air);
    EXPECT_EQ(m_world->getBlock(7, kY, 2), BlockId::Cobblestone) << "pushed on the rising edge";
    EXPECT_NE(m_world->getMetadata(piston) & world::kPistonPoweredBit, 0);
    // Staying on the plate does not push again.
    put({6, kY, 2}, BlockId::Cobblestone);
    for (int32_t i = 0; i < 30; ++i) {
        (void)tickIdle();
        (void)m_world->tick();
    }
    EXPECT_EQ(m_world->getBlock(6, kY, 2), BlockId::Cobblestone);
    EXPECT_EQ(m_world->getBlock(8, kY, 2), BlockId::Air);

    // Stepping off: the plate comes up within 20 ticks and the piston forgets its power.
    placePlayer({1.5f, static_cast<float>(kY), 8.5f});
    for (int32_t i = 0; i < 25; ++i) {
        (void)tickIdle();
        (void)m_world->tick();
    }
    EXPECT_EQ(m_world->getMetadata(plate) & world::kPlatePressedBit, 0);
    EXPECT_EQ(m_world->getMetadata(piston) & world::kPistonPoweredBit, 0);

    // An item lying on a stone plate does nothing; on a wooden one it presses it.
    const core::BlockPos woodPlate{1, kY, 5};
    put(woodPlate, BlockId::WoodPressurePlate);
    (void)m_session.spawnItem(gameplay::makeStack(gameplay::items::Redstone, 1), {4.5f, static_cast<float>(kY), 2.5f});
    (void)m_session.spawnItem(gameplay::makeStack(gameplay::items::Redstone, 1), {1.5f, static_cast<float>(kY), 5.5f});
    for (int32_t i = 0; i < 3; ++i) {
        (void)tickIdle();
        (void)m_world->tick();
    }
    EXPECT_EQ(m_world->getMetadata(plate) & world::kPlatePressedBit, 0);
    EXPECT_NE(m_world->getMetadata(woodPlate) & world::kPlatePressedBit, 0);
}

TEST_F(P2RedstoneWorld, StatePersistsAcrossSaveReload) {
    put(kLever, BlockId::Lever, world::kAttachFloor);
    wireLine(1, 10, 0);
    ironDoor(kDoor);
    ASSERT_TRUE(m_world->toggleLever(kLever));
    worldTicks(4);
    ASSERT_TRUE(doorOpen(kDoor));
    m_world->saveModifiedChunks(world::WorldLevelData{});
    m_world.reset();

    m_world = std::make_unique<world::World>(test::p2worldgen::kSeed, m_dir.path());
    test::p2worldgen::loadAll(*m_world, test::p2worldgen::squareRegion({0, 0}, 1));
    EXPECT_EQ(m_world->getBlock(kLever), BlockId::Lever);
    EXPECT_EQ(m_world->getMetadata(kLever), world::kAttachFloor | world::kLeverOnBit);
    for (int32_t x = 1; x <= 10; ++x) {
        EXPECT_EQ(m_world->getMetadata({x, kY, 0}), 16 - x) << "wire " << x;
    }
    EXPECT_TRUE(doorOpen(kDoor));
    worldTicks(5);
    EXPECT_TRUE(doorOpen(kDoor)) << "still powered after the reload";
    // The reloaded circuit still works.
    ASSERT_TRUE(m_world->toggleLever(kLever));
    worldTicks(3);
    EXPECT_FALSE(doorOpen(kDoor));
    EXPECT_EQ(m_world->getMetadata({1, kY, 0}), 0);
}

TEST_F(P2RedstoneWorld, ChunkReloadReschedulesTorches) {
    // A torch on block A, a lever on A's side; a button on another block.
    const core::BlockPos a{2, kY, 6};
    const core::BlockPos torch{2, kY + 1, 6};
    const core::BlockPos lever{1, kY, 6};
    const core::BlockPos buttonBlock{6, kY, 6};
    const core::BlockPos button{5, kY, 6};
    put(a, BlockId::Stone);
    put(torch, BlockId::RedstoneTorchOn, world::kAttachFloor);
    put(lever, BlockId::Lever, world::kAttachWallPosX);
    put(buttonBlock, BlockId::Stone);
    put(button, BlockId::StoneButton, world::kAttachWallPosX);
    ASSERT_TRUE(m_world->toggleLever(lever));
    worldTicks(5);
    ASSERT_EQ(m_world->getBlock(torch), BlockId::RedstoneTorchOff);

    // Switched back and pressed right before the save: the torch's and the button's pending
    // updates are not saved.
    ASSERT_TRUE(m_world->toggleLever(lever));
    ASSERT_TRUE(m_world->pressButton(button));
    m_world->saveModifiedChunks(world::WorldLevelData{});
    m_world.reset();

    m_world = std::make_unique<world::World>(test::p2worldgen::kSeed, m_dir.path());
    test::p2worldgen::loadAll(*m_world, test::p2worldgen::squareRegion({0, 0}, 1));
    ASSERT_EQ(m_world->getBlock(torch), BlockId::RedstoneTorchOff) << "saved unlit";
    ASSERT_NE(m_world->getMetadata(button) & world::kButtonPressedBit, 0) << "saved pressed";
    worldTicks(4);
    EXPECT_EQ(m_world->getBlock(torch), BlockId::RedstoneTorchOn) << "the reload checked the torch again";
    worldTicks(redstone::RedstoneEngine::kButtonTicks);
    EXPECT_EQ(m_world->getMetadata(button) & world::kButtonPressedBit, 0) << "the button came back up";
}

TEST_F(P2RedstoneWorld, RightClickPartsAndPlacement) {
    // Placement rules: a lever on a wall, a button needs a wall, wire and plates need a floor.
    put({8, kY, 4}, BlockId::Stone);
    physics::RaycastHit side{};
    side.hit = true;
    side.blockPos = {8, kY, 4};
    side.adjacentPos = {7, kY, 4};
    side.face = core::FaceDir::NegX;
    side.blockId = BlockId::Stone;
    hold({gameplay::toItemId(BlockId::StoneButton), 4});
    ASSERT_TRUE(game::placeHeldBlock(*m_world, m_player, side).has_value());
    EXPECT_EQ(m_world->getBlock(7, kY, 4), BlockId::StoneButton);
    EXPECT_EQ(m_world->getMetadata(7, kY, 4), world::kAttachWallPosX);

    physics::RaycastHit top{};
    top.hit = true;
    top.blockPos = {8, kFloorY, 8};
    top.adjacentPos = {8, kY, 8};
    top.face = core::FaceDir::PosY;
    top.blockId = BlockId::Stone;
    EXPECT_FALSE(game::placeHeldBlock(*m_world, m_player, top).has_value()) << "no button on a floor";
    hold({gameplay::toItemId(BlockId::Lever), 1});
    ASSERT_TRUE(game::placeHeldBlock(*m_world, m_player, top).has_value());
    EXPECT_EQ(m_world->getMetadata(8, kY, 8), world::kAttachFloor);

    hold({gameplay::items::Redstone, 3});
    top.blockPos = {9, kFloorY, 8};
    top.adjacentPos = {9, kY, 8};
    const auto wire = game::useHeldItemOnBlock(*m_world, m_player, top);
    ASSERT_TRUE(wire.has_value());
    EXPECT_EQ(wire->kind, game::ItemUseKind::WirePlaced);
    EXPECT_EQ(m_world->getBlock(9, kY, 8), BlockId::RedstoneWire);
    EXPECT_EQ(m_player.getHeldItem().count, 2);

    hold({gameplay::items::IronDoor, 1});
    top.blockPos = {3, kFloorY, 10};
    top.adjacentPos = {3, kY, 10};
    const auto door = game::useHeldItemOnBlock(*m_world, m_player, top);
    ASSERT_TRUE(door.has_value());
    EXPECT_EQ(door->block, BlockId::IronDoor);
    EXPECT_EQ(m_world->getBlock(3, kY + 1, 10), BlockId::IronDoor);

    // The lever placed above powers the wire next to it once switched; breaking the floor
    // under the wire takes the wire away (it lies on the block).
    ASSERT_TRUE(m_world->toggleLever({8, kY, 8}));
    worldTicks(2);
    EXPECT_EQ(m_world->getMetadata(9, kY, 8), 15);
    ASSERT_TRUE(m_world->setBlock(9, kFloorY, 8, BlockId::Air));
    EXPECT_EQ(m_world->getBlock(9, kY, 8), BlockId::Air);
    // The button falls off with its wall.
    ASSERT_TRUE(m_world->setBlock(8, kY, 4, BlockId::Air));
    EXPECT_EQ(m_world->getBlock(7, kY, 4), BlockId::Air);

    // --no-redstone: nothing switches.
    m_world->setRedstoneEnabled(false);
    EXPECT_FALSE(m_world->toggleLever({8, kY, 8}));
}

TEST_F(P2RedstoneWorld, FifteenWiresButtonTorchAndClockOnTheWorld) {
    // DoD: a lever opens a door through 15 wires (the last one carries power 1).
    put({-4, kY, 2}, BlockId::Lever, world::kAttachFloor);
    wireLine(-3, 11, 2);
    const core::BlockPos farDoor{12, kY, 2};
    ironDoor(farDoor);
    worldTicks(5);
    ASSERT_FALSE(doorOpen(farDoor));
    ASSERT_TRUE(m_world->toggleLever({-4, kY, 2}));
    worldTicks(4);
    EXPECT_EQ(m_world->getMetadata({11, kY, 2}), 1);
    EXPECT_TRUE(doorOpen(farDoor));

    // A button next to a door opens it directly, for 20 ticks.
    const core::BlockPos buttonDoor{2, kY, 8};
    ironDoor(buttonDoor);
    put({0, kY, 8}, BlockId::Stone);
    put({1, kY, 8}, BlockId::StoneButton, world::kAttachWallNegX);
    worldTicks(3);
    ASSERT_FALSE(doorOpen(buttonDoor));
    ASSERT_TRUE(m_world->pressButton({1, kY, 8}));
    worldTicks(3);
    EXPECT_TRUE(doorOpen(buttonDoor));
    worldTicks(redstone::RedstoneEngine::kButtonTicks + 2);
    EXPECT_FALSE(doorOpen(buttonDoor));

    // A torch inverts the signal: lever off -> torch lit -> door open; lever on -> closed.
    const core::BlockPos block{6, kY, 10};
    put(block, BlockId::Stone);
    put({5, kY, 10}, BlockId::Lever, world::kAttachWallPosX);
    put({7, kY, 10}, BlockId::RedstoneTorchOn, world::kAttachWallNegX);
    const core::BlockPos torchDoor{8, kY, 10};
    ironDoor(torchDoor);
    worldTicks(6);
    EXPECT_TRUE(doorOpen(torchDoor)) << "the lit torch powers the door";
    ASSERT_TRUE(m_world->toggleLever({5, kY, 10}));
    worldTicks(6);
    EXPECT_EQ(m_world->getBlock({7, kY, 10}), BlockId::RedstoneTorchOff);
    EXPECT_FALSE(doorOpen(torchDoor));

    // A torch clock burns out instead of switching forever.
    put({1, kY, -3}, BlockId::Stone);
    put({2, kY, -3}, BlockId::RedstoneTorchOn, world::kAttachWallNegX);
    for (const core::BlockPos& w : {core::BlockPos{3, kY, -3}, core::BlockPos{3, kY, -2}, core::BlockPos{3, kY, -1},
                                    core::BlockPos{2, kY, -1}, core::BlockPos{1, kY, -1}, core::BlockPos{1, kY, -2}}) {
        put(w, BlockId::RedstoneWire);
    }
    bool burnt = false;
    for (int32_t i = 0; i < 80 && !burnt; ++i) {
        (void)m_world->tick();
        for (const auto& ev : m_world->takeRedstoneEvents()) {
            burnt |= ev.kind == redstone::RedstoneEventKind::TorchBurnout;
        }
    }
    EXPECT_TRUE(burnt);
    EXPECT_EQ(m_world->getBlock({2, kY, -3}), BlockId::RedstoneTorchOff);
}
