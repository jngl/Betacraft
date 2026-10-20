// Phase 2 integration, data foundations: ONE item id scheme shared by the tools/combat
// module (src/gameplay) and the crafting module (src/crafting).
#include "crafting/BetaRecipes.hpp"
#include "crafting/CraftingGrid.hpp"
#include "crafting/ItemIds.hpp"
#include "crafting/RecipeRegistry.hpp"
#include "gameplay/Harvesting.hpp"
#include "gameplay/ItemIds.hpp"
#include "gameplay/ItemStack.hpp"
#include "gameplay/ToolDefinitions.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>

#include <array>
#include <type_traits>
#include <utility>

using namespace betacraft;
using world::BlockId;

namespace {

constexpr std::array<gameplay::ToolKind, 5> kKinds{gameplay::ToolKind::Pickaxe, gameplay::ToolKind::Axe,
                                                   gameplay::ToolKind::Shovel, gameplay::ToolKind::Sword,
                                                   gameplay::ToolKind::Hoe};
constexpr std::array<gameplay::ToolMaterial, 5> kMaterials{
    gameplay::ToolMaterial::Wood, gameplay::ToolMaterial::Stone, gameplay::ToolMaterial::Iron,
    gameplay::ToolMaterial::Gold, gameplay::ToolMaterial::Diamond};

// Cross-module hook from the crafting notes: durability has a single source of truth.
static_assert(crafting::maxDurability(crafting::toolItemId(crafting::ToolKind::Pickaxe, crafting::ToolMaterial::Wood)) ==
              gameplay::materialStats(gameplay::ToolMaterial::Wood).maxUses);
static_assert(crafting::maxDurability(gameplay::items::DiamondSword) ==
              gameplay::materialStats(gameplay::ToolMaterial::Diamond).maxUses);

} // namespace

TEST(P2FoundationsItems, BothModulesUseTheSameIdTypeAndEnums) {
    static_assert(std::is_same_v<crafting::ItemId, gameplay::ItemId>);
    static_assert(std::is_same_v<crafting::ToolMaterial, gameplay::ToolMaterial>);
    static_assert(std::is_same_v<crafting::ToolKind, gameplay::ToolKind>);
    EXPECT_EQ(crafting::kAir, gameplay::kNoItem);
}

TEST(P2FoundationsItems, SharedItemConstantsCoverToolsAndMaterials) {
    namespace items = gameplay::items;
    // S2.10: Beta numbers; the legacy BlockIds fold onto them.
    EXPECT_EQ(items::Stick, gameplay::canonicalize(gameplay::toItemId(BlockId::Stick)));
    EXPECT_EQ(items::Coal, gameplay::canonicalize(gameplay::toItemId(BlockId::CoalItem)));
    EXPECT_EQ(items::GlowstoneDust, 348);
    EXPECT_EQ(items::LavaBucket, 327);
    EXPECT_EQ(items::WaterBucket, 326);
    EXPECT_EQ(items::Bucket, 325);
    EXPECT_EQ(items::Snowball, 332);
    EXPECT_EQ(gameplay::kGlowstoneDustItem, items::GlowstoneDust);
    // crafting re-exports the very same constants.
    EXPECT_EQ(&crafting::items::GlowstoneDust, &items::GlowstoneDust);
    EXPECT_EQ(crafting::items::IronIngot, items::IronIngot);
}

TEST(P2FoundationsItems, ToolIdsResolveIdenticallyInBothModules) {
    for (const gameplay::ToolKind k : kKinds) {
        for (const gameplay::ToolMaterial m : kMaterials) {
            const gameplay::ItemId id = crafting::toolItemId(k, m);
            EXPECT_EQ(gameplay::toolItemId(gameplay::toToolType(k), m), id);
            const auto def = gameplay::findTool(id);
            ASSERT_TRUE(def.has_value()) << id;
            EXPECT_EQ(def->type, gameplay::toToolType(k));
            EXPECT_EQ(def->material, m);
            EXPECT_EQ(def->maxDamage, crafting::maxDurability(id)) << id;
            EXPECT_EQ(gameplay::makeStack(id, 1).maxDamage, crafting::maxDurability(id)) << id;
        }
    }
}

// crafting re-exports gameplay's tables (crafting/ItemIds.hpp `using` declarations), so a
// loop comparing the two modules would compare a function with itself. Both names are
// checked against Beta 1.7.3 values instead, which still catches a split that diverges.

TEST(P2FoundationsItems, VanillaAliasesFoldToTheEngineIds) {
    namespace items = gameplay::items;
    // Vanilla item numbers of the items that already had a BlockId-range engine id.
    const std::pair<gameplay::ItemId, gameplay::ItemId> aliases[] = {
        {280, items::Stick},       {263, items::Coal},         {270, items::WoodPickaxe},
        {274, items::StonePickaxe}, {257, items::IronPickaxe}, {278, items::DiamondPickaxe},
    };
    for (const auto& [vanilla, engine] : aliases) {
        EXPECT_EQ(crafting::canonicalize(vanilla), engine) << vanilla;
        EXPECT_EQ(gameplay::canonicalize(vanilla), engine) << vanilla;
        EXPECT_EQ(crafting::toVanillaId(engine), vanilla) << vanilla;
        EXPECT_EQ(gameplay::toVanillaId(engine), vanilla) << vanilla;
        EXPECT_EQ(crafting::canonicalize(engine), engine) << vanilla;
    }
    // Everything else is its own vanilla id.
    for (const gameplay::ItemId id : {items::IronIngot, items::Diamond, items::Bucket, items::DiamondSword,
                                      gameplay::toItemId(BlockId::Stone), gameplay::toItemId(BlockId::Glowstone)}) {
        EXPECT_EQ(crafting::canonicalize(id), id) << id;
        EXPECT_EQ(gameplay::toVanillaId(id), id) << id;
    }
    // Vanilla tool numbers resolve to tools; materials and blocks do not.
    EXPECT_TRUE(crafting::toolInfo(270).has_value());
    EXPECT_TRUE(gameplay::isTool(270));
    EXPECT_TRUE(gameplay::isTool(items::GoldHoe));
    EXPECT_FALSE(crafting::toolInfo(items::Stick).has_value());
    EXPECT_FALSE(gameplay::isTool(items::Stick));
    EXPECT_FALSE(gameplay::isTool(gameplay::toItemId(BlockId::Cobblestone)));
}

TEST(P2FoundationsItems, StackLimitsFollowBeta) {
    namespace items = gameplay::items;
    const std::pair<gameplay::ItemId, int32_t> limits[] = {
        {items::Egg, 16},         {items::Snowball, 16},      {items::Cookie, 8},
        {items::Bucket, 1},       {items::WaterBucket, 1},    {items::LavaBucket, 1},
        {items::MilkBucket, 1},   {items::Cake, 1},           {items::WoodPickaxe, 1},
        {items::DiamondSword, 1}, {items::Stick, 64},         {items::Diamond, 64},
        {items::GlowstoneDust, 64}, {items::Redstone, 64},    {gameplay::toItemId(BlockId::Cobblestone), 64},
        {323, 1},                 {297, 1},                   {2256, 1}, // Sign, bread, record "13"
    };
    for (const auto& [id, limit] : limits) {
        EXPECT_EQ(gameplay::maxStackSize(id), limit) << id;
        // The crafting grid's narrowed count type keeps the same value.
        EXPECT_EQ(static_cast<int32_t>(crafting::maxStackSize(id)), limit) << id;
    }
}

TEST(P2FoundationsItems, CraftedToolCarriesCombatDurability) {
    // Crafting a wooden pickaxe yields an item whose damage budget is exactly the one the
    // combat module wears down.
    const crafting::RecipeRegistry book = crafting::makeBetaRecipeBook();
    crafting::CraftingGrid grid{crafting::GridSize::Workbench3x3};
    const crafting::ItemId planks = crafting::toItemId(BlockId::OakPlanks);
    ASSERT_TRUE(grid.set(0, 0, {planks, 1, 0}));
    ASSERT_TRUE(grid.set(1, 0, {planks, 1, 0}));
    ASSERT_TRUE(grid.set(2, 0, {planks, 1, 0}));
    ASSERT_TRUE(grid.set(1, 1, {crafting::items::Stick, 1, 0}));
    ASSERT_TRUE(grid.set(1, 2, {crafting::items::Stick, 1, 0}));
    const auto match = book.match(grid);
    ASSERT_TRUE(match.has_value());
    const gameplay::ItemStack held = gameplay::makeStack(match->result.id, match->result.count);
    const auto tool = gameplay::toolOf(held);
    ASSERT_TRUE(tool.has_value());
    EXPECT_EQ(tool->type, gameplay::ToolType::Pickaxe);
    EXPECT_EQ(held.maxDamage, gameplay::materialStats(gameplay::ToolMaterial::Wood).maxUses);
}

TEST(P2FoundationsItems, GlowstoneDustDropIsAKnownCraftingItem) {
    gameplay::ItemStack hand{};
    const gameplay::BlockBreakOutcome out = gameplay::breakBlockWith(BlockId::Glowstone, hand, 1234);
    ASSERT_TRUE(out.drop.has_value());
    EXPECT_EQ(out.drop->id, crafting::items::GlowstoneDust);
    EXPECT_EQ(crafting::maxStackSize(out.drop->id), 64);
}
