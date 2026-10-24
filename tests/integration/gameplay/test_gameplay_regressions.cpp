// Phase 2 integration review fixes, gameplay side: quick clicks, Beta hardness, drops of
// vanilla items and of blocks that lose their support, lava versus water for the player,
// placing fluid into flowing fluid.
#include "tests/integration/support/GameplaySupport.hpp"

#include "crafting/BetaRecipes.hpp"
#include "crafting/CraftingGrid.hpp"
#include "crafting/RecipeRegistry.hpp"
#include "gameplay/Harvesting.hpp"
#include "gameplay/ItemIds.hpp"

#include <gtest/gtest.h>

#include <map>
#include <utility>

using namespace betacraft;
using namespace betacraft::test::p2gameplay;
using physics::InventorySlot;
namespace items = gameplay::items;

namespace {

const core::BlockPos kRock{5, kY, 2};

class GameplayRegressions : public GameplayWorld {
protected:
    [[nodiscard]] std::map<gameplay::ItemId, int32_t> droppedItems() {
        std::map<gameplay::ItemId, int32_t> out;
        m_session.entities().view<ecs::ItemDropComp>().each(
            [&](ecs::EntityId, const ecs::ItemDropComp& item) { out[item.id] += item.count; });
        return out;
    }

    [[nodiscard]] gameplay::ItemStack toolStack(gameplay::ToolKind kind, gameplay::ToolMaterial material) const {
        return gameplay::makeStack(gameplay::toolItemId(kind, material), 1);
    }
};

[[nodiscard]] crafting::RecipeResult craft2x2(gameplay::ItemId ingredient) {
    const crafting::RecipeRegistry book = crafting::makeBetaRecipeBook();
    crafting::CraftingGrid grid{crafting::GridSize::Inventory2x2};
    for (int32_t y = 0; y < 2; ++y) {
        for (int32_t x = 0; x < 2; ++x) {
            EXPECT_TRUE(grid.set(x, y, {ingredient, 1, 0}));
        }
    }
    const auto match = book.match(grid);
    return match ? match->result : crafting::RecipeResult{};
}

} // namespace

// --- Quick clicks ---------------------------------------------------------------------

TEST_F(GameplayRegressions, ClickReleasedBeforeTheGameTickStillBreaksAnInstantBlock) {
    // Press and release inside one 50 ms tick: the tick sees attackPressed without attackHeld.
    ASSERT_TRUE(m_world->setBlock(kRock, BlockId::Dandelion));
    const game::GameTickEvents ev = m_session.tick(
        *m_world, m_player, {.attackPressed = true, .attackHeld = false, .target = targetAt(kRock, BlockId::Dandelion)});
    ASSERT_TRUE(ev.blockBroken.has_value());
    EXPECT_EQ(m_world->getBlock(kRock), BlockId::Air);
    EXPECT_EQ(droppedItems()[gameplay::toItemId(BlockId::Dandelion)], 1);
}

TEST_F(GameplayRegressions, ClickReleasedBeforeTheGameTickDoesNotStartMiningASlowBlock) {
    ASSERT_TRUE(m_world->setBlock(kRock, BlockId::Stone));
    const game::GameTickEvents ev = m_session.tick(
        *m_world, m_player, {.attackPressed = true, .attackHeld = false, .target = targetAt(kRock, BlockId::Stone)});
    EXPECT_FALSE(ev.blockBroken.has_value());
    EXPECT_FALSE(ev.digging);
    EXPECT_EQ(m_session.crackStage(), -1);
    EXPECT_FALSE(m_session.miningTarget().has_value());
    EXPECT_EQ(m_world->getBlock(kRock), BlockId::Stone);
}

// --- Beta 1.7.3 hardness ---------------------------------------------------------------

TEST(HardnessRegressions, BlockHardnessIsBetaBlockStrength) {
    voxel::BlockRegistry::initialize();
    // Block.setHardness values from Beta 1.7.3 Block.java.
    const std::pair<BlockId, float> beta[] = {
        {BlockId::Stone, 1.5f},        {BlockId::Grass, 0.6f},       {BlockId::Dirt, 0.5f},
        {BlockId::Cobblestone, 2.0f},  {BlockId::OakPlanks, 2.0f},   {BlockId::Sapling, 0.0f},
        {BlockId::Sand, 0.5f},         {BlockId::Gravel, 0.6f},      {BlockId::GoldOre, 3.0f},
        {BlockId::IronOre, 3.0f},      {BlockId::CoalOre, 3.0f},     {BlockId::OakLog, 2.0f},
        {BlockId::BirchLog, 2.0f},     {BlockId::PineLog, 2.0f},     {BlockId::Leaves, 0.2f},
        {BlockId::Glass, 0.3f},        {BlockId::LapisOre, 3.0f},    {BlockId::Sandstone, 0.8f},
        {BlockId::TallGrass, 0.0f},    {BlockId::DeadBush, 0.0f},    {BlockId::Piston, 0.5f},
        {BlockId::PistonHead, 0.5f},   {BlockId::Wool, 0.8f},        {BlockId::Dandelion, 0.0f},
        {BlockId::Rose, 0.0f},         {BlockId::CobbleSlab, 2.0f},  {BlockId::Bricks, 2.0f},
        {BlockId::TNT, 0.0f},          {BlockId::Bookshelf, 1.5f},   {BlockId::MossyCobble, 2.0f},
        {BlockId::Obsidian, 10.0f},    {BlockId::Torch, 0.0f},       {BlockId::DiamondOre, 3.0f},
        {BlockId::CraftingTable, 2.5f}, {BlockId::Furnace, 3.5f},    {BlockId::RedstoneOre, 3.0f},
        {BlockId::Ice, 0.5f},          {BlockId::SnowBlock, 0.2f},   {BlockId::Cactus, 0.4f},
        {BlockId::Clay, 0.6f},         {BlockId::SugarCane, 0.0f},   {BlockId::Glowstone, 0.3f},
    };
    for (const auto& [id, hardness] : beta) {
        EXPECT_FLOAT_EQ(voxel::BlockRegistry::get(id).hardness, hardness) << voxel::BlockRegistry::get(id).name;
    }
}

TEST_F(GameplayRegressions, TorchesSaplingsAndSugarCaneBreakOnTheClickWithoutAHitDelay) {
    for (const BlockId id : {BlockId::Torch, BlockId::Sapling, BlockId::SugarCane, BlockId::TNT}) {
        EXPECT_TRUE(gameplay::computeDig(id, {}).instant) << voxel::BlockRegistry::get(id).name;
    }
    // Two torches in a row, clicked on consecutive ticks: no blockHitDelay between them.
    const core::BlockPos second{kRock.x, kRock.y, kRock.z + 1};
    ASSERT_TRUE(m_world->setBlock(kRock, BlockId::Glass));
    ASSERT_TRUE(m_world->setBlock(second, BlockId::Glass));
    const core::BlockPos torchA{kRock.x, kRock.y + 1, kRock.z};
    const core::BlockPos torchB{second.x, second.y + 1, second.z};
    ASSERT_TRUE(m_world->setBlock(torchA, BlockId::Torch));
    ASSERT_TRUE(m_world->setBlock(torchB, BlockId::Torch));
    const game::GameTickEvents a = m_session.tick(
        *m_world, m_player, {.attackPressed = true, .attackHeld = true, .target = targetAt(torchA, BlockId::Torch)});
    const game::GameTickEvents b = m_session.tick(
        *m_world, m_player, {.attackPressed = true, .attackHeld = true, .target = targetAt(torchB, BlockId::Torch)});
    EXPECT_TRUE(a.blockBroken.has_value());
    EXPECT_TRUE(b.blockBroken.has_value());
}

TEST(HardnessRegressions, DigTicksFollowBetaHardness) {
    voxel::BlockRegistry::initialize();
    using gameplay::ToolKind;
    using gameplay::ToolMaterial;
    EXPECT_EQ(gameplay::computeDig(BlockId::OakLog, {}).ticks, 60); // 2.0 x 30
    const gameplay::ItemStack diamondPick =
        gameplay::makeStack(gameplay::toolItemId(ToolKind::Pickaxe, ToolMaterial::Diamond), 1);
    // Obsidian is not in Beta's ItemPickaxe effective list: the diamond pickaxe only makes it
    // harvestable, at speed 1.
    EXPECT_EQ(gameplay::computeDig(BlockId::Obsidian, diamondPick).ticks, 300); // 10 x 30
}

// --- Vanilla drops and their recipes -----------------------------------------------------

TEST_F(GameplayRegressions, DiamondRedstoneAndSnowOresDropTheirBetaItems) {
    using gameplay::ToolKind;
    using gameplay::ToolMaterial;
    for (uint64_t seed = 0; seed < 32; ++seed) {
        gameplay::ItemStack iron = toolStack(ToolKind::Pickaxe, ToolMaterial::Iron);
        const auto diamond = gameplay::breakBlockWith(BlockId::DiamondOre, iron, seed).drop;
        ASSERT_TRUE(diamond.has_value());
        EXPECT_EQ(diamond->id, items::Diamond);
        EXPECT_EQ(diamond->count, 1);

        const auto redstone = gameplay::breakBlockWith(BlockId::RedstoneOre, iron, seed).drop;
        ASSERT_TRUE(redstone.has_value());
        EXPECT_EQ(redstone->id, items::Redstone);
        EXPECT_GE(redstone->count, 4); // Beta BlockRedstoneOre: 4 + nextInt(2)
        EXPECT_LE(redstone->count, 5);

        gameplay::ItemStack shovel = toolStack(ToolKind::Shovel, ToolMaterial::Wood);
        const auto snow = gameplay::breakBlockWith(BlockId::SnowBlock, shovel, seed).drop;
        ASSERT_TRUE(snow.has_value());
        EXPECT_EQ(snow->id, items::Snowball);
        EXPECT_EQ(snow->count, 4);
    }
}

TEST(RecipeRegressions, GlowstoneDustAndSnowballsCraftBackIntoTheirBlocks) {
    voxel::BlockRegistry::initialize();
    const crafting::RecipeResult glowstone = craft2x2(items::GlowstoneDust);
    EXPECT_EQ(glowstone.id, gameplay::toItemId(BlockId::Glowstone));
    EXPECT_EQ(glowstone.count, 1);
    const crafting::RecipeResult snow = craft2x2(items::Snowball);
    EXPECT_EQ(snow.id, gameplay::toItemId(BlockId::SnowBlock));
    EXPECT_EQ(snow.count, 1);
}

// --- Blocks that lose their support ------------------------------------------------------

TEST_F(GameplayRegressions, MiningTheBlockUnderAFlowerDropsTheFlowerToo) {
    ASSERT_TRUE(m_world->setBlock(kRock, BlockId::Dirt));
    const core::BlockPos above{kRock.x, kRock.y + 1, kRock.z};
    ASSERT_TRUE(m_world->setBlock(above, BlockId::Rose));
    const MineResult r = mine(kRock);
    ASSERT_TRUE(r.broken.has_value());
    EXPECT_EQ(m_world->getBlock(above), BlockId::Air);
    const auto drops = droppedItems();
    EXPECT_EQ(drops.at(gameplay::toItemId(BlockId::Dirt)), 1);
    ASSERT_TRUE(drops.contains(gameplay::toItemId(BlockId::Rose)));
    EXPECT_EQ(drops.at(gameplay::toItemId(BlockId::Rose)), 1);
}

TEST_F(GameplayRegressions, MiningTheBlockUnderAStackOfSugarCaneDropsEveryCane) {
    ASSERT_TRUE(m_world->setBlock(kRock, BlockId::Sand));
    for (int32_t i = 1; i <= 3; ++i) {
        ASSERT_TRUE(m_world->setBlock(kRock.x, kRock.y + i, kRock.z, BlockId::SugarCane));
    }
    ASSERT_TRUE(mine(kRock).broken.has_value());
    EXPECT_EQ(droppedItems()[gameplay::toItemId(BlockId::SugarCane)], 3);
}

TEST(SupportRegressions, ATorchThatLosesItsSupportTakesItsLightWithIt) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir{"betacraft_p2_fixes_support_"};
    world::World w(test::p2worldgen::kSeed, dir.path());
    test::p2worldgen::loadAll(w, test::p2worldgen::squareRegion({0, 0}, 1));
    // High above the terrain: no other block light anywhere near.
    const core::BlockPos base{8, 110, 8};
    ASSERT_TRUE(w.setBlock(base, BlockId::Dirt));
    ASSERT_TRUE(w.setBlock(base.x, base.y + 1, base.z, BlockId::Torch));
    ASSERT_EQ(w.getBlockLight(base.x, base.y + 1, base.z), 14);

    ASSERT_TRUE(w.setBlock(base, BlockId::Air));
    ASSERT_EQ(w.getBlock(base.x, base.y + 1, base.z), BlockId::Air);
    for (int32_t dy = -15; dy <= 15; ++dy) {
        for (int32_t dz = -15; dz <= 15; ++dz) {
            for (int32_t dx = -15; dx <= 15; ++dx) {
                ASSERT_EQ(w.getBlockLight(base.x + dx, base.y + dy, base.z + dz), 0)
                    << "phantom light at " << dx << "," << dy << "," << dz;
            }
        }
    }
    EXPECT_EQ(w.takeUnsupportedBreaks().size(), 1U);
    EXPECT_TRUE(w.takeUnsupportedBreaks().empty());
}

// --- Lava is not water ---------------------------------------------------------------------

TEST_F(GameplayRegressions, LavaNeitherDrownsNorCountsAsWaterForTheHead) {
    for (int32_t y = kY; y <= kY + 2; ++y) {
        ASSERT_TRUE(m_world->setBlock(2, y, 2, BlockId::Lava));
    }
    for (int i = 0; i < 120; ++i) {
        (void)m_player.fixedUpdate(1.0f / 60.0f, {}, *m_world);
    }
    EXPECT_FALSE(m_player.isHeadUnderWater());
    EXPECT_FLOAT_EQ(m_player.getAirRatio(), 1.0f);

    // The same column in water does put the head under water.
    for (int32_t y = kY; y <= kY + 2; ++y) {
        ASSERT_TRUE(m_world->setBlock(2, y, 2, BlockId::Water));
    }
    (void)m_player.fixedUpdate(1.0f / 60.0f, {}, *m_world);
    EXPECT_TRUE(m_player.isHeadUnderWater());
}

TEST_F(GameplayRegressions, FallingIntoLavaDoesNotSplash) {
    for (int32_t y = kY; y <= kY + 1; ++y) {
        for (int32_t z = 1; z <= 3; ++z) {
            for (int32_t x = 1; x <= 3; ++x) {
                ASSERT_TRUE(m_world->setBlock(x, y, z, BlockId::Lava));
            }
        }
    }
    m_player.setPosition({2.5f, static_cast<float>(kY + 8), 2.5f});
    bool splashed = false;
    for (int i = 0; i < 120; ++i) {
        splashed = m_player.fixedUpdate(1.0f / 60.0f, {}, *m_world).splashedWater || splashed;
    }
    EXPECT_FALSE(splashed);
}

// --- Placing a fluid into flowing fluid ----------------------------------------------------

TEST_F(GameplayRegressions, PlacingWaterIntoFlowingWaterMakesASource) {
    const core::BlockPos cell{4, kY, 2};
    ASSERT_TRUE(m_world->setBlockWithMetadata(cell.x, cell.y, cell.z, BlockId::Water, 3));
    hold({BlockId::Water, 2});
    physics::RaycastHit target = targetAt({cell.x + 1, cell.y, cell.z}, BlockId::Glass);
    target.adjacentPos = cell;

    const std::optional<BlockId> placed = game::placeHeldBlock(*m_world, m_player, target);
    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(*placed, BlockId::Water);
    EXPECT_EQ(m_world->getBlock(cell), BlockId::Water);
    EXPECT_EQ(m_world->getMetadata(cell), 0);
    EXPECT_EQ(m_player.getHeldItem(), InventorySlot(BlockId::Water, 1));
}

TEST_F(GameplayRegressions, PlacingABlockFollowsTheReplaceableAndOverlapRules) {
    hold({BlockId::Stone, 3});
    const core::BlockPos cell{4, kY, 2};
    physics::RaycastHit target = targetAt({cell.x + 1, cell.y, cell.z}, BlockId::Glass);
    target.adjacentPos = cell;
    ASSERT_TRUE(game::placeHeldBlock(*m_world, m_player, target).has_value());
    EXPECT_EQ(m_world->getBlock(cell), BlockId::Stone);
    EXPECT_EQ(m_player.getHeldItem().count, 2);

    // Occupied by a non-replaceable block now.
    EXPECT_FALSE(game::placeHeldBlock(*m_world, m_player, target).has_value());
    // Inside the player's own box.
    target.adjacentPos = {2, kY, 2};
    EXPECT_FALSE(game::placeHeldBlock(*m_world, m_player, target).has_value());
    EXPECT_EQ(m_player.getHeldItem().count, 2);
}
