// The shared item id scheme (gameplay/ItemIds.hpp) that crafting re-exports.
#include "gameplay/ItemIds.hpp"
#include "gameplay/ToolDefinitions.hpp"

#include "crafting/BetaRecipes.hpp"
#include "crafting/SmeltingRecipes.hpp"
#include "game/InventoryCrafting.hpp"
#include "game/PlayerPersistence.hpp"
#include "game/StarterKit.hpp"
#include "gameplay/Harvesting.hpp"
#include "storage/WorldStorage.hpp"
#include "ui/CreativePalette.hpp"

#include <gtest/gtest.h>

#include <string_view>

using namespace betacraft::gameplay;
using betacraft::voxel::BlockId;

// Everything is constexpr so crafting recipes and static tables can use it.
static_assert(toolItemId(ToolKind::Sword, ToolMaterial::Iron) == items::IronSword);
static_assert(toolInfo(items::beta::DiamondPickaxe)->material == ToolMaterial::Diamond);
static_assert(maxStackSize(items::LavaBucket) == 1);
static_assert(canonicalize(items::beta::Coal) == items::Coal);

TEST(SharedItemIds, ToolKindAndToolTypeLineUp) {
    EXPECT_EQ(toToolType(ToolKind::Pickaxe), ToolType::Pickaxe);
    EXPECT_EQ(toToolType(ToolKind::Axe), ToolType::Axe);
    EXPECT_EQ(toToolType(ToolKind::Shovel), ToolType::Shovel);
    EXPECT_EQ(toToolType(ToolKind::Sword), ToolType::Sword);
    EXPECT_EQ(toToolType(ToolKind::Hoe), ToolType::Hoe);
}

TEST(SharedItemIds, ToolTableRoundTripsThroughToolInfo) {
    for (int k = 0; k < 5; ++k) {
        for (int m = 0; m < 5; ++m) {
            const auto kind = static_cast<ToolKind>(k);
            const auto material = static_cast<ToolMaterial>(m);
            const ItemId id = toolItemId(kind, material);
            const auto info = toolInfo(id);
            ASSERT_TRUE(info.has_value()) << id;
            EXPECT_EQ(info->kind, kind);
            EXPECT_EQ(info->material, material);
        }
    }
    EXPECT_EQ(toolItemId(static_cast<ToolKind>(9), ToolMaterial::Wood), kNoItem);
    EXPECT_FALSE(toolInfo(kNoItem).has_value());
    EXPECT_FALSE(toolInfo(items::Stick).has_value());
}

TEST(SharedItemIds, VanillaStickAndCoalAreNotToolsAndStackTo64) {
    EXPECT_FALSE(isTool(items::beta::Stick));
    EXPECT_FALSE(isTool(items::beta::Coal));
    EXPECT_EQ(maxStackSize(items::beta::Stick), 64);
    EXPECT_EQ(maxStackSize(items::beta::WoodPickaxe), 1);
    EXPECT_EQ(toVanillaId(items::Coal), items::beta::Coal);
}

TEST(SharedItemIds, FindToolReportsTheCanonicalIdForVanillaAliases) {
    const auto def = findTool(items::beta::StonePickaxe);
    ASSERT_TRUE(def.has_value());
    EXPECT_EQ(def->id, items::StonePickaxe);
    EXPECT_EQ(def->id, 274) << "the Beta number is canonical";
    EXPECT_EQ(findTool(toItemId(BlockId::StonePickaxe))->id, items::StonePickaxe) << "legacy id folds";
    EXPECT_EQ(def->maxDamage, materialStats(ToolMaterial::Stone).maxUses);
    EXPECT_EQ(def->name, "Stone Pickaxe");
}

TEST(SharedItemIds, BucketsAndThrowablesFollowBetaStackLimits) {
    EXPECT_EQ(maxStackSize(items::Bucket), 1);
    EXPECT_EQ(maxStackSize(items::WaterBucket), 1);
    EXPECT_EQ(maxStackSize(items::LavaBucket), 1);
    EXPECT_EQ(maxStackSize(items::Snowball), 16);
    EXPECT_EQ(maxStackSize(items::Cookie), 8);
    EXPECT_EQ(maxStackSize(items::GlowstoneDust), 64);
    EXPECT_EQ(maxStackSize(kNoItem), 0);
}

// --- Beta item numbers are canonical; legacy ids 100..105 are only read on load ---

// Nothing the game produces carries a legacy id: recipes, smelting, block drops, the
// Creative kit and palette. voxel::BlockId gains no item past the six legacy ones.
TEST(SharedItemIds, NoNewLegacyIds) {
    betacraft::voxel::BlockRegistry::initialize();
    static_assert(static_cast<int>(BlockId::Count) == 106, "no new item in voxel::BlockId");
    for (int b = 100; b < static_cast<int>(BlockId::Count); ++b) {
        EXPECT_TRUE(isLegacyItemId(static_cast<ItemId>(b))) << b;
        EXPECT_FALSE(isLegacyItemId(canonicalize(static_cast<ItemId>(b)))) << b;
        EXPECT_GE(canonicalize(static_cast<ItemId>(b)), 256) << b;
    }

    const betacraft::game::InventoryCrafting screen;
    const betacraft::crafting::RecipeRegistry legacyBook = betacraft::crafting::makeBetaRecipeBook();
    for (const auto* book : {&screen.recipes(), &legacyBook}) {
        for (size_t i = 0; i < book->size(); ++i) {
            EXPECT_FALSE(isLegacyItemId(book->result(i).id)) << book->label(i);
        }
    }
    for (ItemId id = 1; id < 2300; ++id) {
        if (const auto out = betacraft::crafting::SmeltingBook::beta().result(id)) {
            EXPECT_FALSE(isLegacyItemId(out->id)) << id;
        }
    }
    const ItemStack held[] = {makeStack(items::DiamondPickaxe, 1), makeStack(items::DiamondShovel, 1),
                              makeStack(items::DiamondAxe, 1), ItemStack{}};
    for (int b = 1; b < static_cast<int>(BlockId::Count); ++b) {
        for (const ItemStack& tool : held) {
            for (uint64_t seed = 0; seed < 64; ++seed) {
                ItemStack h = tool;
                const BlockBreakOutcome r = breakBlockWith(static_cast<BlockId>(b), h, seed * 0x9E3779B97F4A7C15ULL);
                if (r.drop) {
                    EXPECT_FALSE(isLegacyItemId(r.drop->id)) << b;
                }
                if (r.bonusDrop) {
                    EXPECT_FALSE(isLegacyItemId(r.bonusDrop->id)) << b;
                }
            }
        }
    }
    betacraft::physics::Player player;
    for (size_t i = 0; i < betacraft::physics::Player::TOTAL_INVENTORY_SIZE; ++i) player.setInventorySlot(i, {});
    betacraft::game::applyStarterKit(player, betacraft::core::GameMode::Creative);
    for (size_t i = 0; i < betacraft::physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
        EXPECT_FALSE(isLegacyItemId(player.getInventorySlot(i).id)) << i;
    }
    EXPECT_EQ(player.getInventorySlot(0).id, items::StonePickaxe);
    // The palette is no longer one fixed 36-entry grid but every entry of every palette
    // category (issue #64), so the whole catalogue is checked, not the visible slots.
    for (const ItemId id : betacraft::ui::allPaletteEntries()) {
        EXPECT_FALSE(isLegacyItemId(id)) << id;
    }
    for (size_t i = 0; i < betacraft::ui::kPaletteVisibleSlots; ++i) {
        EXPECT_FALSE(isLegacyItemId(betacraft::ui::paletteItem(i))) << i;
    }
}

// A level.dat written before the Beta item numbers (items_scheme 1, frozen below) loads its legacy ids as
// Beta numbers; the next save writes items_scheme 2 ids only.
TEST(SharedItemIds, LoadFoldsLegacyIds) {
    betacraft::voxel::BlockRegistry::initialize();
    constexpr std::string_view kSchemeOneLevel =
        "BETACRAFT_LEVEL_V2\n"
        "items_scheme 1\n"
        "seed 173173173\n"
        "mode survival\n"
        "player.health 20\n"
        "inv 0 103 1 12\n"   // stone pickaxe, worn
        "inv 1 100 32 0\n"   // sticks
        "inv 2 101 7 0\n"    // coal
        "inv 3 102 1 0\n"    // wooden pickaxe
        "inv 4 104 1 3\n"    // iron pickaxe
        "inv 5 105 1 0\n"    // diamond pickaxe
        "inv 9 17 32 0\n"    // oak logs (a block: unchanged)
        "inv 10 268 1 40\n"; // wooden sword (already Beta)
    betacraft::storage::WorldLevelData level{};
    ASSERT_EQ(betacraft::storage::decodeLevelData(kSchemeOneLevel, level), betacraft::storage::LevelLoadStatus::Ok);
    EXPECT_EQ(level.itemsScheme, betacraft::storage::kLegacyItemsScheme);
    ASSERT_TRUE(level.player.has_value());

    betacraft::physics::Player player;
    betacraft::game::restorePlayer(player, *level.player, level.itemsScheme);
    using Slot = betacraft::physics::InventorySlot;
    EXPECT_EQ(player.getInventorySlot(0), (Slot{items::StonePickaxe, 1, 12}));
    EXPECT_EQ(player.getInventorySlot(1), (Slot{items::Stick, 32}));
    EXPECT_EQ(player.getInventorySlot(2), (Slot{items::Coal, 7}));
    EXPECT_EQ(player.getInventorySlot(3), (Slot{items::WoodPickaxe, 1}));
    EXPECT_EQ(player.getInventorySlot(4), (Slot{items::IronPickaxe, 1, 3}));
    EXPECT_EQ(player.getInventorySlot(5), (Slot{items::DiamondPickaxe, 1}));
    EXPECT_EQ(player.getInventorySlot(9), (Slot{toItemId(BlockId::OakLog), 32}));
    EXPECT_EQ(player.getInventorySlot(10), (Slot{items::WoodSword, 1, 40}));

    // Saved again: Beta numbers only, under items_scheme 2.
    betacraft::storage::WorldLevelData resaved{};
    resaved.itemsScheme = betacraft::storage::kCurrentItemsScheme;
    resaved.player = betacraft::game::capturePlayer(player);
    for (const betacraft::storage::SavedStack& s : resaved.player->inventory) {
        EXPECT_FALSE(isLegacyItemId(s.id)) << s.id;
    }
    const std::string text = betacraft::storage::encodeLevelData(resaved);
    EXPECT_NE(text.find("items_scheme 2\n"), std::string::npos);
    EXPECT_NE(text.find("inv 1 280 32 0\n"), std::string::npos);
}
