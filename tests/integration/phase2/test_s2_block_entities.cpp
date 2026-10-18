// Sprint 2 integration: block entities (furnace, chest) live in World::blockEntities(),
// follow their chunk to and from disk (the chunk bundle's block entity record since S7.2; the
// .be file before), leave the store through the single
// applyBlockEdit hook (their contents drop on the next game tick), and the furnace ticks in
// EntitySimulation, swapping Furnace <-> LitFurnace without losing its inventory.
#include "P2GameplaySupport.hpp"

#include "crafting/ItemIds.hpp"
#include "world/BlockEntityStore.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>

using namespace betacraft;
using namespace betacraft::test::p2gameplay;
using world::ChestData;
using world::FurnaceData;
using world::StoredStack;

namespace {

namespace items = gameplay::items;

constexpr core::BlockPos kFurnacePos{8, kY, 6};
constexpr core::BlockPos kChestPos{9, kY, 6};

[[nodiscard]] StoredStack stack(gameplay::ItemId id, uint8_t count) {
    return StoredStack{id, count, 0};
}

[[nodiscard]] ChestData fullChest() {
    ChestData c{};
    for (size_t i = 0; i < c.slots.size(); ++i) {
        c.slots[i] = stack(i % 2 == 0 ? gameplay::toItemId(BlockId::Cobblestone) : items::IronIngot,
                           static_cast<uint8_t>(i + 1));
    }
    return c;
}

class S2BlockEntities : public GameplayWorld {
protected:
    FurnaceData& placeFurnace(const FurnaceData& data) {
        EXPECT_TRUE(m_world->setBlock(kFurnacePos, BlockId::Furnace));
        auto& f = std::get<FurnaceData>(m_world->blockEntities().getOrCreate(kFurnacePos, BlockId::Furnace));
        f = data;
        m_world->markBlockEntityChanged(kFurnacePos);
        return f;
    }

    void placeChest(const ChestData& data) {
        ASSERT_TRUE(m_world->setBlock(kChestPos, BlockId::Chest));
        std::get<ChestData>(m_world->blockEntities().getOrCreate(kChestPos, BlockId::Chest)) = data;
        m_world->markBlockEntityChanged(kChestPos);
    }

    // S7.3: block entities are stored in chunk (0, 0)'s region bundle, not a .be file.
    [[nodiscard]] std::vector<world::BlockEntityEntry> storedEntities() const {
        return m_world->getStorage().loadBlockEntities({0, 0});
    }

    // Replaces the stored block entity record of chunk (0, 0) with `bytes` (hand-written or
    // corrupt), as a .be file could be edited before.
    void overwriteStoredBlockEntities(std::span<const uint8_t> bytes) {
        ASSERT_TRUE(m_world->getStorage().closeRegions()); // No stale header left open
        world::WorldStorage storage(m_dir.path());
        std::optional<storage::ChunkBundle> bundle = storage.loadChunkBundle({0, 0});
        ASSERT_TRUE(bundle.has_value());
        bundle->blockEntities.assign(bytes.begin(), bytes.end());
        ASSERT_TRUE(storage.saveChunkBundle({0, 0}, *bundle));
        ASSERT_TRUE(storage.flush());
    }

    // A second World on the same save directory (a restart), with the fixture's chunks.
    [[nodiscard]] std::unique_ptr<world::World> reopen() const {
        auto w = std::make_unique<world::World>(test::p2worldgen::kSeed, m_dir.path());
        test::p2worldgen::loadAll(*w, test::p2worldgen::squareRegion({0, 0}, 1));
        return w;
    }

    [[nodiscard]] std::map<gameplay::ItemId, int32_t> droppedTotals() {
        std::map<gameplay::ItemId, int32_t> out;
        m_session.entities().view<ecs::ItemDropComp>().each(
            [&](ecs::EntityId, const ecs::ItemDropComp& item) { out[item.id] += item.count; });
        return out;
    }

    static constexpr FurnaceData kOresAndCoal{.input = {15, 8, 0}, .fuel = {items::Coal, 1, 0}};
};

} // namespace

TEST_F(S2BlockEntities, FurnaceProgressSurvivesUnloadReload) {
    (void)placeFurnace(kOresAndCoal);
    for (int i = 0; i < 250; ++i) {
        m_session.tickBlockEntities(*m_world);
    }
    const FurnaceData before = std::get<FurnaceData>(*m_world->blockEntities().find(kFurnacePos));
    ASSERT_EQ(before.output, stack(items::IronIngot, 1));
    ASSERT_EQ(before.cookTicks, 50);
    ASSERT_EQ(m_world->getBlock(kFurnacePos), BlockId::LitFurnace);

    m_world->updateChunksAround({4000.0f, 64.0f, 4000.0f}, 0, 0); // Unloads everything
    ASSERT_EQ(m_world->getChunk({0, 0}), nullptr);
    EXPECT_EQ(m_world->blockEntities().size(), 0U);
    EXPECT_EQ(storedEntities().size(), 1U);

    (void)m_world->ensureChunkLoaded({0, 0});
    EXPECT_EQ(m_world->getBlock(kFurnacePos), BlockId::LitFurnace);
    const world::BlockEntity* after = m_world->blockEntities().find(kFurnacePos);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(std::get<FurnaceData>(*after), before);
}

TEST_F(S2BlockEntities, ChestContentsSurviveSaveModifiedChunks) {
    placeChest(fullChest());
    m_world->saveModifiedChunks(world::WorldLevelData{});
    ASSERT_EQ(storedEntities().size(), 1U);

    auto w = reopen();
    EXPECT_EQ(w->getBlock(kChestPos), BlockId::Chest);
    const world::BlockEntity* e = w->blockEntities().find(kChestPos);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(std::get<ChestData>(*e), fullChest());
}

TEST_F(S2BlockEntities, BreakingAChestDropsAllStacks) {
    placeChest(fullChest());
    const MineResult r = mine(kChestPos);
    ASSERT_TRUE(r.broken.has_value());
    EXPECT_EQ(m_world->getBlock(kChestPos), BlockId::Air);
    EXPECT_EQ(m_world->blockEntities().find(kChestPos), nullptr);

    std::map<gameplay::ItemId, int32_t> expected{{items::Chest, 1}};
    for (const StoredStack& s : fullChest().slots) expected[s.id] += s.count;
    EXPECT_EQ(droppedTotals(), expected);
    EXPECT_TRUE(m_world->takeRemovedBlockEntities().empty()) << "the tick consumed them";

    // The chunk's stored entities lose the chest too.
    m_world->saveModifiedChunks(world::WorldLevelData{});
    EXPECT_TRUE(storedEntities().empty());
    const auto bundle = m_world->getStorage().loadChunkBundle({0, 0});
    ASSERT_TRUE(bundle.has_value());
    EXPECT_TRUE(bundle->blockEntities.empty()) << "no record at all, as there was no .be file";
}

TEST_F(S2BlockEntities, FurnaceLitSwapKeepsInventory) {
    (void)placeFurnace(kOresAndCoal);
    (void)tickIdle();
    ASSERT_EQ(m_world->getBlock(kFurnacePos), BlockId::LitFurnace);
    EXPECT_EQ(m_world->getBlockLight(kFurnacePos.x - 1, kFurnacePos.y, kFurnacePos.z), 12);
    const world::BlockEntity* e = m_world->blockEntities().find(kFurnacePos);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(std::get<FurnaceData>(*e).input, stack(15, 8));
    EXPECT_TRUE(std::get<FurnaceData>(*e).fuel.empty()) << "the coal is burning";
    EXPECT_EQ(itemEntityCount(), 0U) << "the swap must not drop the inventory";

    tickIdle(1600);
    ASSERT_EQ(m_world->getBlock(kFurnacePos), BlockId::Furnace);
    EXPECT_EQ(m_world->getBlockLight(kFurnacePos.x - 1, kFurnacePos.y, kFurnacePos.z), 0);
    e = m_world->blockEntities().find(kFurnacePos);
    ASSERT_NE(e, nullptr);
    const auto& f = std::get<FurnaceData>(*e);
    EXPECT_EQ(f.output, stack(items::IronIngot, 8));
    EXPECT_TRUE(f.input.empty());
    EXPECT_EQ(itemEntityCount(), 0U);
}

TEST_F(S2BlockEntities, PistonRefusesToPushAChest) {
    const core::BlockPos piston{kChestPos.x - 1, kY, kChestPos.z};
    placeChest(fullChest());
    ASSERT_TRUE(m_world->setBlock(piston, BlockId::Piston));
    EXPECT_FALSE(m_world->triggerPistonPush(piston, core::FaceDir::PosX));
    EXPECT_EQ(m_world->getBlock(kChestPos), BlockId::Chest);
    EXPECT_NE(m_world->blockEntities().find(kChestPos), nullptr);

    // A chest further down the line blocks the push too, and so does a furnace without data.
    const core::BlockPos stone{kChestPos.x, kY, kChestPos.z + 2};
    ASSERT_TRUE(m_world->setBlock({piston.x, kY, stone.z}, BlockId::Piston));
    ASSERT_TRUE(m_world->setBlock(stone, BlockId::Stone));
    ASSERT_TRUE(m_world->setBlock({stone.x + 1, kY, stone.z}, BlockId::Furnace));
    EXPECT_FALSE(m_world->triggerPistonPush({piston.x, kY, stone.z}, core::FaceDir::PosX));
    EXPECT_EQ(m_world->getBlock(stone), BlockId::Stone);
}

TEST_F(S2BlockEntities, CorruptBeFileStillLoadsTheChunk) {
    placeChest(fullChest());
    m_world->saveModifiedChunks(world::WorldLevelData{});
    const std::string garbage = "BCBE\x01garbage";
    overwriteStoredBlockEntities(std::span(reinterpret_cast<const uint8_t*>(garbage.data()), garbage.size()));
    auto w = reopen();
    ASSERT_NE(w->getChunk({0, 0}), nullptr);
    EXPECT_EQ(w->getBlock(kChestPos), BlockId::Chest);
    EXPECT_EQ(w->getBlock(2, kFloorY, 2), BlockId::Glass);
    EXPECT_EQ(w->blockEntities().find(kChestPos), nullptr);
}

TEST_F(S2BlockEntities, EntityWithoutMatchingBlockIsDiscarded) {
    placeChest(fullChest());
    ASSERT_TRUE(m_world->setBlock(kFurnacePos, BlockId::Stone));
    m_world->saveModifiedChunks(world::WorldLevelData{});
    // Hand-written record: the real chest plus a furnace entity over a stone block.
    const std::vector<world::BlockEntityEntry> entries{{kChestPos, fullChest()}, {kFurnacePos, kOresAndCoal}};
    overwriteStoredBlockEntities(world::encodeBlockEntities(entries));
    auto w = reopen();
    EXPECT_NE(w->blockEntities().find(kChestPos), nullptr);
    EXPECT_EQ(w->blockEntities().find(kFurnacePos), nullptr);
    EXPECT_EQ(w->blockEntities().size(), 1U);
}

TEST_F(S2BlockEntities, NoBeFileForOldSaves) {
    // A save with plain blocks only writes no block entity record, and loads without one.
    ASSERT_TRUE(m_world->setBlock(kFurnacePos, BlockId::Stone));
    m_world->saveModifiedChunks(world::WorldLevelData{});
    const auto bundle = m_world->getStorage().loadChunkBundle({0, 0});
    ASSERT_TRUE(bundle.has_value());
    EXPECT_FALSE(bundle->blocks.empty());
    EXPECT_TRUE(bundle->blockEntities.empty());

    auto w = reopen();
    EXPECT_EQ(w->getBlock(kFurnacePos), BlockId::Stone);
    EXPECT_EQ(w->blockEntities().size(), 0U);
}

// S2.10: a record written with the legacy ids 100..105 (items_scheme 1 era) loads its stacks
// as Beta numbers; the codec itself stays byte-exact.
TEST_F(S2BlockEntities, LegacyItemIdsInBeFileFoldOnLoad) {
    ChestData legacy{};
    legacy.slots[0] = stack(items::legacy::Stick, 16);
    legacy.slots[1] = stack(items::legacy::Coal, 9);
    legacy.slots[2] = StoredStack{items::legacy::StonePickaxe, 1, 17};
    legacy.slots[3] = stack(gameplay::toItemId(BlockId::Cobblestone), 64);
    placeChest(legacy);
    m_world->saveModifiedChunks(world::WorldLevelData{});

    auto w = reopen();
    const world::BlockEntity* e = w->blockEntities().find(kChestPos);
    ASSERT_NE(e, nullptr);
    const ChestData& loaded = std::get<ChestData>(*e);
    EXPECT_EQ(loaded.slots[0], stack(items::Stick, 16));
    EXPECT_EQ(loaded.slots[1], stack(items::Coal, 9));
    EXPECT_EQ(loaded.slots[2], (StoredStack{items::StonePickaxe, 1, 17}));
    EXPECT_EQ(loaded.slots[3], stack(gameplay::toItemId(BlockId::Cobblestone), 64));
}
