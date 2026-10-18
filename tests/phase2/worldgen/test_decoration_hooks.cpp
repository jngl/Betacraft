// P3.4 integration hooks:
//   * FunctionChunkAccess lets World plug `getChunkMutable` into the pipeline via a lambda.
//   * onDecoratedChunkLoaded(coord, pass1Copy): WorldStorage does not persist feature plans,
//     so a chunk restored from disk is re-planned from a deterministically regenerated
//     pass-1 copy; neighbours still awaiting pass 2 then receive its overhanging trees.
#include "worldgen/DecorationPipeline.hpp"
#include "WorldgenTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

using namespace betacraft::worldgen;
using betacraft::core::ChunkCoord;
using betacraft::world::BlockId;
using betacraft::world::Chunk;

namespace {

constexpr uint64_t SEED = 173173173ULL;

using ChunkMap = std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>>;

Chunk& generate(ChunkMap& map, ChunkCoord c) {
    auto chunk = std::make_unique<Chunk>(c);
    test::fillDecorationTerrain(*chunk);
    Chunk& ref = *chunk;
    map[c] = std::move(chunk);
    return ref;
}

auto lookupIn(ChunkMap& map) {
    return [&map](ChunkCoord c) noexcept -> Chunk* {
        const auto it = map.find(c);
        return it == map.end() ? nullptr : it->second.get();
    };
}

std::vector<ChunkCoord> grid(int32_t lo, int32_t hi) {
    std::vector<ChunkCoord> out;
    for (int32_t z = lo; z <= hi; ++z)
        for (int32_t x = lo; x <= hi; ++x) out.push_back(ChunkCoord{x, z});
    return out;
}

bool sameBlocks(const Chunk& a, const Chunk& b) {
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                if (a.getBlock(x, y, z) != b.getBlock(x, y, z)) return false;
    return true;
}

std::unique_ptr<Chunk> copyOf(const Chunk& src) {
    auto out = std::make_unique<Chunk>(src.getCoord());
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x) out->setBlock(x, y, z, src.getBlock(x, y, z), false);
    return out;
}

} // namespace

TEST(FunctionChunkAccess, ForwardsLookupsToTheCallable) {
    ChunkMap map;
    Chunk& c = generate(map, ChunkCoord{2, -3});
    FunctionChunkAccess access(lookupIn(map));
    EXPECT_EQ(access.findChunk(ChunkCoord{2, -3}), &c);
    EXPECT_EQ(access.findChunk(ChunkCoord{0, 0}), nullptr);
}

TEST(FunctionChunkAccess, DrivesThePipeline) {
    ChunkMap map;
    FunctionChunkAccess access(lookupIn(map));
    DecorationPipeline pipeline(SEED, access);
    std::vector<ChunkCoord> decorated;
    for (ChunkCoord c : grid(-1, 1)) {
        generate(map, c);
        const auto done = pipeline.onTerrainReady(c);
        decorated.insert(decorated.end(), done.begin(), done.end());
    }
    ASSERT_EQ(decorated.size(), 1U);
    EXPECT_EQ(decorated[0], (ChunkCoord{0, 0}));
    EXPECT_TRUE(pipeline.isDecorated(ChunkCoord{0, 0}));
}

TEST(DecorationPipelineRestore, ReplanFromPassOneCopyReproducesTheOriginalPlan) {
    ChunkMap mapA;
    FunctionChunkAccess accessA(lookupIn(mapA));
    DecorationPipeline a(SEED, accessA);
    for (ChunkCoord c : grid(-2, 2)) {
        generate(mapA, c);
        (void)a.onTerrainReady(c);
    }

    ChunkMap mapB;
    FunctionChunkAccess accessB(lookupIn(mapB));
    DecorationPipeline b(SEED, accessB);
    mapB[ChunkCoord{0, 0}] = copyOf(*mapA.at(ChunkCoord{0, 0})); // decorated blocks from "disk"
    Chunk pass1(ChunkCoord{0, 0});
    test::fillDecorationTerrain(pass1); // WorldGen pass 1 re-run into a scratch chunk
    EXPECT_TRUE(b.onDecoratedChunkLoaded(ChunkCoord{0, 0}, pass1).empty());
    EXPECT_TRUE(b.isDecorated(ChunkCoord{0, 0}));
    ASSERT_NE(b.findPlan(ChunkCoord{0, 0}), nullptr);
    EXPECT_EQ(*b.findPlan(ChunkCoord{0, 0}), *a.findPlan(ChunkCoord{0, 0}));

    std::vector<ChunkCoord> decorated;
    for (ChunkCoord c : grid(-2, 2)) {
        if (c == ChunkCoord{0, 0}) continue;
        generate(mapB, c);
        const auto done = b.onTerrainReady(c);
        decorated.insert(decorated.end(), done.begin(), done.end());
    }
    EXPECT_TRUE(std::find(decorated.begin(), decorated.end(), ChunkCoord{0, 0}) == decorated.end());
    for (ChunkCoord c : grid(-1, 1)) {
        EXPECT_TRUE(sameBlocks(*mapA.at(c), *mapB.at(c))) << c.x << "," << c.z;
    }
}

TEST(DecorationPipelineRestore, PassOneCopyOfAnotherChunkIsRejected) {
    ChunkMap map;
    FunctionChunkAccess access(lookupIn(map));
    DecorationPipeline p(SEED, access);
    Chunk wrong(ChunkCoord{1, 0});
    test::fillDecorationTerrain(wrong);
    EXPECT_TRUE(p.onDecoratedChunkLoaded(ChunkCoord{0, 0}, wrong).empty());
    EXPECT_FALSE(p.hasPlan(ChunkCoord{0, 0}));
    EXPECT_FALSE(p.hasPlan(ChunkCoord{1, 0}));
}

TEST(DecorationPipelineRestore, KnownChunkIsNotReplanned) {
    ChunkMap map;
    FunctionChunkAccess access(lookupIn(map));
    DecorationPipeline p(SEED, access);
    generate(map, ChunkCoord{0, 0});
    (void)p.onTerrainReady(ChunkCoord{0, 0});
    const FeaturePlan before = *p.findPlan(ChunkCoord{0, 0});
    const Chunk empty(ChunkCoord{0, 0}); // would plan nothing
    EXPECT_TRUE(p.onDecoratedChunkLoaded(ChunkCoord{0, 0}, empty).empty());
    EXPECT_EQ(*p.findPlan(ChunkCoord{0, 0}), before);
    EXPECT_FALSE(p.isDecorated(ChunkCoord{0, 0}));
}
