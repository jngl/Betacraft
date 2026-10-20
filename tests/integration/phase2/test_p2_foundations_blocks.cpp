// Phase 2 integration, data foundations: the Beta fluid block ids (water 8/9, lava 10/11)
// that the fluids, worldgen, combat, crafting and lighting modules reported as missing.
#include "tests/test_helpers.hpp"

#include "crafting/ItemIds.hpp"
#include "gameplay/Harvesting.hpp"
#include "lighting/DynamicLight.hpp"
#include "render/ChunkMesher.hpp"
#include "streaming/SectionNeighborhoodSnapshot18.hpp"
#include "streaming/SnapshotMesher.hpp"
#include "voxel/Block.hpp"
#include "world/World.hpp"
#include "worldgen/CaveCarver.hpp"

#include <gtest/gtest.h>

#include <array>
#include <vector>

using namespace betacraft;
using voxel::BlockId;
using voxel::BlockRegistry;

namespace {

constexpr std::array<BlockId, 4> kFluidBlocks{BlockId::Water, BlockId::StillWater, BlockId::Lava,
                                              BlockId::StillLava};

class P2FoundationsBlocks : public ::testing::Test {
protected:
    void SetUp() override { BlockRegistry::initialize(); }
};

// Air pocket high above the generated terrain of chunk (0,0), so nothing else meshes there.
constexpr int32_t kPocketY = 120;

size_t opaqueCount(const world::World& w, const voxel::Chunk& c) {
    return render::ChunkMesher::buildMesh(w, c, false).opaqueVertices.size();
}

} // namespace

TEST_F(P2FoundationsBlocks, FluidBlocksUseBetaIds) {
    EXPECT_EQ(static_cast<int>(BlockId::Water), 8);
    EXPECT_EQ(static_cast<int>(BlockId::StillWater), 9);
    EXPECT_EQ(static_cast<int>(BlockId::Lava), 10);
    EXPECT_EQ(static_cast<int>(BlockId::StillLava), 11);
}

TEST_F(P2FoundationsBlocks, EveryFluidBlockIsARegisteredLiquid) {
    for (const BlockId id : kFluidBlocks) {
        const voxel::BlockDef& def = BlockRegistry::get(id);
        EXPECT_EQ(def.id, id);
        EXPECT_NE(def.name, voxel::BlockDef{}.name) << static_cast<int>(id);
        EXPECT_TRUE(BlockRegistry::isLiquid(id)) << def.name;
        EXPECT_FALSE(def.solid) << def.name;
        EXPECT_FALSE(def.selectable) << def.name;
        EXPECT_TRUE(def.replaceable) << def.name;
        EXPECT_FALSE(def.isItemOnly) << def.name;
        EXPECT_LT(def.hardness, 0.0f) << def.name;
        EXPECT_EQ(def.shape, voxel::RenderShape::Liquid) << def.name;
    }
    EXPECT_FALSE(BlockRegistry::isLiquid(BlockId::Air));
    EXPECT_FALSE(BlockRegistry::isLiquid(BlockId::Obsidian));
}

TEST_F(P2FoundationsBlocks, WaterAndLavaPredicatesSplitTheFluidIds) {
    EXPECT_TRUE(voxel::isWaterBlock(BlockId::Water));
    EXPECT_TRUE(voxel::isWaterBlock(BlockId::StillWater));
    EXPECT_FALSE(voxel::isWaterBlock(BlockId::Lava));
    EXPECT_TRUE(voxel::isLavaBlock(BlockId::Lava));
    EXPECT_TRUE(voxel::isLavaBlock(BlockId::StillLava));
    EXPECT_FALSE(voxel::isLavaBlock(BlockId::StillWater));
    EXPECT_TRUE(voxel::isSameLiquid(BlockId::Water, BlockId::StillWater));
    EXPECT_TRUE(voxel::isSameLiquid(BlockId::StillLava, BlockId::Lava));
    EXPECT_FALSE(voxel::isSameLiquid(BlockId::Water, BlockId::Lava));
    EXPECT_FALSE(voxel::isSameLiquid(BlockId::Stone, BlockId::Stone));
}

TEST_F(P2FoundationsBlocks, LavaGlowsAtFullBrightnessAndBlocksSkyLight) {
    for (const BlockId id : {BlockId::Lava, BlockId::StillLava}) {
        const voxel::BlockDef& def = BlockRegistry::get(id);
        EXPECT_EQ(def.lightEmission, 15) << def.name;   // Beta setLightValue(1.0F)
        EXPECT_EQ(def.lightOpacity, 15) << def.name;    // Beta setLightOpacity(255)
        EXPECT_EQ(def.layer, voxel::RenderLayer::Opaque) << def.name; // Beta render pass 0
    }
    // Stationary water has exactly the properties of the canonical water block.
    const voxel::BlockDef& w = BlockRegistry::get(BlockId::Water);
    const voxel::BlockDef& sw = BlockRegistry::get(BlockId::StillWater);
    EXPECT_EQ(sw.lightOpacity, w.lightOpacity);
    EXPECT_EQ(sw.layer, w.layer);
    EXPECT_EQ(sw.faceTiles, w.faceTiles);
}

TEST_F(P2FoundationsBlocks, PlacedLavaLightsItsSurroundingsInTheWorld) {
    test::TempDir dir;
    world::World w(173173ULL, dir.path());
    w.ensureChunkLoaded({0, 0});
    ASSERT_EQ(w.getBlock(8, kPocketY, 8), BlockId::Air);
    ASSERT_EQ(w.getBlockLight(8, kPocketY + 1, 8), 0);

    ASSERT_TRUE(w.setBlock(8, kPocketY, 8, BlockId::Lava));
    EXPECT_EQ(w.getBlockLight(8, kPocketY, 8), 15);
    EXPECT_EQ(w.getBlockLight(8, kPocketY + 1, 8), 14);
    EXPECT_EQ(w.getBlockLight(10, kPocketY, 8), 13);
}

TEST_F(P2FoundationsBlocks, SafeSpawnNeverStandsInLava) {
    test::TempDir dir;
    world::World w(173173ULL, dir.path());
    const glm::vec3 first = w.findSafeSpawnPosition();
    const int32_t fx = static_cast<int32_t>(std::floor(first.x));
    const int32_t fy = static_cast<int32_t>(std::floor(first.y));
    const int32_t fz = static_cast<int32_t>(std::floor(first.z));
    ASSERT_TRUE(w.setBlock(fx, fy, fz, BlockId::StillLava));
    const glm::vec3 second = w.findSafeSpawnPosition();
    EXPECT_NE(second, first) << "spawn picked a cell filled with lava";
}

TEST_F(P2FoundationsBlocks, CaveCarverPaletteNowProvidesLava) {
    constexpr worldgen::CaveFillPalette palette{};
    EXPECT_TRUE(palette.providesLava());
    EXPECT_TRUE(voxel::isLavaBlock(palette.lava));
}

TEST_F(P2FoundationsBlocks, CraftingAndCombatAgreeOnFluidBlocks) {
    for (const BlockId id : kFluidBlocks) {
        const auto asBlock = crafting::toBlockId(crafting::toItemId(id));
        ASSERT_TRUE(asBlock.has_value()) << static_cast<int>(id);
        EXPECT_EQ(*asBlock, id);
        EXPECT_FALSE(gameplay::harvestInfo(id).breakable()) << static_cast<int>(id);
    }
}

TEST_F(P2FoundationsBlocks, LavaIsNotADynamicLightItem) {
    // Beta has no lava item (only the bucket), so a held/dropped lava id must not glow even
    // though the placed block emits 15.
    EXPECT_EQ(lighting::emissionOf(BlockId::Lava), 0);
    EXPECT_EQ(lighting::emissionOf(BlockId::StillLava), 0);
}

TEST_F(P2FoundationsBlocks, MesherCullsFacesBetweenCellsOfTheSameLiquid) {
    test::TempDir dir;
    world::World w(173173ULL, dir.path());
    const voxel::Chunk& chunk = w.ensureChunkLoaded({0, 0});
    const size_t base = opaqueCount(w, chunk);

    ASSERT_TRUE(w.setBlock(4, kPocketY, 4, BlockId::Lava));
    const size_t one = opaqueCount(w, chunk);
    EXPECT_EQ(one - base, 6u * 4u);

    // Flowing and still lava are one liquid: the shared face disappears on both sides.
    ASSERT_TRUE(w.setBlock(5, kPocketY, 4, BlockId::StillLava));
    EXPECT_EQ(opaqueCount(w, chunk) - base, 10u * 4u);

    // Water and still water likewise (translucent layer).
    const size_t translucentBase = render::ChunkMesher::buildMesh(w, chunk, false).translucentVertices.size();
    ASSERT_TRUE(w.setBlock(4, kPocketY, 10, BlockId::Water));
    ASSERT_TRUE(w.setBlock(5, kPocketY, 10, BlockId::StillWater));
    EXPECT_EQ(render::ChunkMesher::buildMesh(w, chunk, false).translucentVertices.size() - translucentBase,
              10u * 4u);
}

TEST_F(P2FoundationsBlocks, SnapshotMesherMatchesLegacyMesherAroundFluids) {
    test::TempDir dir;
    world::World w(173173ULL, dir.path());
    for (int cz = -1; cz <= 1; ++cz) {
        for (int cx = -1; cx <= 1; ++cx) {
            w.ensureChunkLoaded({cx, cz});
        }
    }
    // Mixed fluids, including a lava/water contact and a fluid on a chunk border.
    const std::array<BlockId, 6> pattern{BlockId::Lava, BlockId::StillLava, BlockId::Water,
                                         BlockId::StillWater, BlockId::Lava, BlockId::Water};
    for (int i = 0; i < 6; ++i) {
        ASSERT_TRUE(w.setBlock(10 + i, kPocketY, 7, pattern[static_cast<size_t>(i)]));
        ASSERT_TRUE(w.setBlock(10 + i, kPocketY + 1, 7, pattern[static_cast<size_t>(5 - i)]));
    }
    const voxel::Chunk* chunk = w.getChunk({0, 0});
    ASSERT_NE(chunk, nullptr);
    const auto legacy = render::ChunkMesher::buildMesh(w, *chunk, true);

    std::vector<streaming::PackedChunkVertex> opaque;
    std::vector<streaming::PackedChunkVertex> translucent;
    for (int32_t sy = 0; sy < voxel::SectionCoord::SECTIONS_PER_COLUMN; ++sy) {
        const auto snap = streaming::SectionNeighborhoodSnapshot18::capture(w, voxel::SectionCoord{0, sy, 0});
        const auto m = streaming::SnapshotMesher::build(*snap, sy, streaming::MeshOptions{.smoothLighting = true});
        ASSERT_TRUE(m.has_value());
        opaque.insert(opaque.end(), m->opaqueVertices.begin(), m->opaqueVertices.end());
        translucent.insert(translucent.end(), m->translucentVertices.begin(), m->translucentVertices.end());
    }
    ASSERT_EQ(legacy.opaqueVertices.size(), opaque.size());
    ASSERT_EQ(legacy.translucentVertices.size(), translucent.size());
    for (size_t i = 0; i < opaque.size(); ++i) {
        ASSERT_EQ(legacy.opaqueVertices[i].word0, opaque[i].word0) << i;
        ASSERT_EQ(legacy.opaqueVertices[i].word1, opaque[i].word1) << i;
    }
    for (size_t i = 0; i < translucent.size(); ++i) {
        ASSERT_EQ(legacy.translucentVertices[i].word0, translucent[i].word0) << i;
        ASSERT_EQ(legacy.translucentVertices[i].word1, translucent[i].word1) << i;
    }
}
