// P2.3 - Abstract voxel access contract, exercised through the in-memory grid.
#include "fluids/GridFluidVoxelAccess.hpp"

#include <gtest/gtest.h>

using betacraft::core::BlockPos;
using betacraft::fluids::defaultBlocksFlow;
using betacraft::fluids::FluidState;
using betacraft::fluids::FluidType;
using betacraft::fluids::GridFluidVoxelAccess;
using betacraft::fluids::IFluidVoxelAccess;
using betacraft::voxel::BlockId;

TEST(DefaultBlocksFlow, AirAndPlantsAreWashedAwaySolidsBlock) {
    EXPECT_FALSE(defaultBlocksFlow(BlockId::Air));
    EXPECT_FALSE(defaultBlocksFlow(BlockId::Water));
    EXPECT_FALSE(defaultBlocksFlow(BlockId::TallGrass));
    EXPECT_FALSE(defaultBlocksFlow(BlockId::Sapling));
    EXPECT_FALSE(defaultBlocksFlow(BlockId::DeadBush));
    EXPECT_FALSE(defaultBlocksFlow(BlockId::Dandelion));
    EXPECT_FALSE(defaultBlocksFlow(BlockId::Rose));
    EXPECT_FALSE(defaultBlocksFlow(BlockId::Torch));
    EXPECT_TRUE(defaultBlocksFlow(BlockId::Stone));
    EXPECT_TRUE(defaultBlocksFlow(BlockId::Glass));
    EXPECT_TRUE(defaultBlocksFlow(BlockId::Obsidian));
    EXPECT_TRUE(defaultBlocksFlow(BlockId::SugarCane)); // Beta: reeds block flow
    EXPECT_TRUE(defaultBlocksFlow(BlockId::Leaves));
}

TEST(GridFluidVoxelAccess, NewGridIsAirAndDry) {
    GridFluidVoxelAccess g(4, 3, 5);
    EXPECT_EQ(g.sizeX(), 4);
    EXPECT_EQ(g.sizeY(), 3);
    EXPECT_EQ(g.sizeZ(), 5);
    for (int x = 0; x < 4; ++x) {
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < 5; ++z) {
                EXPECT_TRUE(g.isLoaded({x, y, z}));
                EXPECT_EQ(g.getBlock({x, y, z}), BlockId::Air);
                EXPECT_TRUE(g.getFluid({x, y, z}).isEmpty());
            }
        }
    }
}

TEST(GridFluidVoxelAccess, OutOfBoundsIsUnloadedBlockingAndIgnoresWrites) {
    GridFluidVoxelAccess g(2, 2, 2);
    const BlockPos outside[] = {{-1, 0, 0}, {0, -1, 0}, {0, 0, -1}, {2, 0, 0}, {0, 2, 0}, {0, 0, 2}};
    for (const BlockPos& p : outside) {
        EXPECT_FALSE(g.isLoaded(p));
        EXPECT_TRUE(g.blocksFlow(p));
        g.setFluid(p, FluidState::source(FluidType::Water));
        g.setBlock(p, BlockId::Stone);
        EXPECT_TRUE(g.getFluid(p).isEmpty());
    }
    EXPECT_EQ(g.writeCount(), 0U);
}

TEST(GridFluidVoxelAccess, SetFluidReplacesBlockAndSetBlockClearsFluid) {
    GridFluidVoxelAccess g(3, 3, 3);
    const BlockPos p{1, 1, 1};
    g.setBlock(p, BlockId::TallGrass);
    g.setFluid(p, FluidState::flowing(FluidType::Water, 4));
    EXPECT_EQ(g.getFluid(p), FluidState::flowing(FluidType::Water, 4));
    EXPECT_EQ(g.getBlock(p), BlockId::Air);

    g.setBlock(p, BlockId::Obsidian);
    EXPECT_TRUE(g.getFluid(p).isEmpty());
    EXPECT_EQ(g.getBlock(p), BlockId::Obsidian);
    EXPECT_TRUE(g.blocksFlow(p));

    g.setFluid(p, FluidState{}); // clearing a fluid leaves air
    EXPECT_EQ(g.getBlock(p), BlockId::Air);
    EXPECT_EQ(g.writeCount(), 4U);
}

TEST(GridFluidVoxelAccess, FillAndCountFluid) {
    GridFluidVoxelAccess g(5, 2, 5);
    g.fill({0, 0, 0}, {4, 0, 4}, BlockId::Stone);
    EXPECT_EQ(g.getBlock({4, 0, 4}), BlockId::Stone);
    EXPECT_EQ(g.getBlock({0, 1, 0}), BlockId::Air);
    EXPECT_EQ(g.countFluid(FluidType::Water), 0U);
    g.setFluid({0, 1, 0}, FluidState::source(FluidType::Water));
    g.setFluid({1, 1, 0}, FluidState::flowing(FluidType::Lava, 3));
    EXPECT_EQ(g.countFluid(FluidType::Water), 1U);
    EXPECT_EQ(g.countFluid(FluidType::Lava), 1U);
    EXPECT_EQ(g.countFluid(), 2U);
}

TEST(GridFluidVoxelAccess, SnapshotEqualityDetectsAnyDifference) {
    GridFluidVoxelAccess a(3, 3, 3);
    GridFluidVoxelAccess b(3, 3, 3);
    EXPECT_TRUE(a.sameContents(b));
    a.setFluid({0, 0, 0}, FluidState::flowing(FluidType::Water, 2));
    EXPECT_FALSE(a.sameContents(b));
    b.setFluid({0, 0, 0}, FluidState::flowing(FluidType::Water, 2));
    EXPECT_TRUE(a.sameContents(b));
    b.setBlock({2, 2, 2}, BlockId::Stone);
    EXPECT_FALSE(a.sameContents(b));
}

TEST(GridFluidVoxelAccess, UsableThroughInterface) {
    GridFluidVoxelAccess g(2, 2, 2);
    IFluidVoxelAccess& access = g;
    access.setFluid({1, 1, 1}, FluidState::falling(FluidType::Lava));
    EXPECT_TRUE(access.getFluid({1, 1, 1}).isFalling());
    EXPECT_FALSE(access.blocksFlow({0, 0, 0}));
}

TEST(GridFluidVoxelAccess, UnloadedRegionHidesButKeepsItsContents) {
    // Models a chunk saved to disk: unreadable and unwritable while unloaded, intact after.
    GridFluidVoxelAccess g(4, 2, 1);
    g.setBlock({2, 0, 0}, BlockId::Stone);
    g.setFluid({3, 1, 0}, FluidState::source(FluidType::Water));
    g.setLoaded({2, 0, 0}, {3, 1, 0}, false);
    EXPECT_TRUE(g.isLoaded({1, 0, 0}));
    EXPECT_FALSE(g.isLoaded({2, 0, 0}));
    EXPECT_FALSE(g.isLoaded({3, 1, 0}));
    EXPECT_EQ(g.getBlock({2, 0, 0}), BlockId::Air);
    EXPECT_TRUE(g.getFluid({3, 1, 0}).isEmpty());
    EXPECT_TRUE(g.blocksFlow({2, 1, 0})) << "unloaded cells act as walls";
    g.setBlock({2, 1, 0}, BlockId::Dirt); // ignored while unloaded
    g.setLoaded({2, 0, 0}, {3, 1, 0}, true);
    EXPECT_EQ(g.getBlock({2, 0, 0}), BlockId::Stone);
    EXPECT_EQ(g.getBlock({2, 1, 0}), BlockId::Air);
    EXPECT_TRUE(g.getFluid({3, 1, 0}).isSource());
}
