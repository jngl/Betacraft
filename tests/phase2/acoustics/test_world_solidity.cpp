// P4.2 — adapter from block ids (World or any block source) to the acoustic solidity query.
#include "acoustics/OcclusionScheduler.hpp"
#include "acoustics/WorldSolidity.hpp"
#include "core/MpscRingBuffer.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <random>
#include <unordered_map>

namespace {

using namespace betacraft::acoustics;
using betacraft::core::BlockPos;
using betacraft::world::BlockId;
using betacraft::world::BlockRegistry;

struct FakeBlockSource {
    std::unordered_map<BlockPos, BlockId> blocks;
    [[nodiscard]] BlockId getBlock(const BlockPos& p) const noexcept {
        const auto it = blocks.find(p);
        return it == blocks.end() ? BlockId::Air : it->second;
    }
};

// Chunked block source with the World-like getChunk() API, counting the (hash) chunk lookups.
struct FakeChunk {
    std::unordered_map<BlockPos, BlockId> local;
    [[nodiscard]] BlockId getBlock(int32_t x, int32_t y, int32_t z) const noexcept {
        if (y < 0 || y >= 128) {
            return BlockId::Air;
        }
        const auto it = local.find({x, y, z});
        return it == local.end() ? BlockId::Air : it->second;
    }
};

struct FakeChunkWorld {
    std::unordered_map<betacraft::core::ChunkCoord, FakeChunk> chunks;
    mutable uint64_t lookups{0};

    void set(const BlockPos& p, BlockId id) {
        const betacraft::core::ChunkCoord c{betacraft::core::floorDiv(p.x, 16), betacraft::core::floorDiv(p.z, 16)};
        chunks[c].local[{betacraft::core::floorMod(p.x, 16), p.y, betacraft::core::floorMod(p.z, 16)}] = id;
    }
    [[nodiscard]] const FakeChunk* getChunk(betacraft::core::ChunkCoord c) const noexcept {
        ++lookups;
        const auto it = chunks.find(c);
        return it == chunks.end() ? nullptr : &it->second;
    }
    // Uncached reference path (what World::getBlock does: one lookup per voxel).
    [[nodiscard]] BlockId getBlock(const BlockPos& p) const noexcept {
        const FakeChunk* c = getChunk({betacraft::core::floorDiv(p.x, 16), betacraft::core::floorDiv(p.z, 16)});
        return c == nullptr ? BlockId::Air
                            : c->getBlock(betacraft::core::floorMod(p.x, 16), p.y, betacraft::core::floorMod(p.z, 16));
    }
};

class WorldSolidityTest : public ::testing::Test {
protected:
    void SetUp() override { BlockRegistry::initialize(); }
};

} // namespace

TEST_F(WorldSolidityTest, SolidBlocksBlockSound) {
    EXPECT_TRUE(blocksSound(BlockId::Stone));
    EXPECT_TRUE(blocksSound(BlockId::Dirt));
    EXPECT_TRUE(blocksSound(BlockId::OakPlanks));
    EXPECT_TRUE(blocksSound(BlockId::Obsidian));
}

TEST_F(WorldSolidityTest, AirLiquidsAndThinBlocksDoNot) {
    EXPECT_FALSE(blocksSound(BlockId::Air));
    EXPECT_FALSE(blocksSound(BlockId::Water));
    EXPECT_FALSE(blocksSound(BlockId::Torch));
    EXPECT_FALSE(blocksSound(BlockId::TallGrass));
    EXPECT_FALSE(blocksSound(BlockId::Rose));
}

TEST_F(WorldSolidityTest, AdapterDrivesOcclusionFromBlockIds) {
    FakeBlockSource src;
    src.blocks[{2, 0, 0}] = BlockId::Stone;
    src.blocks[{3, 0, 0}] = BlockId::Water; // Liquids do not count
    src.blocks[{4, 0, 0}] = BlockId::Cobblestone;
    const BlockSoliditySource<FakeBlockSource> query(src);
    const OcclusionSample s = measureOcclusion(query, {0.5f, 0.5f, 0.5f}, {6.5f, 0.5f, 0.5f});
    EXPECT_EQ(s.solidVoxels, 2u);
}

TEST_F(WorldSolidityTest, WorldInstantiationCompilesAndUnloadedChunksAreOpenAir) {
    const auto dir = std::filesystem::path(::testing::TempDir()) / "betacraft_acoustics_world";
    const betacraft::world::World world(1234u, dir);
    const WorldSolidityQuery query(world);
    EXPECT_FALSE(query.isSolid({0, 64, 0}));
    const OcclusionSample s = measureOcclusion(query, {0.5f, 70.5f, 0.5f}, {20.5f, 60.5f, -9.5f});
    EXPECT_TRUE(s.valid);
    EXPECT_EQ(s.solidVoxels, 0u);
    EXPECT_FALSE(std::filesystem::exists(dir)) << "acoustic queries must never touch the disk";
}

TEST(OcclusionMpsc, UpdatesRoundTripThroughProjectMpscQueue) {
    betacraft::core::MpscRingBuffer<OcclusionUpdate, 64> queue;
    const OcclusionUpdate sent{42u, 0.5f, 1234.0f, 3u, true};
    ASSERT_TRUE(queue.push(sent));
    OcclusionUpdate received{};
    ASSERT_TRUE(queue.pop(received));
    EXPECT_EQ(received.voiceHandle, 42u);
    EXPECT_FLOAT_EQ(received.gain, 0.5f);
    EXPECT_FLOAT_EQ(received.cutoffHz, 1234.0f);
    EXPECT_EQ(received.solidVoxels, 3u);
    EXPECT_TRUE(received.truncated);
    EXPECT_FALSE(queue.pop(received));
}

TEST_F(WorldSolidityTest, WorldFacadeReturnsUnoccludedCutoffThroughUnloadedTerrain) {
    const auto dir = std::filesystem::path(::testing::TempDir()) / "betacraft_acoustics_world_facade";
    const betacraft::world::World world(99u, dir);
    EXPECT_FLOAT_EQ(computeVoxelOcclusion(world, {0.5f, 70.5f, 0.5f}, {10.5f, 70.5f, 3.5f}), 20000.0f);
    EXPECT_FALSE(std::filesystem::exists(dir));
}

// --- Chunk-cached adapter: one chunk lookup per chunk crossed, not per voxel ---------------------

TEST_F(WorldSolidityTest, ChunkCachedAdapterAgreesWithPerVoxelLookupsEverywhere) {
    FakeChunkWorld w;
    std::mt19937 rng(31u);
    std::uniform_int_distribution<int> xz(-40, 40);
    std::uniform_int_distribution<int> y(-4, 131);
    const BlockId ids[] = {BlockId::Stone, BlockId::Water, BlockId::Dirt, BlockId::Torch};
    for (int i = 0; i < 3000; ++i) {
        w.set({xz(rng), std::clamp(y(rng), 0, 127), xz(rng)}, ids[static_cast<size_t>(i) % 4]);
    }
    const BlockSoliditySource<FakeChunkWorld> reference(w);
    const ChunkCachedSolidity<FakeChunkWorld, 16, 16> cached(w);
    for (int i = 0; i < 20000; ++i) {
        const BlockPos p{xz(rng), y(rng), xz(rng)};
        ASSERT_EQ(cached.isSolid(p), reference.isSolid(p)) << p.x << "," << p.y << "," << p.z;
    }
}

TEST_F(WorldSolidityTest, ChunkCachedAdapterLooksUpEachCrossedChunkOnce) {
    FakeChunkWorld w;
    w.set({5, 10, 3}, BlockId::Stone);
    w.set({20, 10, 3}, BlockId::Stone);
    w.set({-7, 10, 3}, BlockId::Stone);
    const ChunkCachedSolidity<FakeChunkWorld, 16, 16> cached(w);
    w.lookups = 0;
    const OcclusionSample s = measureOcclusion(cached, {-15.5f, 10.5f, 3.5f}, {30.5f, 10.5f, 3.5f});
    EXPECT_EQ(s.solidVoxels, 3u);
    EXPECT_EQ(w.lookups, 3u); // Chunks x = -1, 0, 1 (46 voxels)

    const BlockSoliditySource<FakeChunkWorld> perVoxel(w);
    w.lookups = 0;
    EXPECT_EQ(measureOcclusion(perVoxel, {-15.5f, 10.5f, 3.5f}, {30.5f, 10.5f, 3.5f}).solidVoxels, 3u);
    EXPECT_EQ(w.lookups, 45u); // Every voxel but the listener's (uncached baseline)
}

TEST_F(WorldSolidityTest, ChunkCachedAdapterRereadsAfterInvalidate) {
    FakeChunkWorld w;
    ChunkCachedSolidity<FakeChunkWorld, 16, 16> cached(w);
    EXPECT_FALSE(cached.isSolid({1, 1, 1})); // Caches "chunk (0,0) not loaded"
    w.set({1, 1, 1}, BlockId::Stone);        // Chunk streams in
    cached.invalidate();
    EXPECT_TRUE(cached.isSolid({1, 1, 1}));
}

TEST_F(WorldSolidityTest, WorldChunkCachedQueryCompilesAndUnloadedChunksAreOpenAir) {
    const auto dir = std::filesystem::path(::testing::TempDir()) / "betacraft_acoustics_world_cached";
    const betacraft::world::World world(7u, dir);
    const WorldChunkSolidityQuery query(world);
    EXPECT_FALSE(query.isSolid({0, 64, 0}));
    EXPECT_EQ(measureOcclusion(query, {0.5f, 70.5f, 0.5f}, {30.5f, 60.5f, -9.5f}).solidVoxels, 0u);
    EXPECT_FALSE(std::filesystem::exists(dir));
}
