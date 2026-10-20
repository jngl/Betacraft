#include "gameplay/Harvesting.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace betacraft::gameplay;
using betacraft::voxel::BlockId;
using betacraft::voxel::BlockRegistry;

namespace {
ItemStack tool(ToolType type, ToolMaterial mat) {
    return makeStack(toolItemId(type, mat), 1);
}
const ItemStack kHand{};

constexpr std::array<ToolMaterial, 5> kMaterials{ToolMaterial::Wood, ToolMaterial::Stone,
                                                 ToolMaterial::Iron, ToolMaterial::Gold,
                                                 ToolMaterial::Diamond};
constexpr std::array<ToolType, 5> kTypes{ToolType::Pickaxe, ToolType::Axe, ToolType::Shovel,
                                         ToolType::Sword, ToolType::Hoe};

// Every placeable block id of the engine.
std::vector<BlockId> placeableBlocks() {
    std::vector<BlockId> ids;
    for (int i = 1; i < static_cast<int>(BlockId::Count); ++i) {
        const auto id = static_cast<BlockId>(i);
        const auto& def = BlockRegistry::get(id);
        const bool registered = def.id == id && def.name != betacraft::voxel::BlockDef{}.name;
        if (registered && !def.isItemOnly) {
            ids.push_back(id);
        }
    }
    return ids;
}
} // namespace

// ---------------------------------------------------------------------------
// Block harvest table
// ---------------------------------------------------------------------------

TEST(HarvestTable, CoversEveryRegisteredBlock) {
    EXPECT_GE(placeableBlocks().size(), 40u);
}

TEST(HarvestTable, EveryRegisteredBlockHasADeliberateRule) {
    // Pins {effective tool, required tool, harvest level} for every placeable block, so a new
    // block (e.g. a stone stair) cannot silently fall back to "hand-harvestable": adding one
    // to voxel::BlockRegistry makes this test fail until a rule is chosen here.
    struct Expected {
        BlockId id;
        ToolType effective;
        ToolType required;
        int32_t level;
    };
    constexpr ToolType P = ToolType::Pickaxe;
    constexpr ToolType A = ToolType::Axe;
    constexpr ToolType S = ToolType::Shovel;
    constexpr ToolType N = ToolType::None;
    const std::vector<Expected> table{
        {BlockId::Stone, P, P, 0},        {BlockId::Grass, S, N, 0},
        {BlockId::Dirt, S, N, 0},         {BlockId::Cobblestone, P, P, 0},
        {BlockId::OakPlanks, A, N, 0},    {BlockId::Sapling, N, N, 0},
        {BlockId::Bedrock, N, N, 0},      {BlockId::Water, N, N, 0},
        {BlockId::StillWater, N, N, 0},   {BlockId::Lava, N, N, 0},
        {BlockId::StillLava, N, N, 0},
        {BlockId::Sand, S, N, 0},         {BlockId::Gravel, S, N, 0},
        {BlockId::GoldOre, P, P, 2},      {BlockId::IronOre, P, P, 1},
        {BlockId::CoalOre, P, P, 0},      {BlockId::OakLog, A, N, 0},
        {BlockId::Leaves, N, N, 0},       {BlockId::Glass, N, N, 0},
        {BlockId::LapisOre, P, P, 1},     {BlockId::Sandstone, P, P, 0},
        {BlockId::TallGrass, N, N, 0},    {BlockId::DeadBush, N, N, 0},
        {BlockId::Piston, N, N, 0},       {BlockId::PistonHead, N, N, 0},
        {BlockId::Wool, N, N, 0},         {BlockId::Dandelion, N, N, 0},
        {BlockId::Rose, N, N, 0},         {BlockId::CobbleSlab, P, P, 0},
        {BlockId::Bricks, N, P, 0},       {BlockId::TNT, N, N, 0},
        {BlockId::Bookshelf, A, N, 0},    {BlockId::MossyCobble, P, P, 0},
        {BlockId::Obsidian, N, P, 3},     {BlockId::Torch, N, N, 0},
        {BlockId::DiamondOre, P, P, 2},   {BlockId::CraftingTable, N, N, 0},
        {BlockId::Furnace, N, P, 0},      {BlockId::RedstoneOre, N, P, 2},
        {BlockId::SnowGrass, S, N, 0},    {BlockId::Ice, P, N, 0},
        {BlockId::SnowBlock, S, S, 0},    {BlockId::Cactus, N, N, 0},
        {BlockId::Clay, S, N, 0},         {BlockId::SugarCane, N, N, 0},
        {BlockId::Glowstone, N, N, 0},    {BlockId::BirchLog, A, N, 0},
        {BlockId::PineLog, A, N, 0},      {BlockId::Chest, A, N, 0},
        {BlockId::LitFurnace, N, P, 0},
        // S5.4 shapes: only the cobblestone stairs need a pickaxe, only farmland is shovel-fast
        {BlockId::Bed, N, N, 0},          {BlockId::WoodStairs, N, N, 0},
        {BlockId::Crops, N, N, 0},        {BlockId::Farmland, S, N, 0},
        {BlockId::WoodDoor, N, N, 0},     {BlockId::Ladder, N, N, 0},
        {BlockId::CobbleStairs, N, P, 0}, {BlockId::Fence, N, N, 0},
        // S6.10: Material.rock without a place in ItemPickaxe's list
        {BlockId::MobSpawner, N, P, 0},
        // S8.5 redstone: the stone plate (rock) and the iron door (iron) need a pickaxe
        {BlockId::RedstoneWire, N, N, 0}, {BlockId::Lever, N, N, 0},
        {BlockId::StonePressurePlate, N, P, 0}, {BlockId::IronDoor, N, P, 0},
        {BlockId::WoodPressurePlate, N, N, 0}, {BlockId::RedstoneTorchOff, N, N, 0},
        {BlockId::RedstoneTorchOn, N, N, 0}, {BlockId::StoneButton, N, N, 0},
    };
    for (const BlockId id : placeableBlocks()) {
        const auto it = std::find_if(table.begin(), table.end(), [id](const Expected& e) { return e.id == id; });
        ASSERT_NE(it, table.end()) << "no deliberate harvest rule for block "
                                   << BlockRegistry::get(id).name << " (" << static_cast<int>(id) << ")";
        const BlockHarvestInfo info = harvestInfo(id);
        EXPECT_EQ(info.effectiveTool, it->effective) << BlockRegistry::get(id).name;
        EXPECT_EQ(info.requiredTool, it->required) << BlockRegistry::get(id).name;
        EXPECT_EQ(info.minHarvestLevel, it->level) << BlockRegistry::get(id).name;
    }
    EXPECT_EQ(table.size(), placeableBlocks().size()) << "table lists a block the registry lacks";
}

TEST(HarvestTable, HardnessComesFromBlockRegistry) {
    for (BlockId id : placeableBlocks()) {
        EXPECT_FLOAT_EQ(harvestInfo(id).hardness, BlockRegistry::get(id).hardness)
            << BlockRegistry::get(id).name;
    }
}

TEST(HarvestTable, OreHarvestLevels) {
    // Roadmap P2.5: an iron pickaxe (level 2) is mandatory for diamond and gold.
    EXPECT_EQ(harvestInfo(BlockId::CoalOre).minHarvestLevel, 0);
    EXPECT_EQ(harvestInfo(BlockId::IronOre).minHarvestLevel, 1);
    EXPECT_EQ(harvestInfo(BlockId::LapisOre).minHarvestLevel, 1);
    EXPECT_EQ(harvestInfo(BlockId::GoldOre).minHarvestLevel, 2);
    EXPECT_EQ(harvestInfo(BlockId::DiamondOre).minHarvestLevel, 2);
    EXPECT_EQ(harvestInfo(BlockId::RedstoneOre).minHarvestLevel, 2);
    EXPECT_EQ(harvestInfo(BlockId::Obsidian).minHarvestLevel, 3);
    for (BlockId ore : {BlockId::CoalOre, BlockId::IronOre, BlockId::LapisOre, BlockId::GoldOre,
                        BlockId::DiamondOre, BlockId::RedstoneOre, BlockId::Obsidian}) {
        EXPECT_EQ(harvestInfo(ore).requiredTool, ToolType::Pickaxe);
    }
}

TEST(HarvestTable, RockNeedsAnyPickaxe) {
    for (BlockId rock : {BlockId::Stone, BlockId::Cobblestone, BlockId::MossyCobble,
                         BlockId::Sandstone, BlockId::CobbleSlab, BlockId::Bricks,
                         BlockId::Furnace}) {
        const auto info = harvestInfo(rock);
        EXPECT_EQ(info.requiredTool, ToolType::Pickaxe) << static_cast<int>(rock);
        EXPECT_EQ(info.minHarvestLevel, 0);
    }
}

TEST(HarvestTable, SnowBlockNeedsShovel) {
    EXPECT_EQ(harvestInfo(BlockId::SnowBlock).requiredTool, ToolType::Shovel);
}

TEST(HarvestTable, SoftBlocksDropByHand) {
    for (BlockId soft : {BlockId::Dirt, BlockId::Grass, BlockId::Sand, BlockId::Gravel,
                         BlockId::OakLog, BlockId::OakPlanks, BlockId::Leaves, BlockId::Wool,
                         BlockId::CraftingTable, BlockId::Glass, BlockId::Glowstone,
                         BlockId::Torch, BlockId::Ice, BlockId::Piston}) {
        EXPECT_EQ(harvestInfo(soft).requiredTool, ToolType::None) << static_cast<int>(soft);
    }
}

TEST(HarvestTable, EffectiveToolsFollowBetaLists) {
    EXPECT_EQ(harvestInfo(BlockId::Stone).effectiveTool, ToolType::Pickaxe);
    EXPECT_EQ(harvestInfo(BlockId::Ice).effectiveTool, ToolType::Pickaxe);
    EXPECT_EQ(harvestInfo(BlockId::DiamondOre).effectiveTool, ToolType::Pickaxe);
    // Beta 1.7.3 quirk: obsidian, redstone ore, bricks and furnaces need a pickaxe but get
    // no efficiency bonus from it.
    EXPECT_EQ(harvestInfo(BlockId::Obsidian).effectiveTool, ToolType::None);
    EXPECT_EQ(harvestInfo(BlockId::RedstoneOre).effectiveTool, ToolType::None);
    EXPECT_EQ(harvestInfo(BlockId::Bricks).effectiveTool, ToolType::None);
    EXPECT_EQ(harvestInfo(BlockId::Furnace).effectiveTool, ToolType::None);

    for (BlockId wood : {BlockId::OakLog, BlockId::BirchLog, BlockId::PineLog,
                         BlockId::OakPlanks, BlockId::Bookshelf}) {
        EXPECT_EQ(harvestInfo(wood).effectiveTool, ToolType::Axe) << static_cast<int>(wood);
    }
    for (BlockId soil : {BlockId::Grass, BlockId::Dirt, BlockId::Sand, BlockId::Gravel,
                         BlockId::SnowBlock, BlockId::Clay, BlockId::SnowGrass}) {
        EXPECT_EQ(harvestInfo(soil).effectiveTool, ToolType::Shovel) << static_cast<int>(soil);
    }
}

TEST(HarvestTable, UnbreakableAndNonBlocks) {
    EXPECT_FALSE(harvestInfo(BlockId::Bedrock).breakable());
    EXPECT_FALSE(harvestInfo(BlockId::Water).breakable());
    EXPECT_FALSE(harvestInfo(BlockId::Air).breakable());
    EXPECT_FALSE(harvestInfo(BlockId::Stick).breakable()) << "item-only ids are never in the world";
    EXPECT_FALSE(harvestInfo(BlockId::DiamondPickaxe).breakable());
    EXPECT_FALSE(harvestInfo(static_cast<BlockId>(200)).breakable());
    EXPECT_FALSE(harvestInfo(static_cast<BlockId>(19)).breakable()) << "gap in the id space";
    EXPECT_TRUE(harvestInfo(BlockId::Stone).breakable());
}

// ---------------------------------------------------------------------------
// canHarvest
// ---------------------------------------------------------------------------

TEST(CanHarvest, HandOnlyHarvestsToollessBlocks) {
    EXPECT_TRUE(canHarvest(harvestInfo(BlockId::Dirt), kHand));
    EXPECT_FALSE(canHarvest(harvestInfo(BlockId::Stone), kHand));
    EXPECT_FALSE(canHarvest(harvestInfo(BlockId::SnowBlock), kHand));
}

TEST(CanHarvest, WrongToolTypeCannotHarvestRock) {
    EXPECT_FALSE(canHarvest(harvestInfo(BlockId::Stone), tool(ToolType::Shovel, ToolMaterial::Diamond)));
    EXPECT_FALSE(canHarvest(harvestInfo(BlockId::Stone), tool(ToolType::Sword, ToolMaterial::Diamond)));
    EXPECT_FALSE(canHarvest(harvestInfo(BlockId::Stone), makeStack(toItemId(BlockId::Dirt), 1)));
    EXPECT_TRUE(canHarvest(harvestInfo(BlockId::SnowBlock), tool(ToolType::Shovel, ToolMaterial::Wood)));
    EXPECT_FALSE(canHarvest(harvestInfo(BlockId::SnowBlock), tool(ToolType::Pickaxe, ToolMaterial::Diamond)));
}

TEST(CanHarvest, PickaxeTierMatrix) {
    struct Row { BlockId block; bool wood, stone, iron, gold, diamond; };
    const std::array<Row, 8> rows{{
        {BlockId::Stone,       true,  true,  true,  true,  true},
        {BlockId::CoalOre,     true,  true,  true,  true,  true},
        {BlockId::IronOre,     false, true,  true,  false, true},
        {BlockId::LapisOre,    false, true,  true,  false, true},
        {BlockId::GoldOre,     false, false, true,  false, true},
        {BlockId::DiamondOre,  false, false, true,  false, true},
        {BlockId::RedstoneOre, false, false, true,  false, true},
        {BlockId::Obsidian,    false, false, false, false, true},
    }};
    for (const Row& r : rows) {
        const auto info = harvestInfo(r.block);
        EXPECT_EQ(canHarvest(info, tool(ToolType::Pickaxe, ToolMaterial::Wood)), r.wood) << static_cast<int>(r.block);
        EXPECT_EQ(canHarvest(info, tool(ToolType::Pickaxe, ToolMaterial::Stone)), r.stone) << static_cast<int>(r.block);
        EXPECT_EQ(canHarvest(info, tool(ToolType::Pickaxe, ToolMaterial::Iron)), r.iron) << static_cast<int>(r.block);
        EXPECT_EQ(canHarvest(info, tool(ToolType::Pickaxe, ToolMaterial::Gold)), r.gold) << static_cast<int>(r.block);
        EXPECT_EQ(canHarvest(info, tool(ToolType::Pickaxe, ToolMaterial::Diamond)), r.diamond) << static_cast<int>(r.block);
    }
}

TEST(CanHarvest, PropertyHigherHarvestLevelNeverLosesAbility) {
    for (BlockId id : placeableBlocks()) {
        const auto info = harvestInfo(id);
        for (ToolType type : kTypes) {
            for (ToolMaterial a : kMaterials) {
                for (ToolMaterial b : kMaterials) {
                    if (materialStats(b).harvestLevel < materialStats(a).harvestLevel) {
                        continue;
                    }
                    if (canHarvest(info, tool(type, a))) {
                        EXPECT_TRUE(canHarvest(info, tool(type, b)))
                            << static_cast<int>(id) << " type " << static_cast<int>(type);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Tool speed (Beta getStrVsBlock)
// ---------------------------------------------------------------------------

TEST(ToolSpeed, EffectiveToolUsesMaterialEfficiency) {
    for (ToolMaterial m : kMaterials) {
        EXPECT_FLOAT_EQ(toolSpeed(harvestInfo(BlockId::Stone), tool(ToolType::Pickaxe, m)),
                        materialStats(m).efficiency);
        EXPECT_FLOAT_EQ(toolSpeed(harvestInfo(BlockId::OakLog), tool(ToolType::Axe, m)),
                        materialStats(m).efficiency);
        EXPECT_FLOAT_EQ(toolSpeed(harvestInfo(BlockId::Dirt), tool(ToolType::Shovel, m)),
                        materialStats(m).efficiency);
    }
}

TEST(ToolSpeed, IneffectiveToolIsHandSpeed) {
    EXPECT_FLOAT_EQ(toolSpeed(harvestInfo(BlockId::Dirt), tool(ToolType::Pickaxe, ToolMaterial::Diamond)), 1.0f);
    EXPECT_FLOAT_EQ(toolSpeed(harvestInfo(BlockId::Obsidian), tool(ToolType::Pickaxe, ToolMaterial::Diamond)), 1.0f);
    EXPECT_FLOAT_EQ(toolSpeed(harvestInfo(BlockId::Stone), kHand), 1.0f);
    EXPECT_FLOAT_EQ(toolSpeed(harvestInfo(BlockId::Stone), tool(ToolType::Hoe, ToolMaterial::Diamond)), 1.0f);
}

TEST(ToolSpeed, SwordCutsEverythingAtOnePointFive) {
    for (ToolMaterial m : kMaterials) {
        EXPECT_FLOAT_EQ(toolSpeed(harvestInfo(BlockId::Leaves), tool(ToolType::Sword, m)), 1.5f);
        EXPECT_FLOAT_EQ(toolSpeed(harvestInfo(BlockId::Stone), tool(ToolType::Sword, m)), 1.5f);
    }
}

// ---------------------------------------------------------------------------
// Dig time (Beta Block.blockStrength: speed / hardness / 30 if harvestable, else 1/hardness/100)
// ---------------------------------------------------------------------------

TEST(DigTime, StoneByHandIsSlowAndDropsNothing) {
    const DigResult r = computeDig(BlockId::Stone, kHand);
    EXPECT_TRUE(r.breakable);
    EXPECT_FALSE(r.dropsItems);
    EXPECT_EQ(r.ticks, 150); // 1.5 hardness * 100
    EXPECT_FLOAT_EQ(r.seconds, 7.5f);
    EXPECT_FALSE(r.instant);
}

TEST(DigTime, StoneWithPickaxes) {
    EXPECT_EQ(computeDig(BlockId::Stone, tool(ToolType::Pickaxe, ToolMaterial::Wood)).ticks, 23);   // 22.5
    EXPECT_EQ(computeDig(BlockId::Stone, tool(ToolType::Pickaxe, ToolMaterial::Stone)).ticks, 12);  // 11.25
    EXPECT_EQ(computeDig(BlockId::Stone, tool(ToolType::Pickaxe, ToolMaterial::Iron)).ticks, 8);    // 7.5
    EXPECT_EQ(computeDig(BlockId::Stone, tool(ToolType::Pickaxe, ToolMaterial::Diamond)).ticks, 6); // 5.625
    EXPECT_EQ(computeDig(BlockId::Stone, tool(ToolType::Pickaxe, ToolMaterial::Gold)).ticks, 4);    // 3.75
    EXPECT_TRUE(computeDig(BlockId::Stone, tool(ToolType::Pickaxe, ToolMaterial::Wood)).dropsItems);
}

TEST(DigTime, AxesOnWood) {
    // Hand: hardness * 30 ticks; axe: that divided by the material efficiency.
    struct Case {
        ToolMaterial mat;
        double efficiency;
    };
    for (const BlockId wood : {BlockId::OakLog, BlockId::BirchLog, BlockId::PineLog, BlockId::OakPlanks}) {
        const float hardness = BlockRegistry::get(wood).hardness;
        const int32_t hand = static_cast<int32_t>(std::ceil(hardness * 30.0f - 1e-4f));
        const DigResult byHand = computeDig(wood, kHand);
        EXPECT_TRUE(byHand.dropsItems) << "wood drops by hand";
        EXPECT_EQ(byHand.ticks, hand) << BlockRegistry::get(wood).name;
        for (const Case c : {Case{ToolMaterial::Wood, 2}, Case{ToolMaterial::Stone, 4},
                             Case{ToolMaterial::Iron, 6}, Case{ToolMaterial::Diamond, 8},
                             Case{ToolMaterial::Gold, 12}}) {
            const auto expected = static_cast<int32_t>(std::ceil(hardness * 30.0 / c.efficiency - 1e-4));
            EXPECT_EQ(computeDig(wood, tool(ToolType::Axe, c.mat)).ticks, expected)
                << BlockRegistry::get(wood).name << " material " << static_cast<int>(c.mat);
            // A pickaxe or shovel is no faster than the hand on wood.
            EXPECT_EQ(computeDig(wood, tool(ToolType::Pickaxe, c.mat)).ticks, hand);
            EXPECT_EQ(computeDig(wood, tool(ToolType::Shovel, c.mat)).ticks, hand);
        }
    }
    // Pinned absolute values for the oak log (Beta hardness 2.0, 60 ticks by hand): wood 30,
    // diamond ceil(7.5) = 8, gold 5.
    ASSERT_FLOAT_EQ(BlockRegistry::get(BlockId::OakLog).hardness, 2.0f);
    EXPECT_EQ(computeDig(BlockId::OakLog, kHand).ticks, 60);
    EXPECT_EQ(computeDig(BlockId::OakLog, tool(ToolType::Axe, ToolMaterial::Wood)).ticks, 30);
    EXPECT_EQ(computeDig(BlockId::OakLog, tool(ToolType::Axe, ToolMaterial::Diamond)).ticks, 8);
    EXPECT_EQ(computeDig(BlockId::OakLog, tool(ToolType::Axe, ToolMaterial::Gold)).ticks, 5);
    // Grass (0.6f x 30 = 18.0000007 in float) is exactly 18 ticks: the epsilon must not add a 19th.
    EXPECT_EQ(computeDig(BlockId::Grass, kHand).ticks, 18);
}

TEST(DigTime, ShovelsOnSnowAndClay) {
    // Snow block (0.2 in Beta, engine value read from the registry) needs a shovel to drop;
    // clay drops by hand but digs faster with one.
    const float snow = BlockRegistry::get(BlockId::SnowBlock).hardness;
    const DigResult snowByHand = computeDig(BlockId::SnowBlock, kHand);
    EXPECT_FALSE(snowByHand.dropsItems);
    EXPECT_EQ(snowByHand.ticks, static_cast<int32_t>(std::ceil(snow * 100.0f - 1e-4f)));
    const DigResult snowWoodShovel = computeDig(BlockId::SnowBlock, tool(ToolType::Shovel, ToolMaterial::Wood));
    EXPECT_TRUE(snowWoodShovel.dropsItems);
    EXPECT_EQ(snowWoodShovel.ticks, static_cast<int32_t>(std::ceil(snow * 30.0 / 2.0 - 1e-4)));

    const float clay = BlockRegistry::get(BlockId::Clay).hardness;
    const DigResult clayByHand = computeDig(BlockId::Clay, kHand);
    EXPECT_TRUE(clayByHand.dropsItems);
    EXPECT_EQ(clayByHand.ticks, static_cast<int32_t>(std::ceil(clay * 30.0f - 1e-4f)));
    EXPECT_EQ(computeDig(BlockId::Clay, tool(ToolType::Shovel, ToolMaterial::Iron)).ticks,
              static_cast<int32_t>(std::ceil(clay * 30.0 / 6.0 - 1e-4)));
    // Pinned: clay 0.6 -> 18 ticks by hand, 3 with an iron shovel.
    ASSERT_FLOAT_EQ(clay, 0.6f);
    EXPECT_EQ(clayByHand.ticks, 18);
    EXPECT_EQ(computeDig(BlockId::Clay, tool(ToolType::Shovel, ToolMaterial::Iron)).ticks, 3);
}

TEST(DigTime, DirtByHandAndShovel) {
    const DigResult hand = computeDig(BlockId::Dirt, kHand);
    EXPECT_TRUE(hand.dropsItems);
    EXPECT_EQ(hand.ticks, 15);
    EXPECT_FLOAT_EQ(hand.seconds, 0.75f);
    EXPECT_EQ(computeDig(BlockId::Dirt, tool(ToolType::Shovel, ToolMaterial::Diamond)).ticks, 2);
}

TEST(DigTime, UnharvestableIgnoresToolSpeed) {
    // Beta: when the tool cannot harvest, strength = 1 / hardness / 100 regardless of tool.
    const DigResult goldOnIron = computeDig(BlockId::IronOre, tool(ToolType::Pickaxe, ToolMaterial::Gold));
    EXPECT_FALSE(goldOnIron.dropsItems);
    EXPECT_EQ(goldOnIron.ticks, 300);
    const DigResult stoneOnDiamond = computeDig(BlockId::DiamondOre, tool(ToolType::Pickaxe, ToolMaterial::Stone));
    EXPECT_FALSE(stoneOnDiamond.dropsItems);
    EXPECT_EQ(stoneOnDiamond.ticks, 300);
}

TEST(DigTime, DiamondOreWithIronPickaxe) {
    const DigResult r = computeDig(BlockId::DiamondOre, tool(ToolType::Pickaxe, ToolMaterial::Iron));
    EXPECT_TRUE(r.dropsItems);
    EXPECT_EQ(r.ticks, 15); // 3 * 30 / 6
}

TEST(DigTime, ObsidianOnlyWithDiamondAndNoEfficiencyBonus) {
    const auto hardness = static_cast<double>(BlockRegistry::get(BlockId::Obsidian).hardness);
    const DigResult diamond = computeDig(BlockId::Obsidian, tool(ToolType::Pickaxe, ToolMaterial::Diamond));
    EXPECT_TRUE(diamond.dropsItems);
    EXPECT_EQ(diamond.ticks, static_cast<int32_t>(std::ceil(hardness * 30.0)));
    const DigResult iron = computeDig(BlockId::Obsidian, tool(ToolType::Pickaxe, ToolMaterial::Iron));
    EXPECT_FALSE(iron.dropsItems);
    EXPECT_EQ(iron.ticks, static_cast<int32_t>(std::ceil(hardness * 100.0)));
}

TEST(DigTime, InstantBreakWhenStrengthReachesOne) {
    // Tall grass has hardness 0.02: 0.02 * 30 = 0.6 ticks -> broken on the click itself.
    const DigResult r = computeDig(BlockId::TallGrass, kHand);
    EXPECT_TRUE(r.instant);
    EXPECT_EQ(r.ticks, 0);
    EXPECT_FLOAT_EQ(r.seconds, 0.0f);
    EXPECT_TRUE(r.dropsItems);
    EXPECT_GE(r.progressPerTick, 1.0f);
}

TEST(DigTime, UnbreakableBlocks) {
    for (BlockId id : {BlockId::Bedrock, BlockId::Water, BlockId::Air}) {
        const DigResult r = computeDig(id, tool(ToolType::Pickaxe, ToolMaterial::Diamond));
        EXPECT_FALSE(r.breakable);
        EXPECT_FALSE(r.dropsItems);
        EXPECT_FALSE(r.instant);
        EXPECT_FLOAT_EQ(r.progressPerTick, 0.0f);
        EXPECT_EQ(r.ticks, kUnbreakableTicks);
    }
}

TEST(DigTime, HugeOrInfiniteHardnessSaturatesInsteadOfOverflowing) {
    // Custom tables (mods, future blocks) may carry absurd hardness: the tick count must
    // saturate at kUnbreakableTicks instead of an out-of-range float -> int conversion.
    for (const float hardness : {std::numeric_limits<float>::infinity(), 1e30f,
                                 std::numeric_limits<float>::max(), 1e9f}) {
        const BlockHarvestInfo info{.hardness = hardness};
        for (const DigConditions c : {DigConditions{false, true}, DigConditions{true, false}}) {
            const DigResult r = computeDig(info, kHand, c);
            EXPECT_TRUE(r.breakable) << hardness;
            EXPECT_FALSE(r.instant) << hardness;
            EXPECT_EQ(r.ticks, kUnbreakableTicks) << hardness;
            EXPECT_GE(r.progressPerTick, 0.0) << hardness;
            EXPECT_TRUE(std::isfinite(r.seconds)) << hardness;
            EXPECT_GT(r.seconds, 0.0f) << hardness;
        }
    }
}

TEST(DigTime, NaNHardnessIsUnbreakable) {
    const BlockHarvestInfo info{.hardness = std::numeric_limits<float>::quiet_NaN()};
    const DigResult r = computeDig(info, kHand);
    EXPECT_FALSE(r.breakable);
    EXPECT_EQ(r.ticks, kUnbreakableTicks);
}

TEST(DigTime, WaterAndAirbornePenalties) {
    const ItemStack pick = tool(ToolType::Pickaxe, ToolMaterial::Iron);
    const int32_t base = computeDig(BlockId::Stone, pick).ticks; // 7.5 -> 8
    EXPECT_EQ(base, 8);
    EXPECT_EQ(computeDig(BlockId::Stone, pick, {.inWater = true, .onGround = true}).ticks, 38);   // 37.5
    EXPECT_EQ(computeDig(BlockId::Stone, pick, {.inWater = false, .onGround = false}).ticks, 38);
    EXPECT_EQ(computeDig(BlockId::Stone, pick, {.inWater = true, .onGround = false}).ticks, 188); // 187.5
}

TEST(DigTime, PropertyBetaOnePointFiveVsFiveRuleByHand) {
    // By hand at 20 TPS: seconds ~= hardness * 1.5 when harvestable, hardness * 5 otherwise.
    for (BlockId id : placeableBlocks()) {
        const auto info = harvestInfo(id);
        if (!info.breakable()) {
            continue;
        }
        const DigResult r = computeDig(id, kHand);
        const float factor = r.dropsItems ? 1.5f : 5.0f;
        const float expected = info.hardness * factor;
        if (r.instant) {
            EXPECT_LE(expected, 0.05f + 1e-4f) << static_cast<int>(id);
        } else {
            EXPECT_NEAR(r.seconds, expected, 0.05f) << static_cast<int>(id);
        }
    }
}

TEST(DigTime, PropertyBetterTierNeverSlower) {
    for (BlockId id : placeableBlocks()) {
        for (ToolType type : kTypes) {
            for (ToolMaterial a : kMaterials) {
                for (ToolMaterial b : kMaterials) {
                    const bool bAtLeastAsGood =
                        materialStats(b).efficiency >= materialStats(a).efficiency &&
                        materialStats(b).harvestLevel >= materialStats(a).harvestLevel;
                    if (!bAtLeastAsGood) {
                        continue;
                    }
                    EXPECT_LE(computeDig(id, tool(type, b)).ticks, computeDig(id, tool(type, a)).ticks)
                        << static_cast<int>(id);
                }
            }
        }
    }
}

TEST(DigTime, PropertyCorrectToolNeverSlowerThanHand) {
    for (BlockId id : placeableBlocks()) {
        const auto info = harvestInfo(id);
        if (!info.breakable() || info.effectiveTool == ToolType::None) {
            continue;
        }
        for (ToolMaterial m : kMaterials) {
            EXPECT_LE(computeDig(id, tool(info.effectiveTool, m)).ticks, computeDig(id, kHand).ticks)
                << static_cast<int>(id);
        }
    }
}

TEST(DigTime, TicksConsistentWithProgressPerTick) {
    for (BlockId id : placeableBlocks()) {
        for (ToolType type : kTypes) {
            const DigResult r = computeDig(id, tool(type, ToolMaterial::Stone));
            if (!r.breakable || r.instant) {
                continue;
            }
            ASSERT_GT(r.progressPerTick, 0.0f);
            // Accumulating progress for `ticks` ticks must reach 1, one tick less must not.
            EXPECT_GE(static_cast<double>(r.progressPerTick) * r.ticks, 1.0 - 1e-4);
            EXPECT_LT(static_cast<double>(r.progressPerTick) * (r.ticks - 1), 1.0);
            EXPECT_FLOAT_EQ(r.seconds, static_cast<float>(r.ticks) / 20.0f);
        }
    }
}

// ---------------------------------------------------------------------------
// MiningProgress: per-target accumulator used by the input loop
// ---------------------------------------------------------------------------

TEST(MiningProgress, BreaksAfterExactlyTicks) {
    MiningProgress p;
    const betacraft::core::BlockPos pos{1, 64, 1};
    const DigResult dig = computeDig(BlockId::Stone, tool(ToolType::Pickaxe, ToolMaterial::Iron));
    for (int32_t t = 1; t < dig.ticks; ++t) {
        EXPECT_FALSE(p.advance(pos, dig)) << t;
    }
    EXPECT_TRUE(p.advance(pos, dig));
    EXPECT_EQ(p.elapsedTicks(), 0) << "progress resets after a break";
}

TEST(MiningProgress, InstantBreaksOnFirstAdvance) {
    MiningProgress p;
    EXPECT_TRUE(p.advance({0, 0, 0}, computeDig(BlockId::TallGrass, kHand)));
}

TEST(MiningProgress, UnbreakableNeverBreaks) {
    MiningProgress p;
    const DigResult bedrock = computeDig(BlockId::Bedrock, kHand);
    for (int i = 0; i < 10'000; ++i) {
        ASSERT_FALSE(p.advance({0, 0, 0}, bedrock));
    }
    EXPECT_EQ(p.crackStage(), -1);
}

TEST(MiningProgress, ChangingTargetResets) {
    MiningProgress p;
    const DigResult dig = computeDig(BlockId::Stone, kHand); // 150 ticks
    for (int i = 0; i < 100; ++i) {
        ASSERT_FALSE(p.advance({0, 0, 0}, dig));
    }
    EXPECT_EQ(p.elapsedTicks(), 100);
    EXPECT_FALSE(p.advance({0, 0, 1}, dig));
    EXPECT_EQ(p.elapsedTicks(), 1);
    p.reset();
    EXPECT_EQ(p.elapsedTicks(), 0);
    EXPECT_EQ(p.crackStage(), -1);
}

TEST(MiningProgress, CrackStageZeroToNine) {
    MiningProgress p;
    const DigResult dig = computeDig(BlockId::Stone, kHand); // 150 ticks
    EXPECT_EQ(p.crackStage(), -1);
    (void)p.advance({0, 0, 0}, dig);
    EXPECT_EQ(p.crackStage(), 0);
    for (int i = 1; i < 75; ++i) {
        (void)p.advance({0, 0, 0}, dig);
    }
    EXPECT_EQ(p.crackStage(), 5);
    for (int i = 75; i < 149; ++i) {
        (void)p.advance({0, 0, 0}, dig);
    }
    EXPECT_EQ(p.crackStage(), 9);
    EXPECT_NEAR(p.fraction(), 149.0f / 150.0f, 1e-5f);
}

TEST(DigTime, UnharvestableIgnoresEnvironmentPenalties) {
    // Beta Block.blockStrength returns 1 / hardness / 100 without consulting
    // getCurrentPlayerStrVsBlock, so the water / airborne /5 penalties never apply to it.
    const DigResult dry = computeDig(BlockId::Stone, kHand);
    ASSERT_FALSE(dry.dropsItems);
    EXPECT_EQ(dry.ticks, 150);
    EXPECT_EQ(computeDig(BlockId::Stone, kHand, {.inWater = true, .onGround = true}).ticks, 150);
    EXPECT_EQ(computeDig(BlockId::Stone, kHand, {.inWater = true, .onGround = false}).ticks, 150);
    const ItemStack woodPick = tool(ToolType::Pickaxe, ToolMaterial::Wood);
    EXPECT_EQ(computeDig(BlockId::Obsidian, woodPick, {.inWater = true, .onGround = false}).ticks, 1000);
}

TEST(MiningProgress, HeldBreakStartsBlockHitDelay) {
    MiningProgress p;
    const DigResult grass = computeDig(BlockId::TallGrass, kHand);
    ASSERT_TRUE(grass.instant);
    ASSERT_TRUE(p.advance({0, 0, 0}, grass));
    EXPECT_EQ(p.cooldownTicks(), kBlockHitDelayTicks);
    // The next five held ticks are swallowed, the sixth breaks the next instant block.
    for (int32_t t = 0; t < kBlockHitDelayTicks; ++t) {
        EXPECT_FALSE(p.advance({0, 1, 0}, grass)) << t;
    }
    EXPECT_EQ(p.cooldownTicks(), 0);
    EXPECT_TRUE(p.advance({0, 1, 0}, grass));
}

TEST(MiningProgress, DelayedTicksDoNotCountTowardsTheNextBlock) {
    MiningProgress p;
    const DigResult stone = computeDig(BlockId::Stone, tool(ToolType::Pickaxe, ToolMaterial::Iron));
    int32_t ticks = 0;
    while (!p.advance({0, 0, 0}, stone)) {
        ++ticks;
    }
    // Second block: delay + full dig time, so a column of N blocks costs N*ticks + (N-1)*5.
    int32_t second = 1;
    while (!p.advance({0, -1, 0}, stone)) {
        ++second;
        ASSERT_LT(second, 1000);
    }
    EXPECT_EQ(second, stone.ticks + kBlockHitDelayTicks);
}

TEST(MiningProgress, ResetClearsPendingDelay) {
    // Beta PlayerControllerSP.resetBlockRemoving (button released) zeroes blockHitDelay too.
    MiningProgress p;
    ASSERT_TRUE(p.advance({0, 0, 0}, computeDig(BlockId::TallGrass, kHand)));
    ASSERT_EQ(p.cooldownTicks(), kBlockHitDelayTicks);
    p.reset();
    EXPECT_EQ(p.cooldownTicks(), 0);
    EXPECT_EQ(p.crackStage(), -1);
    EXPECT_TRUE(p.advance({0, 1, 0}, computeDig(BlockId::TallGrass, kHand)))
        << "release + press again starts on a fresh block without waiting";
}

TEST(MiningProgress, ClickBreaksInstantBlockDespitePendingDelay) {
    // Beta clickBlock breaks strength >= 1 blocks through sendBlockRemoved directly, without
    // consulting blockHitDelay (only the held-tick path counts it down).
    MiningProgress p;
    const DigResult grass = computeDig(BlockId::TallGrass, kHand);
    ASSERT_TRUE(p.advance({0, 0, 0}, grass));
    ASSERT_GT(p.cooldownTicks(), 0);
    EXPECT_TRUE(p.click({0, 1, 0}, grass));
    EXPECT_EQ(p.crackStage(), -1);
}

TEST(MiningProgress, ClickRapidlyThroughFlowersBreaksOnePerClick) {
    MiningProgress p;
    const DigResult flower = computeDig(BlockId::Rose, kHand);
    ASSERT_TRUE(flower.instant);
    for (int32_t i = 0; i < 20; ++i) {
        EXPECT_TRUE(p.click({i, 0, 0}, flower)) << i;
    }
}

TEST(MiningProgress, ClickOnSlowOrUnbreakableBlockDoesNotBreakOrProgress) {
    // Non-instant blocks are mined by the held ticks that follow the click.
    MiningProgress p;
    EXPECT_FALSE(p.click({0, 0, 0}, computeDig(BlockId::Stone, kHand)));
    EXPECT_EQ(p.elapsedTicks(), 0);
    EXPECT_FLOAT_EQ(p.fraction(), 0.0f);
    EXPECT_FALSE(p.click({0, 0, 0}, computeDig(BlockId::Bedrock, kHand)));
    EXPECT_FALSE(p.click({0, 0, 0}, DigResult{}));
}

TEST(MiningProgress, ClickDoesNotStartAHitDelay) {
    // sendBlockRemoved from clickBlock leaves blockHitDelay untouched: the held tick right
    // after a click-break already damages the block behind it.
    MiningProgress p;
    ASSERT_TRUE(p.click({0, 0, 0}, computeDig(BlockId::TallGrass, kHand)));
    EXPECT_EQ(p.cooldownTicks(), 0);
    EXPECT_TRUE(p.advance({0, -1, 0}, computeDig(BlockId::TallGrass, kHand)));
}
