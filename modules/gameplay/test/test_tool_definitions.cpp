#include "gameplay/ToolDefinitions.hpp"

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <utility>

using namespace betacraft::gameplay;

// ---------------------------------------------------------------------------
// Tool material tiers (Beta 1.7.3 EnumToolMaterial)
// ---------------------------------------------------------------------------

TEST(ToolMaterial, DurabilityMatchesBeta173) {
    EXPECT_EQ(materialStats(ToolMaterial::Wood).maxUses, 59);
    EXPECT_EQ(materialStats(ToolMaterial::Stone).maxUses, 131);
    EXPECT_EQ(materialStats(ToolMaterial::Iron).maxUses, 250);
    EXPECT_EQ(materialStats(ToolMaterial::Gold).maxUses, 32);
    EXPECT_EQ(materialStats(ToolMaterial::Diamond).maxUses, 1561);
}

TEST(ToolMaterial, HarvestLevelsZeroToThree) {
    EXPECT_EQ(materialStats(ToolMaterial::Wood).harvestLevel, 0);
    EXPECT_EQ(materialStats(ToolMaterial::Stone).harvestLevel, 1);
    EXPECT_EQ(materialStats(ToolMaterial::Iron).harvestLevel, 2);
    EXPECT_EQ(materialStats(ToolMaterial::Gold).harvestLevel, 0);
    EXPECT_EQ(materialStats(ToolMaterial::Diamond).harvestLevel, 3);
}

TEST(ToolMaterial, EfficiencyMatchesBeta173) {
    EXPECT_FLOAT_EQ(materialStats(ToolMaterial::Wood).efficiency, 2.0f);
    EXPECT_FLOAT_EQ(materialStats(ToolMaterial::Stone).efficiency, 4.0f);
    EXPECT_FLOAT_EQ(materialStats(ToolMaterial::Iron).efficiency, 6.0f);
    EXPECT_FLOAT_EQ(materialStats(ToolMaterial::Gold).efficiency, 12.0f);
    EXPECT_FLOAT_EQ(materialStats(ToolMaterial::Diamond).efficiency, 8.0f);
}

TEST(ToolMaterial, EntityDamageBonus) {
    EXPECT_EQ(materialStats(ToolMaterial::Wood).damageBonus, 0);
    EXPECT_EQ(materialStats(ToolMaterial::Stone).damageBonus, 1);
    EXPECT_EQ(materialStats(ToolMaterial::Iron).damageBonus, 2);
    EXPECT_EQ(materialStats(ToolMaterial::Gold).damageBonus, 0);
    EXPECT_EQ(materialStats(ToolMaterial::Diamond).damageBonus, 3);
}

TEST(ToolMaterial, StatsAreConstexpr) {
    static_assert(materialStats(ToolMaterial::Diamond).maxUses == 1561);
    static_assert(materialStats(ToolMaterial::Iron).harvestLevel == 2);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Tool item identification (type x material <-> ItemId)
// ---------------------------------------------------------------------------

namespace {
constexpr std::array<ToolType, 5> kAllTypes{ToolType::Pickaxe, ToolType::Axe, ToolType::Shovel,
                                            ToolType::Sword, ToolType::Hoe};
constexpr std::array<ToolMaterial, 5> kAllMaterials{ToolMaterial::Wood, ToolMaterial::Stone,
                                                    ToolMaterial::Iron, ToolMaterial::Gold,
                                                    ToolMaterial::Diamond};
} // namespace

TEST(ToolItems, LegacyPickaxesKeepEngineBlockIds) {
    // The pickaxes are Beta numbers now; the old BlockIds 102..105 only fold onto
    // them (saves with items_scheme 1).
    EXPECT_EQ(toolItemId(ToolType::Pickaxe, ToolMaterial::Wood), 270);
    EXPECT_EQ(toolItemId(ToolType::Pickaxe, ToolMaterial::Stone), 274);
    EXPECT_EQ(toolItemId(ToolType::Pickaxe, ToolMaterial::Iron), 257);
    EXPECT_EQ(toolItemId(ToolType::Pickaxe, ToolMaterial::Diamond), 278);
    for (const ItemId legacy : {102, 103, 104, 105}) {
        EXPECT_EQ(canonicalize(legacy), toolItemId(ToolType::Pickaxe, findTool(legacy)->material)) << legacy;
    }
}

TEST(ToolItems, OtherToolsUseBetaItemIds) {
    EXPECT_EQ(toolItemId(ToolType::Pickaxe, ToolMaterial::Gold), 285);
    EXPECT_EQ(toolItemId(ToolType::Shovel, ToolMaterial::Iron), 256);
    EXPECT_EQ(toolItemId(ToolType::Axe, ToolMaterial::Iron), 258);
    EXPECT_EQ(toolItemId(ToolType::Sword, ToolMaterial::Iron), 267);
    EXPECT_EQ(toolItemId(ToolType::Sword, ToolMaterial::Wood), 268);
    EXPECT_EQ(toolItemId(ToolType::Shovel, ToolMaterial::Wood), 269);
    EXPECT_EQ(toolItemId(ToolType::Axe, ToolMaterial::Wood), 271);
    EXPECT_EQ(toolItemId(ToolType::Sword, ToolMaterial::Stone), 272);
    EXPECT_EQ(toolItemId(ToolType::Shovel, ToolMaterial::Stone), 273);
    EXPECT_EQ(toolItemId(ToolType::Axe, ToolMaterial::Stone), 275);
    EXPECT_EQ(toolItemId(ToolType::Sword, ToolMaterial::Diamond), 276);
    EXPECT_EQ(toolItemId(ToolType::Shovel, ToolMaterial::Diamond), 277);
    EXPECT_EQ(toolItemId(ToolType::Axe, ToolMaterial::Diamond), 279);
    EXPECT_EQ(toolItemId(ToolType::Sword, ToolMaterial::Gold), 283);
    EXPECT_EQ(toolItemId(ToolType::Shovel, ToolMaterial::Gold), 284);
    EXPECT_EQ(toolItemId(ToolType::Axe, ToolMaterial::Gold), 286);
    EXPECT_EQ(toolItemId(ToolType::Hoe, ToolMaterial::Wood), 290);
    EXPECT_EQ(toolItemId(ToolType::Hoe, ToolMaterial::Stone), 291);
    EXPECT_EQ(toolItemId(ToolType::Hoe, ToolMaterial::Iron), 292);
    EXPECT_EQ(toolItemId(ToolType::Hoe, ToolMaterial::Diamond), 293);
    EXPECT_EQ(toolItemId(ToolType::Hoe, ToolMaterial::Gold), 294);
}

TEST(ToolItems, NoneTypeHasNoItem) {
    EXPECT_EQ(toolItemId(ToolType::None, ToolMaterial::Diamond), kNoItem);
}

TEST(ToolItems, RoundTripAllTwentyFiveTools) {
    std::set<ItemId> seen;
    for (ToolType type : kAllTypes) {
        for (ToolMaterial mat : kAllMaterials) {
            const ItemId id = toolItemId(type, mat);
            ASSERT_NE(id, kNoItem);
            EXPECT_TRUE(seen.insert(id).second) << "duplicate id " << id;
            const auto def = findTool(id);
            ASSERT_TRUE(def.has_value()) << id;
            EXPECT_EQ(def->type, type);
            EXPECT_EQ(def->material, mat);
            EXPECT_EQ(def->id, id);
            EXPECT_EQ(def->maxDamage, materialStats(mat).maxUses);
            EXPECT_TRUE(isTool(id));
        }
    }
    EXPECT_EQ(seen.size(), 25u);
}

TEST(ToolItems, BetaPickaxeIdsAreAliases) {
    // Vanilla numeric ids are the canonical ones; the legacy 102 still resolves.
    const auto wood = findTool(270);
    ASSERT_TRUE(wood.has_value());
    EXPECT_EQ(wood->type, ToolType::Pickaxe);
    EXPECT_EQ(wood->material, ToolMaterial::Wood);
    EXPECT_EQ(wood->id, 270) << "the Beta number is the canonical id";
    EXPECT_EQ(findTool(102)->id, 270) << "the legacy id folds onto it";
    const std::array<std::pair<ItemId, ToolMaterial>, 3> others{
        {{274, ToolMaterial::Stone}, {257, ToolMaterial::Iron}, {278, ToolMaterial::Diamond}}};
    for (const auto& [vanilla, mat] : others) {
        const auto def = findTool(vanilla);
        ASSERT_TRUE(def.has_value()) << vanilla;
        EXPECT_EQ(def->type, ToolType::Pickaxe);
        EXPECT_EQ(def->material, mat);
        EXPECT_EQ(def->id, toolItemId(ToolType::Pickaxe, mat));
    }
}

TEST(ToolItems, NonToolsAreRejected) {
    EXPECT_FALSE(findTool(kNoItem).has_value());
    EXPECT_FALSE(findTool(1).has_value());   // Stone block
    EXPECT_FALSE(findTool(100).has_value()); // Stick
    EXPECT_FALSE(findTool(101).has_value()); // Coal
    EXPECT_FALSE(findTool(106).has_value());
    EXPECT_FALSE(findTool(255).has_value());
    EXPECT_FALSE(findTool(259).has_value()); // Flint and steel
    EXPECT_FALSE(findTool(280).has_value()); // Stick (vanilla)
    EXPECT_FALSE(findTool(295).has_value());
    EXPECT_FALSE(findTool(0xFFFF).has_value());
    EXPECT_FALSE(isTool(1));
}

TEST(ToolItems, FromBlockIdBridgesLegacyInventory) {
    using betacraft::voxel::BlockId;
    EXPECT_EQ(toItemId(BlockId::DiamondPickaxe), 105);
    EXPECT_TRUE(isTool(toItemId(BlockId::WoodPickaxe)));
    EXPECT_FALSE(isTool(toItemId(BlockId::Stick)));
}
