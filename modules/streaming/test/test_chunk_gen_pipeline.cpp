// ChunkGenPipeline: pass 1 (terrain + own lighting) on worker threads from a shared
// const WorldGen, nearest jobs first, cancellable, reset by a seed change.
#include "voxel/Block.hpp"
#include "world/ChunkGenPipeline.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using namespace betacraft;
using world::ChunkGenConfig;
using world::ChunkGenPipeline;
using voxel::Chunk;

namespace {

[[nodiscard]] std::unique_ptr<Chunk> syncGenerate(uint64_t seed, core::ChunkCoord coord) {
    const world::WorldGen gen(seed);
    auto chunk = std::make_unique<Chunk>(coord);
    gen.generateChunk(*chunk);
    chunk->recalculateLighting();
    return chunk;
}

// Blocks, metadata and both light channels of every cell.
[[nodiscard]] ::testing::AssertionResult sameChunk(const Chunk& a, const Chunk& b) {
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                if (a.getBlock(x, y, z) != b.getBlock(x, y, z) || a.getMetadata(x, y, z) != b.getMetadata(x, y, z) ||
                    a.getSkyLight(x, y, z) != b.getSkyLight(x, y, z) ||
                    a.getBlockLight(x, y, z) != b.getBlockLight(x, y, z)) {
                    return ::testing::AssertionFailure() << "differs at " << x << "," << y << "," << z;
                }
    return ::testing::AssertionSuccess();
}

[[nodiscard]] std::vector<core::ChunkCoord> coordsOf(std::vector<world::GeneratedChunk>& ready) {
    std::vector<core::ChunkCoord> out;
    for (const auto& r : ready) out.push_back(r.coord);
    return out;
}

class ChunkGenPipelineTest : public ::testing::Test {
protected:
    void SetUp() override { voxel::BlockRegistry::initialize(); }
};

} // namespace

TEST_F(ChunkGenPipelineTest, GeneratedChunkIdenticalToSyncGeneration) {
    for (const uint64_t seed : {173173173ULL, 42ULL, 0xDEADBEEFCAFEULL}) {
        world::ChunkGenPipeline pipeline(std::make_shared<const world::WorldGen>(seed), world::ChunkGenConfig{.workerCount = 3});
        std::vector<core::ChunkCoord> coords;
        for (int32_t z = -2; z <= 2; ++z)
            for (int32_t x = -2; x <= 2; ++x) {
                coords.push_back({x * 7 - 3, z * 5 + 1});
                ASSERT_TRUE(pipeline.request(coords.back()));
            }
        ASSERT_TRUE(pipeline.waitIdle());
        std::vector<world::GeneratedChunk> ready = pipeline.takeReady();
        ASSERT_EQ(ready.size(), 25U);
        for (const world::GeneratedChunk& r : ready) {
            ASSERT_NE(r.chunk, nullptr);
            EXPECT_EQ(r.chunk->getCoord(), r.coord);
            const std::unique_ptr<Chunk> expected = syncGenerate(seed, r.coord);
            EXPECT_TRUE(sameChunk(*r.chunk, *expected)) << "seed " << seed << " chunk " << r.coord.x << "," << r.coord.z;
        }
        EXPECT_EQ(pipeline.stats().completed, 25U);
    }
}

TEST_F(ChunkGenPipelineTest, CancelledCoordNeverIntegrated) {
    world::ChunkGenPipeline pipeline(std::make_shared<const world::WorldGen>(1ULL), world::ChunkGenConfig{.workerCount = 0});
    ASSERT_TRUE(pipeline.request({0, 0}));
    ASSERT_TRUE(pipeline.request({1, 0}));
    ASSERT_TRUE(pipeline.request({2, 0}));
    EXPECT_FALSE(pipeline.request({1, 0})) << "already pending";
    EXPECT_TRUE(pipeline.cancel({1, 0})); // Pending
    EXPECT_FALSE(pipeline.isScheduled({1, 0}));
    EXPECT_EQ(pipeline.runPendingInline(10), 2U);
    EXPECT_TRUE(pipeline.cancel({2, 0})); // Ready, not taken yet
    std::vector<world::GeneratedChunk> ready = pipeline.takeReady();
    ASSERT_EQ(ready.size(), 1U);
    EXPECT_EQ(ready[0].coord, (core::ChunkCoord{0, 0}));
    EXPECT_FALSE(pipeline.cancel({7, 7}));

    // Threaded: whether the job was pending or already running when cancelled, it never
    // comes out, and the chunk can be requested again.
    world::ChunkGenPipeline threaded(std::make_shared<const world::WorldGen>(1ULL), world::ChunkGenConfig{.workerCount = 2});
    for (int32_t i = 0; i < 16; ++i) ASSERT_TRUE(threaded.request({i, 3}));
    for (int32_t i = 0; i < 16; i += 2) EXPECT_TRUE(threaded.cancel({i, 3}));
    ASSERT_TRUE(threaded.waitIdle());
    ready = threaded.takeReady();
    EXPECT_EQ(ready.size(), 8U);
    for (const auto& r : ready) EXPECT_EQ(r.coord.x % 2, 1) << r.coord.x;
    ASSERT_TRUE(threaded.request({0, 3}));
    ASSERT_TRUE(threaded.waitIdle());
    ready = threaded.takeReady();
    ASSERT_EQ(ready.size(), 1U);
    EXPECT_EQ(ready[0].coord, (core::ChunkCoord{0, 3}));
}

TEST_F(ChunkGenPipelineTest, PriorityByDistance) {
    world::ChunkGenPipeline pipeline(std::make_shared<const world::WorldGen>(5ULL), world::ChunkGenConfig{.workerCount = 0});
    pipeline.setCenter({0, 0});
    for (const core::ChunkCoord c : {core::ChunkCoord{5, 5}, core::ChunkCoord{1, 0}, core::ChunkCoord{-3, 0},
                                     core::ChunkCoord{0, 0}, core::ChunkCoord{0, 2}}) {
        ASSERT_TRUE(pipeline.request(c));
    }
    EXPECT_EQ(pipeline.runPendingInline(10), 5U);
    std::vector<world::GeneratedChunk> ready = pipeline.takeReady();
    const std::vector<core::ChunkCoord> expected{{0, 0}, {1, 0}, {0, 2}, {-3, 0}, {5, 5}};
    EXPECT_EQ(coordsOf(ready), expected);

    // Moving the centre re-orders what is still pending.
    for (const core::ChunkCoord c : {core::ChunkCoord{0, 0}, core::ChunkCoord{10, 0}, core::ChunkCoord{6, 0}}) {
        ASSERT_TRUE(pipeline.request(c));
    }
    pipeline.setCenter({10, 0});
    EXPECT_EQ(pipeline.runPendingInline(10), 3U);
    ready = pipeline.takeReady();
    const std::vector<core::ChunkCoord> moved{{10, 0}, {6, 0}, {0, 0}};
    EXPECT_EQ(coordsOf(ready), moved);
}

// Cross-chunk lighting depends on integration order, so ready chunks come out by priority
// (distance, then request order) whichever worker finished first: a threaded run and an
// inline run hand them over identically.
TEST_F(ChunkGenPipelineTest, ReadyOrderIndependentOfWorkerTiming) {
    std::vector<core::ChunkCoord> coords;
    for (int32_t z = -3; z <= 3; ++z)
        for (int32_t x = -3; x <= 3; ++x) coords.push_back({x, z});
    const auto run = [&coords](size_t workers) {
        world::ChunkGenPipeline pipeline(std::make_shared<const world::WorldGen>(9ULL), world::ChunkGenConfig{.workerCount = workers});
        pipeline.setCenter({1, -1});
        for (const core::ChunkCoord c : coords) {
            EXPECT_TRUE(pipeline.request(c));
        }
        EXPECT_TRUE(pipeline.waitIdle());
        std::vector<world::GeneratedChunk> ready = pipeline.takeReady();
        return coordsOf(ready);
    };
    const std::vector<core::ChunkCoord> inlineOrder = run(0);
    ASSERT_EQ(inlineOrder.size(), coords.size());
    for (int repeat = 0; repeat < 3; ++repeat) {
        EXPECT_EQ(run(4), inlineOrder);
    }
}

TEST_F(ChunkGenPipelineTest, SeedChangeDiscardsInFlight) {
    world::ChunkGenPipeline pipeline(std::make_shared<const world::WorldGen>(11ULL), world::ChunkGenConfig{.workerCount = 0});
    ASSERT_TRUE(pipeline.request({0, 0}));
    ASSERT_TRUE(pipeline.request({1, 1}));
    EXPECT_EQ(pipeline.runPendingInline(1), 1U); // (0, 0) ready, (1, 1) pending
    pipeline.setWorldGen(std::make_shared<const world::WorldGen>(22ULL));
    EXPECT_EQ(pipeline.pendingJobs(), 0U);
    EXPECT_EQ(pipeline.readyResults(), 0U);
    EXPECT_EQ(pipeline.runPendingInline(10), 0U);
    ASSERT_TRUE(pipeline.request({0, 0}));
    EXPECT_EQ(pipeline.runPendingInline(10), 1U);
    std::vector<world::GeneratedChunk> ready = pipeline.takeReady();
    ASSERT_EQ(ready.size(), 1U);
    EXPECT_TRUE(sameChunk(*ready[0].chunk, *syncGenerate(22ULL, {0, 0}))) << "built from the new seed";

    // Threaded: jobs running when the seed changes are thrown away; only chunks requested
    // after it come out, all from the new seed.
    world::ChunkGenPipeline threaded(std::make_shared<const world::WorldGen>(11ULL), world::ChunkGenConfig{.workerCount = 2});
    for (int32_t i = 0; i < 12; ++i) ASSERT_TRUE(threaded.request({i, 0}));
    threaded.setWorldGen(std::make_shared<const world::WorldGen>(33ULL));
    ASSERT_TRUE(threaded.request({100, 0}));
    ASSERT_TRUE(threaded.waitIdle());
    ready = threaded.takeReady();
    ASSERT_EQ(ready.size(), 1U);
    EXPECT_EQ(ready[0].coord, (core::ChunkCoord{100, 0}));
    EXPECT_TRUE(sameChunk(*ready[0].chunk, *syncGenerate(33ULL, {100, 0})));
}
