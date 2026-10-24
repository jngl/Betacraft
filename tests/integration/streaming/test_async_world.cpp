// Sprint 7 integration (S7.6, S7.7): World streaming through the I/O worker and the pass-1
// generation pool ends with the same chunks (blocks, decoration, light) as synchronous
// loading, and integration stays within its per-frame budget.
#include "test_support/GLTestContext.hpp"

#include "tests/integration/support/WorldgenSupport.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

using namespace betacraft;
using namespace betacraft::test::p2worldgen;
using voxel::BlockId;
using voxel::Chunk;

namespace {

constexpr int32_t kRadius = 5;

// Integrates everything the streaming requests produce, frame after frame.
void streamUntilSettled(world::World& w, const glm::vec3& centre, int32_t radius) {
    for (int frame = 0; frame < 10000; ++frame) {
        w.requestChunksAround(centre, radius);
        w.waitStreamingIdle();
        const world::World::IntegrationStats stats = w.integrateReadyChunks(std::chrono::seconds(10));
        const world::World::StreamingStats streaming = w.getStreamingStats();
        if (stats.integrated == 0 && streaming.loading == 0 && streaming.generating == 0 && streaming.integrating == 0) {
            return;
        }
    }
    FAIL() << "streaming never settled";
}

[[nodiscard]] ::testing::AssertionResult sameVoxels(const Chunk& a, const Chunk& b) {
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                if (a.getBlock(x, y, z) != b.getBlock(x, y, z) || a.getMetadata(x, y, z) != b.getMetadata(x, y, z) ||
                    a.getSkyLight(x, y, z) != b.getSkyLight(x, y, z) ||
                    a.getBlockLight(x, y, z) != b.getBlockLight(x, y, z)) {
                    return ::testing::AssertionFailure()
                           << "differs at " << x << "," << y << "," << z << ": block "
                           << static_cast<int>(a.getBlock(x, y, z)) << " vs " << static_cast<int>(b.getBlock(x, y, z))
                           << ", sky " << int{a.getSkyLight(x, y, z)} << " vs " << int{b.getSkyLight(x, y, z)};
                }
    return ::testing::AssertionSuccess();
}

} // namespace

TEST(StreamingAsyncWorld, AsyncAndSyncWorldsConverge) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir("betacraft_s7_async_");
    // A save holding player edits in a few chunks, so both paths read some chunks from disk
    // (one of them through the I/O worker) and generate the rest.
    const core::BlockPos marks[] = {{3, 110, 3}, {-20, 111, 9}, {40, 112, -30}};
    {
        world::World seedWorld(kSeed, dir.path() / "base");
        for (const core::BlockPos& m : marks) {
            (void)seedWorld.ensureChunkLoaded({core::floorDiv(m.x, 16), core::floorDiv(m.z, 16)});
            ASSERT_TRUE(seedWorld.setBlock(m, BlockId::Glowstone));
        }
        seedWorld.saveModifiedChunks(storage::WorldLevelData{});
    }
    std::filesystem::copy(dir.path() / "base", dir.path() / "sync", std::filesystem::copy_options::recursive);
    std::filesystem::copy(dir.path() / "base", dir.path() / "async", std::filesystem::copy_options::recursive);

    const glm::vec3 centre(8.0f, 70.0f, -4.0f);
    world::World syncWorld(kSeed, dir.path() / "sync");
    syncWorld.updateChunksAround(centre, kRadius, 1000);

    world::World asyncWorld(kSeed, dir.path() / "async");
    asyncWorld.setGenerationWorkers(3);
    streamUntilSettled(asyncWorld, centre, kRadius);

    ASSERT_EQ(asyncWorld.getChunks().size(), syncWorld.getChunks().size());
    for (const auto& [coord, chunk] : syncWorld.getChunks()) {
        const Chunk* other = asyncWorld.getChunk(coord);
        ASSERT_NE(other, nullptr) << coord.x << "," << coord.z;
        EXPECT_EQ(asyncWorld.isChunkDecorated(coord), syncWorld.isChunkDecorated(coord)) << coord.x << "," << coord.z;
        EXPECT_TRUE(sameVoxels(*other, *chunk)) << "chunk " << coord.x << "," << coord.z;
    }
    for (const core::BlockPos& m : marks) {
        EXPECT_EQ(asyncWorld.getBlock(m), BlockId::Glowstone);
    }
    EXPECT_EQ(asyncWorld.getStorageStats().bundlesWritten, 0U) << "streaming in writes nothing";
}

TEST(StreamingAsyncWorld, IntegrationRespectsBudget) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir("betacraft_s7_budget_");
    world::World w(kSeed, dir.path());
    const glm::vec3 centre(0.0f, 70.0f, 0.0f);
    w.requestChunksAround(centre, 6);
    w.waitStreamingIdle();
    const size_t ready = w.getStreamingStats().integrating;
    ASSERT_GT(ready, 100U); // Everything read or generated, nothing integrated yet
    EXPECT_TRUE(w.getChunks().empty());

    constexpr auto kBudget = std::chrono::milliseconds(2);
    const world::World::IntegrationStats first = w.integrateReadyChunks(kBudget);
    EXPECT_GE(first.integrated, 1U);
    EXPECT_LT(first.integrated, ready) << "a 2 ms budget cannot integrate a whole radius";
    EXPECT_TRUE(first.budgetExhausted);
    // Every chunk but the last started inside the budget (margin for scheduling noise).
    EXPECT_LE(first.elapsed, kBudget + first.slowestChunk + std::chrono::milliseconds(5));
    EXPECT_EQ(w.getChunks().size(), first.integrated);

    const world::World::IntegrationStats capped = w.integrateReadyChunks(std::chrono::seconds(10), 3);
    EXPECT_EQ(capped.integrated, 3U);

    // Frame after frame, everything gets in and the F3 numbers settle.
    size_t total = first.integrated + capped.integrated;
    for (int frame = 0; frame < 10000 && w.getStreamingStats().integrating > 0; ++frame) {
        total += w.integrateReadyChunks(kBudget).integrated;
    }
    EXPECT_EQ(total, ready);
    const world::World::StreamingStats stats = w.getStreamingStats();
    EXPECT_EQ(stats.integrating, 0U);
    EXPECT_EQ(stats.generating, 0U);
    EXPECT_EQ(stats.loading, 0U);
    EXPECT_GT(stats.maxIntegration.count(), 0);
    EXPECT_GE(stats.maxIntegration, stats.lastIntegration);
}

TEST(StreamingAsyncWorld, UnloadedEditReloadsFromSaveQueue) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir("betacraft_s7_requeue_");
    world::World w(kSeed, dir.path());
    const core::BlockPos mark{5, 115, 5};
    const glm::vec3 home(8.0f, 70.0f, 8.0f);
    streamUntilSettled(w, home, 2);
    ASSERT_TRUE(w.setBlock(mark, BlockId::Obsidian));
    // Far away: the edited chunk is unloaded and its save queued; coming back streams it in
    // again from the queue or the region, never from regenerated terrain.
    w.requestChunksAround(glm::vec3(5000.0f, 70.0f, 5000.0f), 0);
    ASSERT_EQ(w.getChunk({0, 0}), nullptr);
    streamUntilSettled(w, home, 2);
    EXPECT_EQ(w.getBlock(mark), BlockId::Obsidian);
    EXPECT_TRUE(w.flushStorage());
    EXPECT_EQ(w.getStreamingStats().writing, 0U);
    EXPECT_GE(w.getStreamingStats().openRegions, 1U);
    EXPECT_GT(w.getStreamingStats().openRegionBytes, 0U);
}

// Sprint 8 hardening: an async read of a chunk, cancelled by a synchronous ensureChunkLoaded
// after it finished but before it was collected, must not answer the chunk's next request:
// the chunk was edited, unloaded and requested again in between, and the late answer holds
// the older contents.
TEST(StreamingAsyncWorld, LateAnswerToACancelledLoadIsDiscarded) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir("betacraft_s8_stale_load_");
    world::World w(kSeed, dir.path());
    w.setGenerationWorkers(0);
    const core::BlockPos mark{5, 115, 5};
    const glm::vec3 home(8.0f, 70.0f, 8.0f);
    const glm::vec3 away(5000.0f, 70.0f, 5000.0f);

    // Version 1 of the chunk on disk.
    (void)w.ensureChunkLoaded({0, 0});
    ASSERT_TRUE(w.setBlock(mark, BlockId::Obsidian));
    w.requestChunksAround(away, 0);
    ASSERT_EQ(w.getChunk({0, 0}), nullptr);
    ASSERT_TRUE(w.flushStorage());

    // An async request whose read (version 1) completes but is not collected yet...
    w.requestChunksAround(home, 0);
    w.waitDiskIdle();
    // ...is cancelled by a synchronous load; the chunk is edited (version 2), unloaded and
    // requested again before the next pump.
    (void)w.ensureChunkLoaded({0, 0});
    ASSERT_EQ(w.getBlock(mark), BlockId::Obsidian);
    ASSERT_TRUE(w.setBlock(mark, BlockId::Glass));
    w.requestChunksAround(away, 0);
    ASSERT_EQ(w.getChunk({0, 0}), nullptr);
    w.requestChunksAround(home, 0);

    streamUntilSettled(w, home, 0);
    ASSERT_NE(w.getChunk({0, 0}), nullptr);
    EXPECT_EQ(w.getBlock(mark), BlockId::Glass);
}
