// Sprint 5 (S5.4, S5.9, S5.10) metadata-dependent shapes: collision and pick boxes from
// math::AABB (ladder, door, bed, fence, farmland, crops, stairs), the player walking
// through an open door but not a closed one nor over a fence, the 18^3 snapshot carrying the
// metadata, metadata edits re-meshing only shaped blocks, and both meshers emitting the same
// sub-block geometry.
#include "P2GameplaySupport.hpp"

#include "gameplay/ItemIds.hpp"

#include "math/AABB.hpp"
#include "physics/Raycaster.hpp"
#include "render/ChunkMesher.hpp"
#include "streaming/BlockShapeMesher.hpp"
#include "streaming/SectionNeighborhoodSnapshot18.hpp"
#include "streaming/SnapshotMesher.hpp"
#include "voxel/BlockShapes.hpp"

#include <glm/glm.hpp>

#include <vector>

using namespace betacraft;
using namespace betacraft::test::p2gameplay;
using math::AABB;

namespace {

void expectBox(const AABB& box, const glm::vec3& min, const glm::vec3& max, const char* what) {
    constexpr float eps = 1e-5f;
    EXPECT_NEAR(box.min.x, min.x, eps) << what;
    EXPECT_NEAR(box.min.y, min.y, eps) << what;
    EXPECT_NEAR(box.min.z, min.z, eps) << what;
    EXPECT_NEAR(box.max.x, max.x, eps) << what;
    EXPECT_NEAR(box.max.y, max.y, eps) << what;
    EXPECT_NEAR(box.max.z, max.z, eps) << what;
}

std::vector<AABB> collisionBoxes(BlockId id, uint8_t meta) {
    std::vector<AABB> out;
    (void)AABB::forEachBlockBox(0, 0, 0, id, meta, [&](const AABB& b) {
        out.push_back(b);
        return false;
    });
    return out;
}

bool sameWords(const std::vector<streaming::PackedChunkVertex>& a, const std::vector<streaming::PackedChunkVertex>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].word0 != b[i].word0 || a[i].word1 != b[i].word1) return false;
    }
    return true;
}

class S5MetadataShapes : public GameplayWorld {
protected:
    // Walks along +X (yaw 0) for `seconds`, optionally holding jump; returns the furthest x.
    float walkPlusX(float seconds, bool jump) {
        physics::PlayerInput input{};
        input.forward = true;
        input.jump = jump;
        float furthest = m_player.getPosition().x;
        const int frames = static_cast<int>(seconds * 60.0f);
        for (int i = 0; i < frames; ++i) {
            (void)m_player.fixedUpdate(1.0f / 60.0f, input, *m_world);
            furthest = std::max(furthest, m_player.getPosition().x);
        }
        return furthest;
    }
};

} // namespace

// --- Boxes --------------------------------------------------------------------------------

TEST(S5ShapeBoxes, NewShapesAppendedAtTheEndOfRenderShape) {
    EXPECT_EQ(static_cast<int>(voxel::RenderShape::Liquid), 6);
    EXPECT_EQ(static_cast<int>(voxel::RenderShape::Ladder), 7);
    EXPECT_EQ(static_cast<int>(voxel::RenderShape::Stairs), 13);
    EXPECT_TRUE(voxel::shapeDependsOnMetadata(voxel::RenderShape::Door));
    EXPECT_FALSE(voxel::shapeDependsOnMetadata(voxel::RenderShape::Fence));
    EXPECT_FALSE(voxel::shapeDependsOnMetadata(voxel::RenderShape::Liquid));
}

TEST(S5ShapeBoxes, LadderIsTwoSixteenthsAgainstItsWall) {
    voxel::BlockRegistry::initialize();
    constexpr float t = 2.0f / 16.0f;
    expectBox(AABB::fromBlock(0, 0, 0, BlockId::Ladder, 2), {0, 0, 1 - t}, {1, 1, 1}, "meta 2 (+Z wall)");
    expectBox(AABB::fromBlock(0, 0, 0, BlockId::Ladder, 3), {0, 0, 0}, {1, 1, t}, "meta 3 (-Z wall)");
    expectBox(AABB::fromBlock(0, 0, 0, BlockId::Ladder, 4), {1 - t, 0, 0}, {1, 1, 1}, "meta 4 (+X wall)");
    expectBox(AABB::fromBlock(0, 0, 0, BlockId::Ladder, 5), {0, 0, 0}, {t, 1, 1}, "meta 5 (-X wall)");
    EXPECT_EQ(voxel::ladderSupportOffset(4), glm::ivec3(1, 0, 0));
    EXPECT_EQ(voxel::ladderMetaForClickedFace(core::FaceDir::NegX), std::optional<uint8_t>{4});
    EXPECT_EQ(voxel::ladderMetaForClickedFace(core::FaceDir::PosY), std::nullopt);
}

TEST(S5ShapeBoxes, DoorIsThreeSixteenthsByOrientationAndState) {
    voxel::BlockRegistry::initialize();
    constexpr float t = 3.0f / 16.0f;
    // Beta BlockDoor.getState: closed = orientation - 1, open = orientation (mod 4).
    const glm::vec3 zLow[2] = {{0, 0, 0}, {1, 1, t}};
    const glm::vec3 xHigh[2] = {{1 - t, 0, 0}, {1, 1, 1}};
    const glm::vec3 zHigh[2] = {{0, 0, 1 - t}, {1, 1, 1}};
    const glm::vec3 xLow[2] = {{0, 0, 0}, {t, 1, 1}};
    const glm::vec3* byState[4] = {zLow, xHigh, zHigh, xLow};
    for (uint8_t meta = 0; meta < 16; ++meta) {
        const uint8_t state = (meta & voxel::kDoorOpenBit) != 0 ? (meta & 3) : ((meta + 3) & 3);
        EXPECT_EQ(voxel::doorState(meta), state) << int(meta);
        const glm::vec3* box = byState[state];
        expectBox(AABB::fromBlock(0, 0, 0, BlockId::WoodDoor, meta), box[0], box[1], "door");
    }
    // Opening turns the slab a quarter: orientation 0 closed is along X, open along Z.
    expectBox(AABB::fromBlock(0, 0, 0, BlockId::WoodDoor, 0), {0, 0, 0}, {t, 1, 1}, "closed");
    expectBox(AABB::fromBlock(0, 0, 0, BlockId::WoodDoor, voxel::kDoorOpenBit), {0, 0, 0}, {1, 1, t}, "open");
    // The upper half (bit 3) has the same box.
    expectBox(AABB::fromBlock(0, 0, 0, BlockId::WoodDoor, voxel::kDoorUpperBit | voxel::kDoorOpenBit), {0, 0, 0},
              {1, 1, t}, "open upper half");
}

TEST(S5ShapeBoxes, BedFenceFarmlandCropsAndStairs) {
    voxel::BlockRegistry::initialize();
    expectBox(AABB::fromBlock(3, 4, 5, BlockId::Bed, 0), {3, 4, 5}, {4, 4 + 9.0f / 16.0f, 6}, "bed");
    expectBox(AABB::fromBlock(3, 4, 5, BlockId::Bed, voxel::kBedHeadBit | 2), {3, 4, 5}, {4, 4 + 9.0f / 16.0f, 6},
              "bed head");
    expectBox(AABB::fromBlock(3, 4, 5, BlockId::Fence, 0), {3, 4, 5}, {4, 5.5f, 6}, "fence collides 1.5 high");
    expectBox(AABB::selectionBox(3, 4, 5, BlockId::Fence, 0), {3, 4, 5}, {4, 5, 6}, "fence picks as a cube");
    expectBox(AABB::fromBlock(3, 4, 5, BlockId::Farmland, 7), {3, 4, 5}, {4, 4 + 15.0f / 16.0f, 6}, "farmland");
    expectBox(AABB::selectionBox(3, 4, 5, BlockId::Crops, 7), {3, 4, 5}, {4, 4.25f, 6}, "crops pick slab");
    EXPECT_FALSE(voxel::BlockRegistry::isSolid(BlockId::Crops));
    EXPECT_TRUE(collisionBoxes(BlockId::Crops, 7).empty()) << "crops do not collide";
    EXPECT_EQ(collisionBoxes(BlockId::Fence, 0).size(), 1U);

    // Stairs: a bottom half and a full-height half on the metadata's side (0 = +X ... 3 = -Z).
    const glm::vec3 highHalf[4][2] = {{{0.5f, 0, 0}, {1, 1, 1}},
                                      {{0, 0, 0}, {0.5f, 1, 1}},
                                      {{0, 0, 0.5f}, {1, 1, 1}},
                                      {{0, 0, 0}, {1, 1, 0.5f}}};
    for (const BlockId id : {BlockId::WoodStairs, BlockId::CobbleStairs}) {
        for (uint8_t meta = 0; meta < 4; ++meta) {
            const std::vector<AABB> boxes = collisionBoxes(id, meta);
            ASSERT_EQ(boxes.size(), 2U);
            float volume = 0.0f;
            bool foundHigh = false;
            for (const AABB& b : boxes) {
                const glm::vec3 d = b.max - b.min;
                volume += d.x * d.y * d.z;
                foundHigh = foundHigh || (glm::all(glm::lessThan(glm::abs(b.min - highHalf[meta][0]), glm::vec3(1e-5f))) &&
                                          glm::all(glm::lessThan(glm::abs(b.max - highHalf[meta][1]), glm::vec3(1e-5f))));
            }
            EXPECT_NEAR(volume, 0.75f, 1e-5f) << "stairs fill three quarters of the cell";
            EXPECT_TRUE(foundHigh) << "meta " << int(meta);
            expectBox(AABB::fromBlock(0, 0, 0, id, meta), {0, 0, 0}, {1, 1, 1}, "stairs outline");
        }
    }
    // Beta onBlockPlacedBy: the high step on the far side of the look direction.
    EXPECT_EQ(voxel::stairsMetaForLook(1.0f, 0.2f), 0);
    EXPECT_EQ(voxel::stairsMetaForLook(-1.0f, 0.2f), 1);
    EXPECT_EQ(voxel::stairsMetaForLook(0.1f, 1.0f), 2);
    EXPECT_EQ(voxel::stairsMetaForLook(0.1f, -1.0f), 3);
}

TEST(S5ShapeBoxes, OldSignatureReadsMetadataZero) {
    voxel::BlockRegistry::initialize();
    expectBox(AABB::fromBlock(1, 2, 3, BlockId::WoodDoor), AABB::fromBlock(1, 2, 3, BlockId::WoodDoor, 0).min,
              AABB::fromBlock(1, 2, 3, BlockId::WoodDoor, 0).max, "door");
    expectBox(AABB::fromBlock(1, 2, 3, BlockId::CobbleSlab), {1, 2, 3}, {2, 2.5f, 4}, "slab unchanged");
}

// --- Player against the shapes ------------------------------------------------------------

TEST_F(S5MetadataShapes, OpenDoorLetsThePlayerThroughClosedDoorBlocks) {
    // Orientation 0: closed spans the cell along Z at its -X side, open lies along X.
    ASSERT_TRUE(m_world->setBlockWithMetadata(5, kY, 2, BlockId::WoodDoor, 0));
    ASSERT_TRUE(m_world->setBlockWithMetadata(5, kY + 1, 2, BlockId::WoodDoor, voxel::kDoorUpperBit));
    const float closedReach = walkPlusX(2.0f, false);
    EXPECT_LT(closedReach, 5.0f - 0.29f) << "a closed door blocks";

    ASSERT_TRUE(m_world->setMetadata(5, kY, 2, voxel::kDoorOpenBit));
    ASSERT_TRUE(m_world->setMetadata(5, kY + 1, 2, voxel::kDoorUpperBit | voxel::kDoorOpenBit));
    const float openReach = walkPlusX(2.0f, false);
    EXPECT_GT(openReach, 6.5f) << "an open door lets the player through";
}

TEST_F(S5MetadataShapes, FenceCannotBeJumped) {
    for (int32_t z = -4; z <= 9; ++z) {
        ASSERT_TRUE(m_world->setBlock(5, kY, z, BlockId::Fence));
    }
    const float reach = walkPlusX(4.0f, true);
    EXPECT_LT(reach, 5.0f - 0.29f) << "the 1.5-high fence box is above the jump apex";
    EXPECT_LE(m_player.getPosition().y, static_cast<float>(kY) + 1.4f);

    // A one-block wall in its place is jumped over.
    for (int32_t z = -4; z <= 9; ++z) {
        ASSERT_TRUE(m_world->setBlock(5, kY, z, BlockId::Cobblestone));
    }
    EXPECT_GT(walkPlusX(4.0f, true), 5.5f);
}

TEST_F(S5MetadataShapes, StairsAreWalkedUpHalfABlockAtATime) {
    // Ascending stairs towards +X: meta 0 puts the high half on the +X side.
    ASSERT_TRUE(m_world->setBlockWithMetadata(5, kY, 2, BlockId::WoodStairs, 0));
    const std::vector<AABB> boxes = collisionBoxes(BlockId::WoodStairs, 0);
    ASSERT_EQ(boxes.size(), 2U);
    // Standing on the low step of the cell: the feet rest at half height.
    placePlayer({5.2f, static_cast<float>(kY) + 0.6f, 2.5f});
    EXPECT_NEAR(m_player.getPosition().y, static_cast<float>(kY) + 0.5f, 0.02f);
}

TEST_F(S5MetadataShapes, PlacedStairsFaceAwayFromThePlayer) {
    ASSERT_TRUE(m_world->setBlock(6, kY, 2, BlockId::Stone));
    hold({gameplay::toItemId(BlockId::CobbleStairs), 2});
    // Looking along +X: the high step on the +X side (meta 0), so the player walks up it.
    ASSERT_EQ(game::placeHeldBlock(*m_world, m_player, targetAt({6, kY, 2}, BlockId::Stone)),
              std::optional<BlockId>{BlockId::CobbleStairs});
    EXPECT_EQ(m_world->getMetadata(5, kY, 2), 0);
    m_player.setRotation(90.0f, 0.0f); // Looking along +Z
    ASSERT_TRUE(m_world->setBlock(6, kY, 4, BlockId::Stone));
    ASSERT_TRUE(game::placeHeldBlock(*m_world, m_player, targetAt({6, kY, 4}, BlockId::Stone)).has_value());
    EXPECT_EQ(m_world->getMetadata(5, kY, 4), 2);
}

TEST_F(S5MetadataShapes, PlacementOverlapUsesTheShapedBox) {
    // The player stands in cell (2, kY, 2); a ladder plate on its +X side does not touch the
    // player box, a full fence would.
    ASSERT_TRUE(m_world->setBlock(3, kY, 2, BlockId::Stone));
    hold({gameplay::toItemId(BlockId::Ladder), 1});
    EXPECT_TRUE(game::placeHeldBlock(*m_world, m_player, targetAt({3, kY, 2}, BlockId::Stone)).has_value());
    EXPECT_EQ(m_world->getBlock(2, kY, 2), BlockId::Ladder);
    ASSERT_TRUE(m_world->setBlock(2, kY, 2, BlockId::Air));
    hold({gameplay::toItemId(BlockId::Fence), 1});
    EXPECT_FALSE(game::placeHeldBlock(*m_world, m_player, targetAt({3, kY, 2}, BlockId::Stone)).has_value());
}

TEST_F(S5MetadataShapes, RaycastPicksFenceAsCubeAndCropsAsSlab) {
    ASSERT_TRUE(m_world->setBlock(6, kY, 2, BlockId::Fence));
    const physics::RaycastHit fence =
        physics::Raycaster::cast(*m_world, {6.5f, kY + 3.0f, 2.5f}, {0.0f, -1.0f, 0.0f}, 6.0f);
    ASSERT_TRUE(fence.hit);
    EXPECT_EQ(fence.blockId, BlockId::Fence);
    EXPECT_NEAR(fence.distance, 2.0f, 1e-3f) << "the pick box tops at 1.0, not at the collision's 1.5";

    ASSERT_TRUE(m_world->setBlock(7, kY, 2, BlockId::Farmland));
    ASSERT_TRUE(m_world->setBlockWithMetadata(7, kY + 1, 2, BlockId::Crops, 3));
    const physics::RaycastHit crops =
        physics::Raycaster::cast(*m_world, {7.5f, kY + 3.0f, 2.5f}, {0.0f, -1.0f, 0.0f}, 6.0f);
    ASSERT_TRUE(crops.hit);
    EXPECT_EQ(crops.blockId, BlockId::Crops);
    EXPECT_EQ(crops.blockMeta, 3);
    EXPECT_NEAR(crops.distance, 1.75f, 1e-3f);
}

// --- Snapshot and re-meshing --------------------------------------------------------------

TEST_F(S5MetadataShapes, SnapshotCarriesMetadata) {
    ASSERT_TRUE(m_world->setBlockWithMetadata(3, kY, 4, BlockId::WoodDoor, 0x6));
    ASSERT_TRUE(m_world->setBlockWithMetadata(16, kY, 4, BlockId::Ladder, 5)); // neighbour column
    ASSERT_TRUE(m_world->setBlockWithMetadata(3, kY - 8 - 1, 4, BlockId::Crops, 7)); // section below
    const int32_t sy = kY / 16;
    const auto snap = streaming::SectionNeighborhoodSnapshot18::capture(*m_world, {0, sy, 0});
    const int32_t ly = kY - sy * 16;
    EXPECT_EQ(snap->getBlock(3, ly, 4), BlockId::WoodDoor);
    EXPECT_EQ(snap->getMetadata(3, ly, 4), 0x6);
    EXPECT_EQ(snap->getBlockState(3, ly, 4), static_cast<voxel::BlockStateId>(BlockId::WoodDoor))
        << "the metadata does not leak into the block state";
    EXPECT_EQ(snap->getBlock(16, ly, 4), BlockId::Ladder) << "guard border";
    EXPECT_EQ(snap->getMetadata(16, ly, 4), 5);
    EXPECT_EQ(snap->getMetadata(4, ly, 4), 0);
    EXPECT_EQ(snap->getMetadata(40, ly, 4), 0) << "outside the cube";

    // Generic capture: VoxelSample::meta, defaulted to 0 for older samplers.
    const auto generic = streaming::SectionNeighborhoodSnapshot18::captureFrom(
        [](int32_t x, int32_t y, int32_t z) -> std::optional<streaming::VoxelSample> {
            if (x == 2 && y == 3 && z == 4) {
                return streaming::VoxelSample{static_cast<voxel::BlockStateId>(BlockId::Ladder), 15, 0, 3};
            }
            return streaming::VoxelSample{static_cast<voxel::BlockStateId>(BlockId::Air), 15, 0};
        });
    EXPECT_EQ(generic->getMetadata(2, 3, 4), 3);
    EXPECT_EQ(generic->getBlock(2, 3, 4), BlockId::Ladder);
    EXPECT_EQ(generic->getMetadata(2, 3, 5), 0);
}

TEST_F(S5MetadataShapes, DoorMetadataEditRemeshesTheSectionButFluidLevelsDoNot) {
    voxel::Chunk* chunk = m_world->getChunkMutable({0, 0});
    ASSERT_NE(chunk, nullptr);
    const size_t sy = static_cast<size_t>(kY / 16);
    ASSERT_TRUE(m_world->setBlockWithMetadata(4, kY, 4, BlockId::WoodDoor, 0));
    ASSERT_TRUE(m_world->setBlock(6, kY, 4, BlockId::Water));
    ASSERT_TRUE(m_world->setBlock(8, kY, 4, BlockId::Fence));

    chunk->setMeshDirty(false);
    ASSERT_TRUE(m_world->setMetadata(4, kY, 4, voxel::kDoorOpenBit));
    EXPECT_TRUE(chunk->isSectionDirty(sy)) << "opening a door changes its geometry";

    chunk->setMeshDirty(false);
    ASSERT_TRUE(m_world->setMetadata(6, kY, 4, 3));
    EXPECT_FALSE(chunk->isMeshDirty()) << "a fluid level change re-meshes nothing";
    ASSERT_TRUE(m_world->setMetadata(8, kY, 4, 2));
    EXPECT_FALSE(chunk->isMeshDirty()) << "fence geometry ignores the metadata";

    // On a column seam the neighbour column's section sees the cell too.
    ASSERT_TRUE(m_world->setBlockWithMetadata(0, kY, 4, BlockId::Ladder, 2));
    voxel::Chunk* west = m_world->getChunkMutable({-1, 0});
    ASSERT_NE(west, nullptr);
    chunk->setMeshDirty(false);
    west->setMeshDirty(false);
    ASSERT_TRUE(m_world->setMetadata(0, kY, 4, 3));
    EXPECT_TRUE(chunk->isSectionDirty(sy));
    EXPECT_TRUE(west->isSectionDirty(sy));
}

// --- Geometry -----------------------------------------------------------------------------

TEST(S5ShapeMesher, ClosedDoorVerticesStayInsideItsSlab) {
    voxel::BlockRegistry::initialize();
    const auto snap = streaming::SectionNeighborhoodSnapshot18::captureFrom(
        [](int32_t x, int32_t y, int32_t z) -> std::optional<streaming::VoxelSample> {
            if (x == 5 && y == 6 && z == 7) {
                return streaming::VoxelSample{static_cast<voxel::BlockStateId>(BlockId::WoodDoor), 15, 0, 0};
            }
            return streaming::VoxelSample{static_cast<voxel::BlockStateId>(BlockId::Air), 15, 0};
        });
    const auto mesh = streaming::SnapshotMesher::build(*snap, 0, {});
    ASSERT_TRUE(mesh.has_value());
    ASSERT_EQ(mesh->opaqueVertices.size(), 6U * 4U) << "one box, six faces";
    for (const streaming::PackedChunkVertex& v : mesh->opaqueVertices) {
        const glm::vec3 p = v.getLocalPosition();
        EXPECT_GE(p.x, 5.0f - 1e-5f);
        EXPECT_LE(p.x, 5.0f + 3.0f / 16.0f + 1e-5f) << "closed orientation 0: x in [0, 3/16]";
        EXPECT_GE(p.z, 7.0f - 1e-5f);
        EXPECT_LE(p.z, 8.0f + 1e-5f);
        EXPECT_EQ(v.getAtlasTile(), voxel::kDoorLowerTile);
    }
}

TEST(S5ShapeMesher, FenceJoinsNeighbouringFencesOnly) {
    voxel::BlockRegistry::initialize();
    const auto meshWith = [](BlockId east) {
        const auto snap = streaming::SectionNeighborhoodSnapshot18::captureFrom(
            [east](int32_t x, int32_t y, int32_t z) -> std::optional<streaming::VoxelSample> {
                BlockId id = BlockId::Air;
                if (y == 4 && z == 4 && x == 4) id = BlockId::Fence;
                if (y == 4 && z == 4 && x == 5) id = east;
                return streaming::VoxelSample{static_cast<voxel::BlockStateId>(id), 15, 0};
            });
        return streaming::SnapshotMesher::build(*snap, 0, {})->opaqueVertices.size() / 4;
    };
    const size_t alone = meshWith(BlockId::Air);
    EXPECT_EQ(alone, 6U) << "a lone post";
    // Two posts plus two bars each way; a bar skips the end inside its post and the end
    // meeting the neighbour's bar on the cell seam, so it keeps 4 faces.
    const size_t joined = meshWith(BlockId::Fence);
    EXPECT_EQ(joined, 6U + 6U + 4U * 4U);
    EXPECT_EQ(meshWith(BlockId::Stone), 6U + 6U) << "the post and the stone cube: fences join fences only";
}

TEST_F(S5MetadataShapes, BothMeshersProduceTheSameShapeGeometry) {
    // Every shape in several states, some on the column seam (x = 0 and 15) and on the
    // section seam (y = 111 / 112), with neighbours that join and cull.
    struct Placed {
        int32_t x, y, z;
        BlockId id;
        uint8_t meta;
    };
    const std::vector<Placed> blocks{
        {1, kY, 1, BlockId::WoodDoor, 0},        {1, kY + 1, 1, BlockId::WoodDoor, 8},
        {3, kY, 1, BlockId::WoodDoor, 5},        {3, kY + 1, 1, BlockId::WoodDoor, 13},
        {5, kY, 1, BlockId::Bed, 1},             {6, kY, 1, BlockId::Bed, 9},
        {8, kY, 1, BlockId::Farmland, 0},        {9, kY, 1, BlockId::Farmland, 7},
        {9, kY + 1, 1, BlockId::Crops, 7},       {8, kY + 1, 1, BlockId::Crops, 2},
        {11, kY, 1, BlockId::WoodStairs, 0},     {12, kY, 1, BlockId::CobbleStairs, 1},
        {11, kY, 3, BlockId::WoodStairs, 2},     {12, kY, 3, BlockId::CobbleStairs, 3},
        {0, kY, 5, BlockId::Fence, 0},           {-1, kY, 5, BlockId::Fence, 0},
        {0, kY, 6, BlockId::Fence, 0},           {0, kY + 1, 6, BlockId::Fence, 0},
        {15, kY, 7, BlockId::Fence, 0},          {16, kY, 7, BlockId::Fence, 0},
        {15, kY, 9, BlockId::Ladder, 4},         {16, kY, 9, BlockId::Stone, 0},
        {4, kY, 9, BlockId::Ladder, 2},          {4, kY, 10, BlockId::Stone, 0},
        {6, kY, 9, BlockId::Ladder, 3},          {7, kY, 9, BlockId::Ladder, 5},
        {4, 111, 4, BlockId::WoodStairs, 1},     {4, 112, 4, BlockId::Fence, 0},
        {5, 111, 4, BlockId::Fence, 0},          {5, 112, 4, BlockId::WoodDoor, 2},
        {9, kY, 12, BlockId::Glowstone, 0},      {10, kY, 12, BlockId::Fence, 0},
    };
    for (const Placed& b : blocks) {
        ASSERT_TRUE(m_world->setBlockWithMetadata(b.x, b.y, b.z, b.id, b.meta)) << b.x << " " << b.y << " " << b.z;
    }

    const voxel::Chunk& chunk = *m_world->getChunk({0, 0});
    const render::ChunkMeshData legacy = render::ChunkMesher::buildMesh(*m_world, chunk, true);
    std::vector<streaming::PackedChunkVertex> opaque;
    std::vector<streaming::PackedChunkVertex> translucent;
    for (int32_t sy = 0; sy < voxel::Chunk::HEIGHT / 16; ++sy) {
        const auto snap = streaming::SectionNeighborhoodSnapshot18::capture(*m_world, {0, sy, 0});
        const auto mesh = streaming::SnapshotMesher::build(*snap, sy, {.smoothLighting = true});
        ASSERT_TRUE(mesh.has_value());
        opaque.insert(opaque.end(), mesh->opaqueVertices.begin(), mesh->opaqueVertices.end());
        translucent.insert(translucent.end(), mesh->translucentVertices.begin(), mesh->translucentVertices.end());
    }
    EXPECT_TRUE(sameWords(legacy.opaqueVertices, opaque)) << legacy.opaqueVertices.size() << " vs " << opaque.size();
    EXPECT_TRUE(sameWords(legacy.translucentVertices, translucent));

    // The shapes really produced sub-block vertices.
    size_t fine = 0;
    for (const streaming::PackedChunkVertex& v : opaque) {
        fine += v.getFineOffset() != glm::uvec3(0) ? 1U : 0U;
    }
    EXPECT_GT(fine, 100U);
}
