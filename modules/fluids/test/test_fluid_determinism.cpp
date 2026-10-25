// Determinism and randomized property tests (fixed seeds) on random terrain.
#include "FluidTestUtils.hpp"

#include <gtest/gtest.h>

#include <random>
#include <vector>

using namespace fluidtest;
using betacraft::fluids::FluidEvent;

namespace {

constexpr int32_t kSX = 20;
constexpr int32_t kSY = 8;
constexpr int32_t kSZ = 20;

// Random heightmap of stone (0..3 blocks above the floor), a few pits through the floor
// (the grid edge below acts as a wall) and scattered plants, all derived from `seed`.
std::unique_ptr<GridFluidVoxelAccess> makeRandomTerrain(uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> height(0, 3);
    std::uniform_int_distribution<int> percent(0, 99);
    auto g = std::make_unique<GridFluidVoxelAccess>(kSX, kSY, kSZ);
    for (int32_t x = 0; x < kSX; ++x) {
        for (int32_t z = 0; z < kSZ; ++z) {
            const int roll = percent(rng);
            if (roll < 8) {
                continue; // pit: no floor at all
            }
            const int h = height(rng);
            g->fill({x, 0, z}, {x, h, z}, BlockId::Stone);
            if (roll > 92 && h + 1 < kSY) {
                g->setBlock({x, h + 1, z}, BlockId::TallGrass);
            }
        }
    }
    return g;
}

struct SourceSpec {
    BlockPos pos;
    FluidType type;
};

std::vector<SourceSpec> randomSources(uint32_t seed, int count, bool allowLava) {
    std::mt19937 rng(seed ^ 0x9E3779B9U);
    std::uniform_int_distribution<int32_t> px(0, kSX - 1);
    std::uniform_int_distribution<int32_t> py(4, kSY - 1);
    std::uniform_int_distribution<int32_t> pz(0, kSZ - 1);
    std::uniform_int_distribution<int> coin(0, 3);
    std::vector<SourceSpec> out;
    for (int i = 0; i < count; ++i) {
        const FluidType t = allowLava && coin(rng) == 0 ? FluidType::Lava : FluidType::Water;
        out.push_back({{px(rng), py(rng), pz(rng)}, t});
    }
    return out;
}

// First air/plant cell above the terrain column, so a source placed there rests on the
// ground instead of falling (a falling column never counts as a source).
BlockPos onSurface(const GridFluidVoxelAccess& g, BlockPos p) {
    int32_t y = kSY - 1;
    while (y > 0 && !g.blocksFlow({p.x, y - 1, p.z})) {
        --y;
    }
    return {p.x, y, p.z};
}

int decayOf(const GridFluidVoxelAccess& g, const BlockPos& p, FluidType type) {
    const FluidState f = g.getFluid(p);
    return f.type == type ? f.betaDecay() : -1;
}

// Invariants a *settled* simulation must satisfy, each of which a simulator bug can break
// (the grid itself enforces none of them):
//  - falling fluid is fed by the same fluid directly above it, and non-source fluid
//    with the same fluid above it is falling;
//  - a flowing cell's level is exactly what its strongest horizontal neighbour gives it
//    (falling neighbours feed like sources), i.e. no orphaned or mis-levelled cells;
//  - no lava thick enough to harden (decay <= 4) is left touching water above/beside.
void checkSettledInvariants(const GridFluidVoxelAccess& g, const FluidConfig& cfg) {
    static constexpr BlockPos kSides[4] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}};
    for (int32_t y = 0; y < kSY; ++y) {
        for (int32_t z = 0; z < kSZ; ++z) {
            for (int32_t x = 0; x < kSX; ++x) {
                const BlockPos p{x, y, z};
                const FluidState f = g.getFluid(p);
                if (f.isEmpty()) {
                    continue;
                }
                if (f.isFalling()) {
                    ASSERT_EQ(g.getFluid(p.offset(0, 1, 0)).type, f.type) << "unfed falling fluid at " << x << ',' << y
                                                                          << ',' << z;
                    continue;
                }
                if (f.type == FluidType::Lava && f.betaDecay() <= 4) {
                    for (const BlockPos& d : kSides) {
                        ASSERT_NE(g.getFluid(p.offset(d.x, 0, d.z)).type, FluidType::Water) << "unhardened lava";
                    }
                    ASSERT_NE(g.getFluid(p.offset(0, 1, 0)).type, FluidType::Water) << "unhardened lava";
                }
                if (f.isSource()) {
                    continue; // sources never recompute, even under more fluid
                }
                ASSERT_NE(g.getFluid(p.offset(0, 1, 0)).type, f.type) << "flowing fluid under fluid must be falling";
                int smallest = -1;
                for (const BlockPos& d : kSides) {
                    int n = decayOf(g, p.offset(d.x, 0, d.z), f.type);
                    if (n >= FluidState::kFallingDecay) {
                        n = 0;
                    }
                    if (n >= 0 && (smallest < 0 || n < smallest)) {
                        smallest = n;
                    }
                }
                const int step = f.type == FluidType::Lava ? cfg.lavaDecayStep : cfg.waterDecayStep;
                ASSERT_GE(smallest, 0) << "orphaned flowing cell at " << x << ',' << y << ',' << z;
                ASSERT_EQ(f.betaDecay(), smallest + step) << "mis-levelled cell at " << x << ',' << y << ',' << z;
            }
        }
    }
}

// FNV-1a over every cell (block id, fluid type, nibble): a platform-independent digest
// for golden-snapshot regression tests.
uint64_t digest(const GridFluidVoxelAccess& g) {
    uint64_t h = 0xCBF29CE484222325ULL;
    const auto add = [&h](uint64_t v) {
        h ^= v;
        h *= 0x100000001B3ULL;
    };
    for (int32_t y = 0; y < kSY; ++y) {
        for (int32_t z = 0; z < kSZ; ++z) {
            for (int32_t x = 0; x < kSX; ++x) {
                const FluidState f = g.getFluid({x, y, z});
                add(f.isEmpty() ? static_cast<uint64_t>(g.getBlock({x, y, z})) : 0x100U);
                add(static_cast<uint64_t>(f.type));
                add(f.toNibble());
            }
        }
    }
    return h;
}

} // namespace

TEST(FluidDeterminism, IdenticalRunsMatchTickByTickIncludingEvents) {
    for (const uint32_t seed : {1U, 7U, 42U, 1234U}) {
        auto a = makeRandomTerrain(seed);
        auto b = makeRandomTerrain(seed);
        ASSERT_TRUE(a->sameContents(*b));
        FluidConfig cfg{};
        cfg.maxUpdatesPerTick = 40; // force budget carry-over to be part of the ordering
        FluidSimulator sa(*a, cfg);
        FluidSimulator sb(*b, cfg);
        for (const SourceSpec& s : randomSources(seed, 6, true)) {
            sa.placeSource(s.pos, s.type);
            sb.placeSource(s.pos, s.type);
        }
        for (int t = 0; t < 3000 && sa.pendingUpdates() > 0; ++t) {
            const auto ra = sa.tick();
            const auto rb = sb.tick();
            ASSERT_EQ(ra.processed, rb.processed) << "seed " << seed << " tick " << t;
            ASSERT_TRUE(a->sameContents(*b)) << "seed " << seed << " diverged at tick " << t;
        }
        EXPECT_EQ(sa.takeEvents(), sb.takeEvents());
        EXPECT_EQ(sb.pendingUpdates(), 0U);
    }
}

TEST(FluidProperties, RandomTerrainConvergesAndRespectsInvariants) {
    for (uint32_t seed = 100; seed < 116; ++seed) {
        auto g = makeRandomTerrain(seed);
        FluidSimulator sim(*g);
        for (const SourceSpec& s : randomSources(seed, 5, true)) {
            sim.placeSource(s.pos, s.type);
        }
        uint64_t ticks = 0;
        while (ticks < 20000 && sim.pendingUpdates() > 0) {
            sim.tick();
            ++ticks;
        }
        EXPECT_EQ(sim.pendingUpdates(), 0U) << "seed " << seed << " never settled";
        EXPECT_GT(g->countFluid(), 20U) << "seed " << seed << ": scenario too trivial to mean anything";
        checkSettledInvariants(*g, sim.config());
    }
}

TEST(FluidProperties, SettledStateIsAFixedPointWhenEveryCellIsWoken) {
    for (uint32_t seed = 200; seed < 212; ++seed) {
        auto g = makeRandomTerrain(seed);
        FluidSimulator sim(*g);
        for (const SourceSpec& s : randomSources(seed, 4, false)) {
            sim.placeSource(s.pos, s.type);
        }
        sim.runUntilIdle(20000);
        ASSERT_EQ(sim.pendingUpdates(), 0U);
        const auto snapshot = g->clone();
        for (int32_t y = 0; y < kSY; ++y) {
            for (int32_t z = 0; z < kSZ; ++z) {
                for (int32_t x = 0; x < kSX; ++x) {
                    sim.scheduleUpdate({x, y, z});
                }
            }
        }
        sim.runUntilIdle(20000);
        EXPECT_TRUE(g->sameContents(*snapshot)) << "seed " << seed << "\n" << dumpLayer(*g, 4);
    }
}

TEST(FluidProperties, RemovingTheOnlySourceAlwaysDrainsToEmpty) {
    for (uint32_t seed = 300; seed < 316; ++seed) {
        auto g = makeRandomTerrain(seed);
        FluidSimulator sim(*g);
        const SourceSpec src = randomSources(seed, 1, false).front();
        sim.placeSource(src.pos, FluidType::Water);
        sim.runUntilIdle(20000);
        ASSERT_EQ(sim.pendingUpdates(), 0U);
        ASSERT_GT(g->countFluid(), 10U) << "seed " << seed << ": nothing to retract";
        sim.removeFluid(src.pos);
        sim.runUntilIdle(20000);
        EXPECT_EQ(sim.pendingUpdates(), 0U);
        EXPECT_EQ(g->countFluid(), 0U) << "seed " << seed << " left residual water";
    }
}

TEST(FluidProperties, SingleWaterSourceNeverMultiplies) {
    for (uint32_t seed = 400; seed < 416; ++seed) {
        auto g = makeRandomTerrain(seed);
        FluidSimulator sim(*g);
        const BlockPos src = onSurface(*g, randomSources(seed, 1, false).front().pos);
        ASSERT_TRUE(g->blocksFlow(src.offset(0, -1, 0))) << "seed " << seed << ": source must rest on ground";
        sim.placeSource(src, FluidType::Water);
        sim.runUntilIdle(20000);
        size_t sources = 0;
        for (int32_t y = 0; y < kSY; ++y) {
            for (int32_t z = 0; z < kSZ; ++z) {
                for (int32_t x = 0; x < kSX; ++x) {
                    sources += g->getFluid({x, y, z}).isSource() ? 1U : 0U;
                }
            }
        }
        EXPECT_EQ(sources, 1U) << "seed " << seed;
    }
}

TEST(FluidProperties, BudgetDoesNotChangeTheSettledResultOnRandomTerrain) {
    for (uint32_t seed = 500; seed < 508; ++seed) {
        auto ref = makeRandomTerrain(seed);
        auto lim = makeRandomTerrain(seed);
        FluidSimulator sr(*ref);
        FluidConfig cfg{};
        cfg.maxUpdatesPerTick = 3;
        FluidSimulator sl(*lim, cfg);
        // Water only: lava/water contact outcomes legitimately depend on arrival timing.
        const SourceSpec src = randomSources(seed, 1, false).front();
        sr.placeSource(src.pos, FluidType::Water);
        sl.placeSource(src.pos, FluidType::Water);
        sr.runUntilIdle(50000);
        sl.runUntilIdle(200000);
        ASSERT_EQ(sl.pendingUpdates(), 0U);
        EXPECT_TRUE(lim->sameContents(*ref)) << "seed " << seed;
    }
}

TEST(FluidDeterminism, GoldenSnapshotOfMixedScenario) {
    // Pins the exact outcome (grid digest, ticks to settle, events) of one water + lava
    // scenario, spread then retraction (which exercises the seeded lava-thinning hash), so
    // cross-platform, cross-compiler or unintended rule drift is caught, not only
    // run-to-run divergence. An *intended* rule change must update these values.
    auto g = makeRandomTerrain(15U);
    FluidConfig cfg{};
    cfg.maxUpdatesPerTick = 40;
    cfg.randomSeed = 7;
    FluidSimulator sim(*g, cfg);
    const std::vector<SourceSpec> sources = randomSources(15U, 6, true);
    for (const SourceSpec& s : sources) {
        sim.placeSource(s.pos, s.type);
    }
    const uint64_t spreadTicks = sim.runUntilIdle(20000);
    ASSERT_EQ(sim.pendingUpdates(), 0U);
    const size_t spreadEvents = sim.takeEvents().size();
    const uint64_t spreadDigest = digest(*g);
    const size_t spreadWater = g->countFluid(FluidType::Water);
    const size_t spreadLava = g->countFluid(FluidType::Lava);
    for (const SourceSpec& s : sources) {
        sim.removeFluid(s.pos);
    }
    const uint64_t drainTicks = sim.runUntilIdle(20000);
    ASSERT_EQ(sim.pendingUpdates(), 0U);
    EXPECT_EQ(spreadDigest, 0x510DD63C96D85B06ULL);
    EXPECT_EQ(spreadTicks, 960U);
    EXPECT_EQ(spreadEvents, 41U);
    EXPECT_EQ(spreadWater, 29U);
    EXPECT_EQ(spreadLava, 195U);
    // Drain duration depends on every seeded lava-thinning decision.
    EXPECT_EQ(drainTicks, 1860U);
    EXPECT_EQ(g->countFluid(), 0U);
    EXPECT_EQ(digest(*g), 0xEB46D803E56E2E1FULL) << "hardened blocks left behind";
}
