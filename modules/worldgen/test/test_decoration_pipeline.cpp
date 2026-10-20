// P3.4 - Two-pass decoration pipeline: pass 2 of a chunk runs only once its 3x3
// neighbourhood finished pass 1; results are identical for any chunk load order.
#include "worldgen/DecorationPipeline.hpp"
#include "worldgen/SeededRandom.hpp"
#include "WorldgenTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace betacraft::worldgen;
using betacraft::core::BlockPos;
using betacraft::core::ChunkCoord;
using betacraft::core::floorDiv;
using betacraft::core::floorMod;
using betacraft::voxel::BlockId;
using betacraft::voxel::Chunk;

namespace {

constexpr uint64_t SEED = 173173173ULL;

class MapChunkStore final : public ChunkAccess {
public:
    [[nodiscard]] Chunk* findChunk(ChunkCoord c) noexcept override {
        const auto it = m_chunks.find(c);
        return it == m_chunks.end() ? nullptr : it->second.get();
    }
    Chunk& generate(ChunkCoord c) {
        auto chunk = std::make_unique<Chunk>(c);
        test::fillDecorationTerrain(*chunk); // pass 1
        Chunk& ref = *chunk;
        m_chunks[c] = std::move(chunk);
        return ref;
    }
    void put(std::unique_ptr<Chunk> chunk) {
        const ChunkCoord c = chunk->getCoord();
        m_chunks[c] = std::move(chunk);
    }
    void erase(ChunkCoord c) { m_chunks.erase(c); }

private:
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> m_chunks;
};

struct Harness {
    MapChunkStore store;
    DecorationPipeline pipeline{SEED, store};
    std::vector<ChunkCoord> decorated;

    std::vector<ChunkCoord> load(ChunkCoord c) {
        store.generate(c);
        auto done = pipeline.onTerrainReady(c);
        decorated.insert(decorated.end(), done.begin(), done.end());
        return done;
    }
};

std::vector<ChunkCoord> grid(int32_t lo, int32_t hi) {
    std::vector<ChunkCoord> out;
    for (int32_t z = lo; z <= hi; ++z)
        for (int32_t x = lo; x <= hi; ++x) out.push_back(ChunkCoord{x, z});
    return out;
}

void shuffle(std::vector<ChunkCoord>& v, uint64_t seed) {
    SeededRandom rng(seed);
    for (size_t i = v.size() - 1; i > 0; --i) {
        std::swap(v[i], v[rng.nextBounded(static_cast<uint32_t>(i + 1))]);
    }
}

bool sameBlocks(const Chunk& a, const Chunk& b) {
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                if (a.getBlock(x, y, z) != b.getBlock(x, y, z)) return false;
    return true;
}

// Reference result computed without the pipeline: pass-1 chunk + every feature planned by
// any chunk of the 5x5 area whose water condition holds, rasterized directly. Water comes
// from the analytic fixture, independently of the pipeline's chunk probe.
const std::vector<FeaturePlan>& referencePlans() {
    static const std::vector<FeaturePlan> plans = [] {
        const FeaturePlanner planner(SEED);
        std::vector<FeaturePlan> out;
        for (ChunkCoord c : grid(-2, 2)) {
            Chunk pass1(c);
            test::fillDecorationTerrain(pass1);
            out.push_back(planner.plan(pass1));
        }
        return out;
    }();
    return plans;
}

std::unique_ptr<Chunk> referenceChunk(ChunkCoord target) {
    auto out = std::make_unique<Chunk>(target);
    test::fillDecorationTerrain(*out);
    for (const FeaturePlan& plan : referencePlans()) {
        for (const Feature& f : plan) {
            const bool holds = featureConditionHolds(f, [](int32_t wx, int32_t y, int32_t wz) {
                const test::DecorationColumn c = test::decorationColumn(wx, wz);
                return c.lake && y > c.surface && y <= test::SEA;
            });
            if (holds) (void)rasterizeFeature(f, *out);
        }
    }
    return out;
}

} // namespace

TEST(DecorationPipeline, ChunkWaitsForAllEightNeighbours) {
    Harness h;
    std::vector<ChunkCoord> order = grid(-1, 1);
    // Centre first, then 7 of its 8 neighbours.
    std::rotate(order.begin(), order.begin() + 4, order.begin() + 5);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_TRUE(h.load(order[i]).empty());
        EXPECT_FALSE(h.pipeline.isDecorated(ChunkCoord{0, 0}));
    }
    EXPECT_TRUE(h.pipeline.hasPlan(ChunkCoord{0, 0}));
    const auto done = h.load(order[8]);
    ASSERT_EQ(done.size(), 1U);
    EXPECT_EQ(done[0], (ChunkCoord{0, 0}));
    EXPECT_TRUE(h.pipeline.isDecorated(ChunkCoord{0, 0}));
}

TEST(DecorationPipeline, EachChunkIsDecoratedExactlyOnceAndOnlyWhenSurrounded) {
    Harness h;
    std::vector<ChunkCoord> order = grid(-2, 2);
    shuffle(order, 11ULL);
    for (ChunkCoord c : order) (void)h.load(c);

    std::set<std::pair<int32_t, int32_t>> unique;
    for (ChunkCoord c : h.decorated) {
        EXPECT_TRUE(unique.insert({c.x, c.z}).second) << "decorated twice: " << c.x << "," << c.z;
    }
    for (ChunkCoord c : grid(-2, 2)) {
        const bool inner = std::abs(c.x) <= 1 && std::abs(c.z) <= 1;
        EXPECT_EQ(h.pipeline.isDecorated(c), inner) << c.x << "," << c.z;
        EXPECT_TRUE(h.pipeline.hasPlan(c));
    }
    EXPECT_EQ(h.pipeline.plannedChunkCount(), 25U);
    EXPECT_EQ(h.pipeline.awaitingDecorationCount(), 16U);
}

TEST(DecorationPipeline, ResultIsIdenticalForPermutedLoadOrders) {
    std::vector<std::vector<ChunkCoord>> orders;
    orders.push_back(grid(-2, 2));
    orders.push_back(grid(-2, 2));
    std::reverse(orders.back().begin(), orders.back().end());
    for (uint64_t s : {1ULL, 2ULL, 3ULL, 0xC0FFEEULL}) {
        orders.push_back(grid(-2, 2));
        shuffle(orders.back(), s);
    }

    Harness first;
    for (ChunkCoord c : orders[0]) (void)first.load(c);
    for (size_t o = 1; o < orders.size(); ++o) {
        Harness h;
        for (ChunkCoord c : orders[o]) (void)h.load(c);
        for (ChunkCoord c : grid(-1, 1)) {
            ASSERT_TRUE(sameBlocks(*first.store.findChunk(c), *h.store.findChunk(c)))
                << "order " << o << " chunk " << c.x << "," << c.z;
        }
    }
}

TEST(DecorationPipeline, MatchesDirectWorldRasterizationReference) {
    Harness h;
    std::vector<ChunkCoord> order = grid(-2, 2);
    shuffle(order, 77ULL);
    for (ChunkCoord c : order) (void)h.load(c);
    for (ChunkCoord c : grid(-1, 1)) {
        EXPECT_TRUE(sameBlocks(*h.store.findChunk(c), *referenceChunk(c))) << c.x << "," << c.z;
    }
}

TEST(DecorationPipeline, TreesCrossingChunkBordersAreComplete) {
    Harness h;
    for (ChunkCoord c : grid(-2, 2)) (void)h.load(c);

    int crossing = 0;
    for (ChunkCoord c : grid(-1, 1)) {
        const FeaturePlan* plan = h.pipeline.findPlan(c);
        ASSERT_NE(plan, nullptr);
        for (const Feature& f : *plan) {
            if (!isTreeFeature(f.type)) continue;
            const FeatureBounds b = featureBounds(f);
            const bool crosses = floorDiv(b.min.x, Chunk::WIDTH) != floorDiv(b.max.x, Chunk::WIDTH) ||
                                 floorDiv(b.min.z, Chunk::DEPTH) != floorDiv(b.max.z, Chunk::DEPTH);
            if (crosses) ++crossing;
            forEachFeatureBlock(f, [&](BlockPos p, BlockId want) {
                const ChunkCoord owner{floorDiv(p.x, Chunk::WIDTH), floorDiv(p.z, Chunk::DEPTH)};
                if (!h.pipeline.isDecorated(owner)) return; // outer ring: not final yet
                const BlockId got = h.store.findChunk(owner)->getBlock(
                    floorMod(p.x, Chunk::WIDTH), p.y, floorMod(p.z, Chunk::DEPTH));
                // Either the wanted block, or something that legitimately dominates it.
                ASSERT_EQ(mergeDecoration(got, want), got)
                    << "tree block missing at " << p.x << "," << p.y << "," << p.z;
                if (want != BlockId::Dirt && want != BlockId::Leaves) {
                    ASSERT_EQ(got, want); // trunks are never cut
                }
            });
        }
    }
    EXPECT_GT(crossing, 0) << "fixture must contain border-crossing trees";
}

TEST(DecorationPipeline, DuplicateTerrainReadyIsIgnored) {
    Harness h;
    for (ChunkCoord c : grid(-1, 1)) (void)h.load(c);
    const FeaturePlan before = *h.pipeline.findPlan(ChunkCoord{0, 0});
    // Re-notifying a decorated chunk must neither re-plan from decorated data nor re-decorate.
    EXPECT_TRUE(h.pipeline.onTerrainReady(ChunkCoord{0, 0}).empty());
    EXPECT_EQ(*h.pipeline.findPlan(ChunkCoord{0, 0}), before);
}

TEST(DecorationPipeline, UnknownChunkIsIgnored) {
    MapChunkStore store;
    DecorationPipeline p(SEED, store);
    EXPECT_TRUE(p.onTerrainReady(ChunkCoord{4, 4}).empty());
    EXPECT_FALSE(p.hasPlan(ChunkCoord{4, 4}));
    EXPECT_FALSE(p.isDecorated(ChunkCoord{4, 4}));
    EXPECT_EQ(p.findPlan(ChunkCoord{4, 4}), nullptr);
}

TEST(DecorationPipeline, RegeneratingAnUnloadedNeighbourKeepsDecoratedChunksIntact) {
    Harness h;
    for (ChunkCoord c : grid(-1, 1)) (void)h.load(c);
    Chunk snapshot(ChunkCoord{0, 0});
    const Chunk& centre = *h.store.findChunk(ChunkCoord{0, 0});
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x) snapshot.setBlock(x, y, z, centre.getBlock(x, y, z), false);
    const FeaturePlan neighbourPlan = *h.pipeline.findPlan(ChunkCoord{1, 0});

    h.store.erase(ChunkCoord{1, 0});
    h.pipeline.onChunkUnloaded(ChunkCoord{1, 0});
    EXPECT_FALSE(h.pipeline.hasPlan(ChunkCoord{1, 0}));
    EXPECT_TRUE(h.pipeline.isDecorated(ChunkCoord{0, 0}));

    const auto done = h.load(ChunkCoord{1, 0});
    EXPECT_TRUE(std::find(done.begin(), done.end(), ChunkCoord{0, 0}) == done.end());
    EXPECT_EQ(*h.pipeline.findPlan(ChunkCoord{1, 0}), neighbourPlan); // deterministic re-plan
    EXPECT_TRUE(sameBlocks(snapshot, *h.store.findChunk(ChunkCoord{0, 0})));
}

TEST(DecorationPipeline, UnloadedNeighbourBlocksDecorationUntilItReturns) {
    Harness h;
    std::vector<ChunkCoord> all = grid(-1, 1);
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i] == ChunkCoord{0, 0} || all[i] == ChunkCoord{-1, 1}) continue;
        (void)h.load(all[i]);
    }
    // (-1,1) loads, then is unloaded before the centre arrives.
    (void)h.load(ChunkCoord{-1, 1});
    h.store.erase(ChunkCoord{-1, 1});
    h.pipeline.onChunkUnloaded(ChunkCoord{-1, 1});
    EXPECT_TRUE(h.load(ChunkCoord{0, 0}).empty());
    EXPECT_EQ(h.pipeline.awaitingDecorationCount(), 8U);

    const auto done = h.load(ChunkCoord{-1, 1});
    ASSERT_EQ(done.size(), 1U);
    EXPECT_EQ(done[0], (ChunkCoord{0, 0}));
    EXPECT_TRUE(sameBlocks(*h.store.findChunk(ChunkCoord{0, 0}), *referenceChunk(ChunkCoord{0, 0})));
}

TEST(DecorationPipeline, DecoratedChunkRestoredFromDiskContributesItsPlan) {
    // World A: everything generated normally.
    Harness a;
    for (ChunkCoord c : grid(-2, 2)) (void)a.load(c);

    // World B: (0,0) comes back from "disk" already decorated, with its saved plan.
    Harness b;
    auto restored = std::make_unique<Chunk>(ChunkCoord{0, 0});
    const Chunk& src = *a.store.findChunk(ChunkCoord{0, 0});
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x) restored->setBlock(x, y, z, src.getBlock(x, y, z), false);
    b.store.put(std::move(restored));
    EXPECT_TRUE(b.pipeline.onDecoratedChunkLoaded(ChunkCoord{0, 0}, *a.pipeline.findPlan(ChunkCoord{0, 0})).empty());
    EXPECT_TRUE(b.pipeline.isDecorated(ChunkCoord{0, 0}));

    std::vector<ChunkCoord> order = grid(-2, 2);
    order.erase(std::find(order.begin(), order.end(), ChunkCoord{0, 0}));
    shuffle(order, 5ULL);
    for (ChunkCoord c : order) (void)b.load(c);

    EXPECT_TRUE(std::find(b.decorated.begin(), b.decorated.end(), ChunkCoord{0, 0}) == b.decorated.end());
    for (ChunkCoord c : grid(-1, 1)) {
        EXPECT_TRUE(sameBlocks(*a.store.findChunk(c), *b.store.findChunk(c))) << c.x << "," << c.z;
    }
}

TEST(DecorationPipeline, RestoringAnAlreadyKnownChunkIsIgnored) {
    Harness h;
    (void)h.load(ChunkCoord{0, 0});
    const FeaturePlan plan = *h.pipeline.findPlan(ChunkCoord{0, 0});
    EXPECT_TRUE(h.pipeline.onDecoratedChunkLoaded(ChunkCoord{0, 0}, FeaturePlan{}).empty());
    EXPECT_EQ(*h.pipeline.findPlan(ChunkCoord{0, 0}), plan);
    EXPECT_FALSE(h.pipeline.isDecorated(ChunkCoord{0, 0}));
}

TEST(DecorationPipeline, DecorationIsGenerationOutputNotPlayerEdit) {
    Harness h;
    for (ChunkCoord c : grid(-1, 1)) (void)h.load(c);
    EXPECT_FALSE(h.store.findChunk(ChunkCoord{0, 0})->isModified());
    EXPECT_TRUE(h.store.findChunk(ChunkCoord{0, 0})->isMeshDirty());
}

TEST(DecorationPipeline, ExposesItsPlannerSeed) {
    MapChunkStore store;
    const DecorationPipeline p(SEED, store);
    EXPECT_EQ(p.planner().getSeed(), SEED);
}

// ---- Review fixes: stale registrations and edited frontier chunks ----

namespace {

std::unique_ptr<Chunk> copyOf(const Chunk& src) {
    auto out = std::make_unique<Chunk>(src.getCoord());
    *out = src;
    return out;
}

// Turns every grass / snow top into stone: a re-plan of this chunk would lose its trees.
void stripSurface(Chunk& c) {
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x) {
                const BlockId b = c.getBlock(x, y, z);
                if (b == BlockId::Grass || b == BlockId::SnowGrass || b == BlockId::Sand) {
                    c.setBlock(x, y, z, BlockId::Stone, true);
                }
            }
}

} // namespace

// World evicts a decorated chunk and regenerates pass 1 without telling the pipeline: the
// fresh, bare instance must still be decorated (it used to be silently ignored: no trees).
TEST(DecorationPipeline, RegeneratedDecoratedChunkWithoutUnloadIsRedecorated) {
    Harness h;
    for (ChunkCoord c : grid(-2, 2)) (void)h.load(c);
    ASSERT_TRUE(h.pipeline.isDecorated(ChunkCoord{0, 0}));
    h.store.generate(ChunkCoord{0, 0}); // fresh pass-1 instance replaces the decorated one
    const auto done = h.pipeline.onTerrainReady(ChunkCoord{0, 0});
    ASSERT_EQ(done.size(), 1U);
    EXPECT_EQ(done[0], (ChunkCoord{0, 0}));
    EXPECT_TRUE(sameBlocks(*h.store.findChunk(ChunkCoord{0, 0}), *referenceChunk(ChunkCoord{0, 0})));
}

// An undecorated frontier chunk whose plan was already consumed by a decorated neighbour is
// edited, unloaded and reloaded: it must keep its ORIGINAL plan, or the neighbour's leaves
// would hang next to a trunk that never grows (the P3.4 border glitch, reintroduced).
TEST(DecorationPipeline, EditedFrontierChunkReloadedAfterUnloadKeepsItsOriginalPlan) {
    Harness h;
    for (ChunkCoord c : grid(-1, 1)) (void)h.load(c);
    const ChunkCoord frontier{1, 0};
    ASSERT_TRUE(h.pipeline.isDecorated(ChunkCoord{0, 0}));
    ASSERT_FALSE(h.pipeline.isDecorated(frontier));
    const FeaturePlan original = *h.pipeline.findPlan(frontier);
    ASSERT_FALSE(original.empty());

    auto edited = copyOf(*h.store.findChunk(frontier));
    stripSurface(*edited);
    h.store.erase(frontier);
    h.pipeline.onChunkUnloaded(frontier);
    EXPECT_EQ(h.pipeline.retainedPlanCount(), 1U);

    h.store.put(std::move(edited));
    (void)h.pipeline.onTerrainReady(frontier);
    ASSERT_NE(h.pipeline.findPlan(frontier), nullptr);
    EXPECT_EQ(*h.pipeline.findPlan(frontier), original);
    EXPECT_EQ(h.pipeline.retainedPlanCount(), 0U);
}

TEST(DecorationPipeline, UnconsumedPlansAreNotRetainedOnUnload) {
    Harness h;
    (void)h.load(ChunkCoord{7, 7}); // no neighbours: nobody consumed its plan
    h.store.erase(ChunkCoord{7, 7});
    h.pipeline.onChunkUnloaded(ChunkCoord{7, 7});
    EXPECT_EQ(h.pipeline.retainedPlanCount(), 0U);
    // Decorated chunks are not retained either: their result is on disk.
    for (ChunkCoord c : grid(-1, 1)) (void)h.load(c);
    h.store.erase(ChunkCoord{0, 0});
    h.pipeline.onChunkUnloaded(ChunkCoord{0, 0});
    EXPECT_EQ(h.pipeline.retainedPlanCount(), 0U);
}

// Cross-session variant: the World saved an undecorated chunk together with its plan.
TEST(DecorationPipeline, UndecoratedChunkRestoredWithItsSavedPlanUsesThatPlan) {
    Harness a;
    for (ChunkCoord c : grid(-1, 1)) (void)a.load(c);
    const ChunkCoord frontier{1, 0};
    const FeaturePlan saved = *a.pipeline.findPlan(frontier);

    Harness b;
    auto edited = copyOf(*a.store.findChunk(frontier));
    stripSurface(*edited);
    b.store.put(std::move(edited));
    EXPECT_TRUE(b.pipeline.onUndecoratedChunkLoaded(frontier, saved).empty());
    EXPECT_FALSE(b.pipeline.isDecorated(frontier));
    ASSERT_NE(b.pipeline.findPlan(frontier), nullptr);
    EXPECT_EQ(*b.pipeline.findPlan(frontier), saved);
    // Known chunks are not re-registered.
    EXPECT_TRUE(b.pipeline.onUndecoratedChunkLoaded(frontier, FeaturePlan{}).empty());
    EXPECT_EQ(*b.pipeline.findPlan(frontier), saved);
}

// World persists every edited chunk with findPlan() and regenerates unedited ones to the same
// pass-1 terrain, so it can restore any plan itself: a retained copy would only leak memory
// along the streaming frontier.
TEST(DecorationPipeline, PlansTheCallerRestoresAreNotRetainedOnUnload) {
    Harness h;
    for (ChunkCoord c : grid(-1, 1)) (void)h.load(c);
    const ChunkCoord frontier{1, 0};
    ASSERT_FALSE(h.pipeline.isDecorated(frontier));
    const FeaturePlan original = *h.pipeline.findPlan(frontier);

    h.store.erase(frontier);
    h.pipeline.onChunkUnloaded(frontier, PlanRetention::CallerRestores);
    EXPECT_FALSE(h.pipeline.hasPlan(frontier));
    EXPECT_EQ(h.pipeline.retainedPlanCount(), 0U);

    // The unedited chunk regenerates and re-plans to the very same plan.
    (void)h.load(frontier);
    ASSERT_NE(h.pipeline.findPlan(frontier), nullptr);
    EXPECT_EQ(*h.pipeline.findPlan(frontier), original);
}

TEST(DecorationPipeline, CallerRestoresAlsoDropsAPlanRetainedEarlier) {
    Harness h;
    for (ChunkCoord c : grid(-1, 1)) (void)h.load(c);
    const ChunkCoord frontier{1, 0};
    h.store.erase(frontier);
    h.pipeline.onChunkUnloaded(frontier); // default: retain
    ASSERT_EQ(h.pipeline.retainedPlanCount(), 1U);
    h.pipeline.onChunkUnloaded(frontier, PlanRetention::CallerRestores);
    EXPECT_EQ(h.pipeline.retainedPlanCount(), 0U);
}
