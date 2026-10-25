// Feature planning (pure function of seed + pass-1 chunk) and clipped rasterization.
#include "worldgen/DecorationFeatures.hpp"
#include "worldgen/SeededRandom.hpp"
#include "WorldgenTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

using namespace betacraft::worldgen;
using betacraft::core::BlockPos;
using betacraft::core::ChunkCoord;
using betacraft::core::floorDiv;
using betacraft::core::floorMod;
using betacraft::voxel::BiomeId;
using betacraft::voxel::BlockId;
using betacraft::voxel::Chunk;

namespace {

constexpr uint64_t SEED = 173173173ULL;

std::unique_ptr<Chunk> makeChunk(ChunkCoord cc) {
    auto c = std::make_unique<Chunk>(cc);
    test::fillDecorationTerrain(*c);
    return c;
}

bool isTree(FeatureType t) {
    return t == FeatureType::OakTree || t == FeatureType::BirchTree || t == FeatureType::PineTree;
}

bool samePlan(const FeaturePlan& a, const FeaturePlan& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!(a[i] == b[i])) return false;
    }
    return true;
}

Feature oakAt(BlockPos origin, uint8_t height = 5, uint32_t variant = 0x5A5AU) {
    return Feature{FeatureType::OakTree, origin, height, variant};
}

} // namespace

// ---------------------------------------------------------------- planning

TEST(FeaturePlanner, PlanIsDeterministicForSameSeedAndTerrain) {
    const FeaturePlanner a(SEED);
    const FeaturePlanner b(SEED);
    for (ChunkCoord cc : {ChunkCoord{0, 0}, ChunkCoord{-2, 3}, ChunkCoord{7, -5}}) {
        const auto chunk = makeChunk(cc);
        EXPECT_TRUE(samePlan(a.plan(*chunk), b.plan(*chunk)));
        EXPECT_TRUE(samePlan(a.plan(*chunk), a.plan(*chunk)));
    }
}

TEST(FeaturePlanner, DifferentSeedsGiveDifferentPlans) {
    const auto chunk = makeChunk(ChunkCoord{0, 0});
    EXPECT_FALSE(samePlan(FeaturePlanner(1ULL).plan(*chunk), FeaturePlanner(2ULL).plan(*chunk)));
}

TEST(FeaturePlanner, PlanDoesNotDependOnPreviouslyPlannedChunks) {
    const auto target = makeChunk(ChunkCoord{1, 1});
    const FeaturePlanner fresh(SEED);
    const FeaturePlan ref = fresh.plan(*target);

    const FeaturePlanner used(SEED);
    for (int32_t i = -3; i <= 3; ++i) (void)used.plan(*makeChunk(ChunkCoord{i, -i}));
    EXPECT_TRUE(samePlan(used.plan(*target), ref));
}

TEST(FeaturePlanner, PlanIsSortedCanonically) {
    const FeaturePlanner p(SEED);
    const FeaturePlan plan = p.plan(*makeChunk(ChunkCoord{0, 0}));
    ASSERT_FALSE(plan.empty());
    EXPECT_TRUE(std::is_sorted(plan.begin(), plan.end()));
}

TEST(FeaturePlanner, FeaturesOriginateInsideTheirChunkAndStayWithinOverhang) {
    const FeaturePlanner p(SEED);
    for (int32_t cx = -4; cx <= 4; ++cx) {
        for (int32_t cz = -4; cz <= 4; ++cz) {
            const auto chunk = makeChunk(ChunkCoord{cx, cz});
            const int32_t minX = cx * Chunk::WIDTH;
            const int32_t minZ = cz * Chunk::DEPTH;
            for (const Feature& f : p.plan(*chunk)) {
                ASSERT_EQ(floorDiv(f.origin.x, Chunk::WIDTH), cx);
                ASSERT_EQ(floorDiv(f.origin.z, Chunk::DEPTH), cz);
                const FeatureBounds b = featureBounds(f);
                ASSERT_GE(b.min.x, minX - MAX_FEATURE_OVERHANG);
                ASSERT_LE(b.max.x, minX + Chunk::WIDTH - 1 + MAX_FEATURE_OVERHANG);
                ASSERT_GE(b.min.z, minZ - MAX_FEATURE_OVERHANG);
                ASSERT_LE(b.max.z, minZ + Chunk::DEPTH - 1 + MAX_FEATURE_OVERHANG);
                ASSERT_GE(b.min.y, 0);
                ASSERT_LT(b.max.y, Chunk::HEIGHT);
            }
        }
    }
}

// Regression for the border glitch: the legacy generator only planted trees at lx/lz in
// [2, 13], leaving 2-block treeless corridors along every chunk border.
TEST(FeaturePlanner, TreesArePlantedOnChunkBorderColumnsToo) {
    const FeaturePlanner p(SEED);
    int borderTrees = 0;
    int trees = 0;
    for (int32_t cx = -4; cx <= 4; ++cx) {
        for (int32_t cz = -4; cz <= 4; ++cz) {
            for (const Feature& f : p.plan(*makeChunk(ChunkCoord{cx, cz}))) {
                if (!isTree(f.type)) continue;
                ++trees;
                const int32_t lx = floorMod(f.origin.x, Chunk::WIDTH);
                const int32_t lz = floorMod(f.origin.z, Chunk::DEPTH);
                if (lx < 2 || lx > 13 || lz < 2 || lz > 13) ++borderTrees;
            }
        }
    }
    EXPECT_GT(trees, 30);
    EXPECT_GT(borderTrees, 5);
}

TEST(FeaturePlanner, FeaturesRespectBiomeAndSurfaceRules) {
    const FeaturePlanner p(SEED);
    bool sawPine = false;
    bool sawBroadleaf = false;
    bool sawDesertFlora = false;
    for (int32_t cx = -5; cx <= 5; ++cx) {
        for (int32_t cz = -5; cz <= 5; ++cz) {
            const auto chunk = makeChunk(ChunkCoord{cx, cz});
            for (const Feature& f : p.plan(*chunk)) {
                const int32_t lx = floorMod(f.origin.x, Chunk::WIDTH);
                const int32_t lz = floorMod(f.origin.z, Chunk::DEPTH);
                const BlockId support = chunk->getBlock(lx, f.origin.y - 1, lz);
                const BiomeId biome = chunk->getColumnBiome(lx, lz);
                // Feature base sits directly on the surface, in pass-1 air.
                ASSERT_EQ(chunk->getBlock(lx, f.origin.y, lz), BlockId::Air);
                ASSERT_NE(support, BlockId::Air);
                switch (f.type) {
                    case FeatureType::PineTree:
                        ASSERT_EQ(biome, BiomeId::Taiga);
                        ASSERT_TRUE(support == BlockId::Grass || support == BlockId::SnowGrass);
                        sawPine = true;
                        break;
                    case FeatureType::OakTree:
                    case FeatureType::BirchTree:
                        ASSERT_NE(biome, BiomeId::Taiga);
                        ASSERT_TRUE(support == BlockId::Grass || support == BlockId::SnowGrass);
                        sawBroadleaf = true;
                        break;
                    case FeatureType::Cactus:
                    case FeatureType::DeadBush:
                        ASSERT_EQ(biome, BiomeId::Desert);
                        ASSERT_EQ(support, BlockId::Sand);
                        sawDesertFlora = true;
                        break;
                    case FeatureType::TallGrass:
                    case FeatureType::Dandelion:
                    case FeatureType::Rose:
                        ASSERT_EQ(support, BlockId::Grass);
                        break;
                    case FeatureType::SugarCane:
                        ASSERT_TRUE(support == BlockId::Sand || support == BlockId::Grass);
                        break;
                }
                if (isTree(f.type)) {
                    ASSERT_GE(f.height, 4);
                    ASSERT_LE(f.height, 6);
                }
            }
        }
    }
    EXPECT_TRUE(sawPine);
    EXPECT_TRUE(sawBroadleaf);
    EXPECT_TRUE(sawDesertFlora);
}

TEST(FeaturePlanner, NoFeaturesOnBareStoneOrEmptyChunks) {
    const FeaturePlanner p(SEED);
    Chunk empty(0, 0);
    EXPECT_TRUE(p.plan(empty).empty());

    Chunk stone(3, 3);
    for (int32_t z = 0; z < Chunk::DEPTH; ++z)
        for (int32_t x = 0; x < Chunk::WIDTH; ++x)
            for (int32_t y = 0; y <= 70; ++y) stone.setBlock(x, y, z, BlockId::Stone, false);
    EXPECT_TRUE(p.plan(stone).empty());
}

TEST(FeaturePlanner, NoTreeWhenTrunkSpaceIsObstructedOrTooHigh) {
    const FeaturePlanner p(SEED);
    // Grass at Y=124: a tree would exceed the build height.
    Chunk high(0, 0);
    for (int32_t z = 0; z < Chunk::DEPTH; ++z)
        for (int32_t x = 0; x < Chunk::WIDTH; ++x) {
            high.setColumnBiome(x, z, BiomeId::Forest, glm::vec3(0.5f), glm::vec3(0.5f));
            high.setBlock(x, 124, z, BlockId::Grass, false);
        }
    for (const Feature& f : p.plan(high)) ASSERT_FALSE(isTree(f.type));

    // Overhang 2 blocks above the grass blocks every trunk.
    auto roofed = makeChunk(ChunkCoord{0, 0});
    for (int32_t z = 0; z < Chunk::DEPTH; ++z)
        for (int32_t x = 0; x < Chunk::WIDTH; ++x)
            roofed->setBlock(x, test::decorationSurface(x, z) + 3, z, BlockId::Stone, false);
    for (const Feature& f : p.plan(*roofed)) ASSERT_FALSE(isTree(f.type));
}

// ---------------------------------------------------------------- feature geometry

TEST(FeatureGeometry, OakTreeHasTrunkDirtAndCanopy) {
    const Feature f = oakAt(BlockPos{8, 70, 8}, 5);
    std::map<std::tuple<int32_t, int32_t, int32_t>, BlockId> blocks;
    forEachFeatureBlock(f, [&](BlockPos p, BlockId b) { blocks[{p.x, p.y, p.z}] = b; });

    for (int32_t y = 70; y < 75; ++y) EXPECT_EQ((blocks[{8, y, 8}]), BlockId::OakLog);
    EXPECT_EQ((blocks[{8, 69, 8}]), BlockId::Dirt);
    int leaves = 0;
    for (const auto& [pos, b] : blocks) {
        if (b == BlockId::Leaves) ++leaves;
    }
    EXPECT_GT(leaves, 30);
    const FeatureBounds bounds = featureBounds(f);
    for (const auto& [pos, b] : blocks) {
        EXPECT_GE(std::get<0>(pos), bounds.min.x);
        EXPECT_LE(std::get<0>(pos), bounds.max.x);
        EXPECT_GE(std::get<1>(pos), bounds.min.y);
        EXPECT_LE(std::get<1>(pos), bounds.max.y);
        EXPECT_GE(std::get<2>(pos), bounds.min.z);
        EXPECT_LE(std::get<2>(pos), bounds.max.z);
    }
}

TEST(FeatureGeometry, EveryFeatureTypeEmitsBlocksInsideItsBounds) {
    for (FeatureType t : {FeatureType::OakTree, FeatureType::BirchTree, FeatureType::PineTree,
                          FeatureType::TallGrass, FeatureType::Dandelion, FeatureType::Rose,
                          FeatureType::DeadBush, FeatureType::Cactus, FeatureType::SugarCane}) {
        const Feature f{t, BlockPos{-3, 66, 17}, 3, 0x1234U};
        const FeatureBounds b = featureBounds(f);
        int n = 0;
        forEachFeatureBlock(f, [&](BlockPos p, BlockId) {
            ++n;
            EXPECT_TRUE(p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y &&
                        p.z >= b.min.z && p.z <= b.max.z) << static_cast<int>(t);
        });
        EXPECT_GT(n, 0) << static_cast<int>(t);
    }
}

// ---------------------------------------------------------------- merge rule

TEST(DecorationMerge, PriorityRules) {
    EXPECT_EQ(mergeDecoration(BlockId::Air, BlockId::Leaves), BlockId::Leaves);
    EXPECT_EQ(mergeDecoration(BlockId::Leaves, BlockId::OakLog), BlockId::OakLog);
    EXPECT_EQ(mergeDecoration(BlockId::OakLog, BlockId::Leaves), BlockId::OakLog);
    EXPECT_EQ(mergeDecoration(BlockId::TallGrass, BlockId::Leaves), BlockId::Leaves);
    EXPECT_EQ(mergeDecoration(BlockId::Leaves, BlockId::TallGrass), BlockId::Leaves);
    EXPECT_EQ(mergeDecoration(BlockId::Stone, BlockId::Leaves), BlockId::Stone);   // terrain wins
    EXPECT_EQ(mergeDecoration(BlockId::Water, BlockId::OakLog), BlockId::Water);   // never into water
    EXPECT_EQ(mergeDecoration(BlockId::Grass, BlockId::Dirt), BlockId::Dirt);      // soil under trunk
    EXPECT_EQ(mergeDecoration(BlockId::SnowGrass, BlockId::Dirt), BlockId::Dirt);
    EXPECT_EQ(mergeDecoration(BlockId::Sand, BlockId::Dirt), BlockId::Sand);
    EXPECT_EQ(mergeDecoration(BlockId::Air, BlockId::Dirt), BlockId::Air);         // dirt never floats
}

// Property: folding any multiset of proposals with mergeDecoration is order independent.
TEST(DecorationMerge, IsCommutativeAndAssociativeOverRandomSequences) {
    const std::array<BlockId, 12> pool{BlockId::Air, BlockId::Leaves, BlockId::OakLog, BlockId::BirchLog,
                                       BlockId::PineLog, BlockId::TallGrass, BlockId::Rose, BlockId::Dandelion,
                                       BlockId::Cactus, BlockId::Dirt, BlockId::SugarCane, BlockId::DeadBush};
    const std::array<BlockId, 4> bases{BlockId::Air, BlockId::Grass, BlockId::Stone, BlockId::Leaves};
    SeededRandom rng(4242ULL);
    for (int trial = 0; trial < 2000; ++trial) {
        std::vector<BlockId> seq(1 + rng.nextBounded(6U));
        for (auto& b : seq) b = pool[rng.nextBounded(static_cast<uint32_t>(pool.size()))];
        const BlockId base = bases[rng.nextBounded(4U)];
        auto fold = [&](const std::vector<BlockId>& s) {
            BlockId v = base;
            for (BlockId b : s) v = mergeDecoration(v, b);
            return v;
        };
        const BlockId ref = fold(seq);
        std::sort(seq.begin(), seq.end());
        do {
            ASSERT_EQ(fold(seq), ref) << "trial " << trial;
        } while (std::next_permutation(seq.begin(), seq.end()));
    }
}

// ---------------------------------------------------------------- clipped rasterization

TEST(FeatureRasterizer, TreeOnChunkCornerIsCompleteAcrossFourChunks) {
    // Trunk at world (15, 70, 15): the canopy spills into chunks (1,0), (0,1) and (1,1).
    const Feature f = oakAt(BlockPos{15, 70, 15}, 5);
    std::map<std::pair<int32_t, int32_t>, std::unique_ptr<Chunk>> chunks;
    for (int32_t cx = 0; cx <= 1; ++cx)
        for (int32_t cz = 0; cz <= 1; ++cz) {
            auto c = std::make_unique<Chunk>(cx, cz);
            for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                for (int32_t x = 0; x < Chunk::WIDTH; ++x) c->setBlock(x, 69, z, BlockId::Grass, false);
            chunks[{cx, cz}] = std::move(c);
        }
    uint32_t written = 0;
    for (auto& [cc, c] : chunks) written += rasterizeFeature(f, *c);

    uint32_t expected = 0;
    forEachFeatureBlock(f, [&](BlockPos p, BlockId b) {
        ++expected;
        const Chunk& c = *chunks.at({floorDiv(p.x, Chunk::WIDTH), floorDiv(p.z, Chunk::DEPTH)});
        EXPECT_EQ(c.getBlock(floorMod(p.x, Chunk::WIDTH), p.y, floorMod(p.z, Chunk::DEPTH)), b)
            << p.x << "," << p.y << "," << p.z;
    });
    EXPECT_EQ(written, expected);
    // Each of the four chunks received part of the tree.
    for (auto& [cc, c] : chunks) {
        int leaves = 0;
        for (int32_t y = 70; y < 78; ++y)
            for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                    if (c->getBlock(x, y, z) == BlockId::Leaves) ++leaves;
        EXPECT_GT(leaves, 0) << cc.first << "," << cc.second;
    }
}

TEST(FeatureRasterizer, IgnoresFeaturesThatDoNotTouchTheChunk) {
    Chunk c(5, 5);
    EXPECT_EQ(rasterizeFeature(oakAt(BlockPos{0, 70, 0}), c), 0U);
    EXPECT_TRUE(featureIntersectsChunk(oakAt(BlockPos{80, 70, 80}), ChunkCoord{5, 5}));
    EXPECT_FALSE(featureIntersectsChunk(oakAt(BlockPos{0, 70, 0}), ChunkCoord{5, 5}));
}

TEST(FeatureRasterizer, NeverOverwritesTerrainAndDoesNotFlagModification) {
    Chunk c(0, 0);
    for (int32_t z = 0; z < Chunk::DEPTH; ++z)
        for (int32_t x = 0; x < Chunk::WIDTH; ++x)
            for (int32_t y = 0; y <= 69; ++y) c.setBlock(x, y, z, y == 69 ? BlockId::Grass : BlockId::Stone, false);
    // A stone pillar inside the canopy volume must survive.
    c.setBlock(9, 73, 8, BlockId::Stone, false);
    (void)rasterizeFeature(oakAt(BlockPos{8, 70, 8}), c);
    EXPECT_EQ(c.getBlock(9, 73, 8), BlockId::Stone);
    EXPECT_EQ(c.getBlock(8, 69, 8), BlockId::Dirt);
    EXPECT_FALSE(c.isModified());
}

TEST(FeatureRasterizer, OverlappingTreesGiveSameResultInEitherOrder) {
    const Feature a = oakAt(BlockPos{6, 70, 8}, 5, 0x1U);
    const Feature b = Feature{FeatureType::BirchTree, BlockPos{8, 70, 8}, 6, 0x2U};
    Chunk ab(0, 0);
    Chunk ba(0, 0);
    (void)rasterizeFeature(a, ab);
    (void)rasterizeFeature(b, ab);
    (void)rasterizeFeature(b, ba);
    (void)rasterizeFeature(a, ba);
    for (int32_t y = 60; y < 85; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x) ASSERT_EQ(ab.getBlock(x, y, z), ba.getBlock(x, y, z));
    EXPECT_EQ(ab.getBlock(8, 72, 8), BlockId::BirchLog); // trunks beat neighbouring leaves
    EXPECT_EQ(ab.getBlock(6, 72, 8), BlockId::OakLog);
}

TEST(FeaturePlanner, SugarCaneGrowsOnlyBesideWaterAtSeaLevel) {
    const FeaturePlanner p(SEED);
    int canes = 0;
    for (int32_t cx = 0; cx < 8; ++cx) {
        Chunk c(cx, 2);
        // Striped marsh: even x columns are water at sea level, odd ones grass at sea level.
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x) {
                c.setColumnBiome(x, z, BiomeId::Swampland, glm::vec3(0.5f), glm::vec3(0.5f));
                for (int32_t y = 0; y < test::SEA; ++y) c.setBlock(x, y, z, BlockId::Stone, false);
                c.setBlock(x, test::SEA, z, (x % 2 == 0) ? BlockId::Water : BlockId::Grass, false);
            }
        for (const Feature& f : p.plan(c)) {
            if (f.type != FeatureType::SugarCane) continue;
            ++canes;
            const int32_t lx = floorMod(f.origin.x, Chunk::WIDTH);
            const int32_t lz = floorMod(f.origin.z, Chunk::DEPTH);
            ASSERT_EQ(f.origin.y, test::SEA + 1);
            const bool nearWater = c.getBlock(lx + 1, test::SEA, lz) == BlockId::Water ||
                                   c.getBlock(lx - 1, test::SEA, lz) == BlockId::Water ||
                                   c.getBlock(lx, test::SEA, lz + 1) == BlockId::Water ||
                                   c.getBlock(lx, test::SEA, lz - 1) == BlockId::Water;
            ASSERT_TRUE(nearWater);
            ASSERT_GE(f.height, 2);
            ASSERT_LE(f.height, 3);
        }
    }
    EXPECT_GT(canes, 0);
}

// ---------------------------------------------------------------- fixture contract

// The shared decoration terrain must exercise water-adjacent rules and pass-1 caves, or the
// pipeline order / border tests would silently skip those code paths.
TEST(DecorationFixture, ContainsLakesShoresAndPassOneCaves) {
    int water = 0;
    int shoreColumns = 0;
    int caveVoxels = 0;
    int borderShoreColumns = 0;
    for (int32_t cz = -2; cz <= 2; ++cz)
        for (int32_t cx = -2; cx <= 2; ++cx) {
            const auto c = makeChunk(ChunkCoord{cx, cz});
            for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                for (int32_t x = 0; x < Chunk::WIDTH; ++x) {
                    const int32_t wx = cx * Chunk::WIDTH + x;
                    const int32_t wz = cz * Chunk::DEPTH + z;
                    if (c->getBlock(x, test::SEA, z) == BlockId::Water) ++water;
                    const BlockId top = c->getBlock(x, test::SEA, z);
                    const bool atSea = (top == BlockId::Grass || top == BlockId::Sand) &&
                                       c->getBlock(x, test::SEA + 1, z) == BlockId::Air;
                    const bool besideLake = test::decorationColumn(wx + 1, wz).lake || test::decorationColumn(wx - 1, wz).lake ||
                                            test::decorationColumn(wx, wz + 1).lake || test::decorationColumn(wx, wz - 1).lake;
                    if (atSea && besideLake) {
                        ++shoreColumns;
                        if (x == 0 || x == Chunk::WIDTH - 1 || z == 0 || z == Chunk::DEPTH - 1) ++borderShoreColumns;
                    }
                    const int32_t s = test::decorationColumn(wx, wz).surface;
                    for (int32_t y = 1; y < s - 3; ++y)
                        if (c->getBlock(x, y, z) == BlockId::Air) ++caveVoxels;
                }
        }
    // The shared seed must also yield cane and water-conditional border features, so the
    // pipeline order / reference suites cover FeatureCondition resolution.
    const FeaturePlanner planner(SEED);
    int canes = 0;
    int conditional = 0;
    for (int32_t cz = -2; cz <= 2; ++cz)
        for (int32_t cx = -2; cx <= 2; ++cx)
            for (const Feature& f : planner.plan(*makeChunk(ChunkCoord{cx, cz}))) {
                if (f.type == FeatureType::SugarCane) ++canes;
                if (f.condition != FeatureCondition::None) ++conditional;
            }
    EXPECT_GT(canes, 0);
    EXPECT_GT(conditional, 0);
    EXPECT_GT(water, 50);
    EXPECT_GT(shoreColumns, 20);
    EXPECT_GT(borderShoreColumns, 4);
    EXPECT_GT(caveVoxels, 500);
}
