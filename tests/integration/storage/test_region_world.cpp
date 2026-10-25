// Integration: World saves each chunk as one bundle in region files,
// reloads it from there, and the fluid simulation alone never writes a bundle (bug 8 under
// the new storage, with the F3 counter that must stay at 0).
#include "test_support/GLTestContext.hpp"

#include "tests/integration/support/WorldgenSupport.hpp"
#include "storage/RegionChunkStore.hpp"
#include "voxel/BlockEntityStore.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <filesystem>

using namespace betacraft;
using namespace betacraft::test::worldgen_support;
using voxel::BlockId;
using voxel::Chunk;

namespace {

[[nodiscard]] size_t regionFileCount(const std::filesystem::path& save) {
    size_t n = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(save / "region", ec)) {
        if (entry.path().extension() == ".bcr") ++n;
    }
    return n;
}

[[nodiscard]] int32_t countWater(const Chunk& c, int32_t minY) {
    int32_t n = 0;
    for (int32_t y = minY; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                if (voxel::isWaterBlock(c.getBlock(x, y, z))) ++n;
    return n;
}

} // namespace

TEST(StorageRegionWorld, EditSaveReloadThroughRegions) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir("betacraft_s7_region_");
    const core::BlockPos glass{-3, 120, 5};     // Chunk (-1, 0), region (-1, 0)
    const core::BlockPos far{40 * 16 + 2, 121, 3}; // Chunk (40, 0), region (1, 0)
    const core::BlockPos chestPos{6, 121, 6};
    voxel::ChestData chest{};
    chest.slots[0] = {265, 33, 0};
    chest.slots[26] = {4, 64, 0};
    std::vector<std::unique_ptr<Chunk>> expected;
    {
        world::World w(kSeed, dir.path());
        loadAll(w, squareRegion({0, 0}, 1));
        ASSERT_TRUE(w.setBlock(glass, BlockId::Glass));
        ASSERT_TRUE(w.setBlock(chestPos, BlockId::Chest));
        std::get<voxel::ChestData>(w.blockEntities().getOrCreate(chestPos, BlockId::Chest)) = chest;
        w.markBlockEntityChanged(chestPos);
        (void)w.ensureChunkLoaded({40, 0});
        ASSERT_TRUE(w.setBlock(far, BlockId::Obsidian));
        w.saveModifiedChunks(storage::WorldLevelData{});
        EXPECT_EQ(w.getStorageStats().unsolicitedBundleWrites, 0U);
        EXPECT_EQ(w.getStorageStats().bundlesWritten, 3U);

        // Streaming eviction of an edited chunk goes through the regions too, and the same
        // world reads it back before any flush.
        ASSERT_TRUE(w.setBlock(glass.offset(0, 1, 0), BlockId::Glass));
        w.updateChunksAround(glm::vec3(-4000.0f, 70.0f, 4000.0f), 0, 0);
        ASSERT_EQ(w.getChunk({-1, 0}), nullptr);
        loadAll(w, squareRegion({0, 0}, 1));
        EXPECT_EQ(w.getBlock(glass.offset(0, 1, 0)), BlockId::Glass);
        w.saveModifiedChunks(storage::WorldLevelData{});
        for (const core::ChunkCoord c : {core::ChunkCoord{-1, 0}, core::ChunkCoord{0, 0}}) {
            auto copy = std::make_unique<Chunk>(c);
            ASSERT_TRUE(copy->deserializeRLE(w.getChunk(c)->serializeRLE()));
            expected.push_back(std::move(copy));
        }
    }
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "chunks")) << "no legacy file written";
    EXPECT_EQ(regionFileCount(dir.path()), 3U); // r.-1.0, r.0.0, r.1.0
    EXPECT_TRUE(std::filesystem::exists(dir.path() / "region" / "r.-1.0.bcr"));
    EXPECT_TRUE(std::filesystem::exists(dir.path() / "region" / "r.1.0.bcr"));

    world::World again(kSeed, dir.path());
    loadAll(again, squareRegion({0, 0}, 1));
    EXPECT_EQ(again.getBlock(glass), BlockId::Glass);
    EXPECT_EQ(again.getBlock(glass.offset(0, 1, 0)), BlockId::Glass);
    for (const auto& c : expected) {
        EXPECT_EQ(firstBlockDifference(*again.getChunk(c->getCoord()), *c), -1) << c->getCoord().x;
        EXPECT_TRUE(again.getChunk(c->getCoord())->isPlayerTouched());
    }
    const voxel::BlockEntity* e = again.blockEntities().find(chestPos);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(std::get<voxel::ChestData>(*e), chest);
    EXPECT_TRUE(again.isChunkDecorated({0, 0}));
    (void)again.ensureChunkLoaded({40, 0});
    EXPECT_EQ(again.getBlock(far), BlockId::Obsidian);
}

TEST(StorageRegionWorld, UntouchedFluidChunksNotWritten) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir("betacraft_s7_region_");
    world::World w(kSeed, dir.path());
    loadAll(w, squareRegion({0, 0}, 2));
    uint32_t updates = 0;
    for (int i = 0; i < 200; ++i) {
        updates += w.tick().processed;
    }
    ASSERT_GT(updates, 0U) << "the seed's generated fluid must move for this test to mean anything";
    w.saveModifiedChunks(storage::WorldLevelData{});
    w.updateChunksAround(glm::vec3(8000.0f, 70.0f, 8000.0f), 0, 0); // Unloads every chunk
    EXPECT_EQ(w.getStorageStats().bundlesWritten, 0U);
    EXPECT_EQ(w.getStorageStats().unsolicitedBundleWrites, 0U);
    EXPECT_EQ(regionFileCount(dir.path()), 0U) << "exploring without editing writes no bundle";

    // Player water flowing over a seam: the touched chunk and its neighbour are saved, and the
    // counter of bundles without a player edit around them stays at 0.
    loadAll(w, squareRegion({0, 0}, 2));
    for (int32_t z = 0; z < Chunk::DEPTH; ++z) {
        for (int32_t x = 0; x < Chunk::WIDTH; ++x) {
            ASSERT_TRUE(w.setBlock(x, 119, z, BlockId::Glass));
        }
    }
    ASSERT_TRUE(w.setBlock(15, 120, 8, BlockId::Water));
    for (int i = 0; i < 200; ++i) {
        (void)w.tick();
    }
    ASSERT_GT(countWater(*w.getChunk({1, 0}), 100), 0);
    w.saveModifiedChunks(storage::WorldLevelData{});
    w.updateChunksAround(glm::vec3(8000.0f, 70.0f, 8000.0f), 0, 0);
    EXPECT_GE(w.getStorageStats().bundlesWritten, 2U);
    EXPECT_EQ(w.getStorageStats().unsolicitedBundleWrites, 0U);

    storage::RegionChunkStore regions(dir.path() / "region", true);
    EXPECT_TRUE(regions.contains({0, 0}));
    EXPECT_TRUE(regions.contains({1, 0}));
    for (const core::ChunkCoord c : squareRegion({0, 0}, 2)) {
        if (std::abs(c.x) <= 1 && std::abs(c.z) <= 1) continue;
        EXPECT_FALSE(regions.contains(c)) << "chunk " << c.x << "," << c.z << " is two chunks from any edit";
    }
}
