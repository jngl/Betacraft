// P2.6 — Item id scheme shared with the tools/combat module (P2.5).
#include "crafting/ItemIds.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>

#include <set>

using namespace betacraft::crafting;
using betacraft::voxel::BlockId;

namespace {
constexpr std::array<ToolKind, 5> kKinds{ToolKind::Pickaxe, ToolKind::Axe, ToolKind::Shovel,
                                         ToolKind::Sword, ToolKind::Hoe};
constexpr std::array<ToolMaterial, 5> kMaterials{ToolMaterial::Wood, ToolMaterial::Stone,
                                                 ToolMaterial::Iron, ToolMaterial::Gold,
                                                 ToolMaterial::Diamond};
} // namespace

TEST(ItemIds, BlockIdsMapOneToOne) {
    EXPECT_EQ(toItemId(BlockId::Air), kAir);
    EXPECT_EQ(toItemId(BlockId::OakPlanks), 5);
    EXPECT_EQ(toItemId(BlockId::Stick), 100);
    EXPECT_TRUE(isBlockRange(toItemId(BlockId::Furnace)));
    EXPECT_FALSE(isBlockRange(items::IronIngot));
}

TEST(ItemIds, ToBlockIdOnlyForBlockRange) {
    ASSERT_TRUE(toBlockId(toItemId(BlockId::Torch)).has_value());
    EXPECT_EQ(*toBlockId(toItemId(BlockId::Torch)), BlockId::Torch);
    EXPECT_FALSE(toBlockId(items::WoodAxe).has_value());
    EXPECT_FALSE(toBlockId(items::IronIngot).has_value());
}

// S2.10: the six items that had codebase ids (BlockId 100..105) use their Beta numbers;
// the old ids only fold onto them.
TEST(ItemIds, LegacyItemsKeepTheirCodebaseIds) {
    EXPECT_EQ(items::Stick, 280);
    EXPECT_EQ(items::Coal, 263);
    EXPECT_EQ(items::WoodPickaxe, 270);
    EXPECT_EQ(items::StonePickaxe, 274);
    EXPECT_EQ(items::IronPickaxe, 257);
    EXPECT_EQ(items::DiamondPickaxe, 278);
    EXPECT_EQ(canonicalize(toItemId(BlockId::Stick)), items::Stick);
    EXPECT_EQ(canonicalize(toItemId(BlockId::CoalItem)), items::Coal);
    EXPECT_EQ(canonicalize(toItemId(BlockId::WoodPickaxe)), items::WoodPickaxe);
    EXPECT_EQ(canonicalize(toItemId(BlockId::StonePickaxe)), items::StonePickaxe);
    EXPECT_EQ(canonicalize(toItemId(BlockId::IronPickaxe)), items::IronPickaxe);
    EXPECT_EQ(canonicalize(toItemId(BlockId::DiamondPickaxe)), items::DiamondPickaxe);
}

TEST(ItemIds, NewItemsUseVanillaBetaNumbers) {
    EXPECT_EQ(items::IronShovel, 256);
    EXPECT_EQ(items::IronAxe, 258);
    EXPECT_EQ(items::Diamond, 264);
    EXPECT_EQ(items::IronIngot, 265);
    EXPECT_EQ(items::GoldIngot, 266);
    EXPECT_EQ(items::WoodSword, 268);
    EXPECT_EQ(items::GoldPickaxe, 285);
    EXPECT_EQ(items::DiamondHoe, 293);
    EXPECT_EQ(items::Bucket, 325);
    EXPECT_EQ(items::MilkBucket, 335);
    EXPECT_EQ(items::Cake, 354);
    EXPECT_EQ(items::Chest, 54);
}

TEST(ItemIds, CanonicalizeFoldsVanillaAliasesOntoLegacyIds) {
    EXPECT_EQ(canonicalize(items::beta::Stick), items::Stick);
    EXPECT_EQ(canonicalize(items::beta::Coal), items::Coal);
    EXPECT_EQ(canonicalize(items::beta::WoodPickaxe), items::WoodPickaxe);
    EXPECT_EQ(canonicalize(items::beta::StonePickaxe), items::StonePickaxe);
    EXPECT_EQ(canonicalize(items::beta::IronPickaxe), items::IronPickaxe);
    EXPECT_EQ(canonicalize(items::beta::DiamondPickaxe), items::DiamondPickaxe);
    EXPECT_EQ(canonicalize(items::IronAxe), items::IronAxe);
    EXPECT_EQ(canonicalize(toItemId(BlockId::Cobblestone)), toItemId(BlockId::Cobblestone));
}

TEST(ItemIds, ToVanillaIdIsInverseOfCanonicalizeForAliases) {
    EXPECT_EQ(toVanillaId(items::Stick), items::beta::Stick);
    EXPECT_EQ(toVanillaId(items::DiamondPickaxe), items::beta::DiamondPickaxe);
    EXPECT_EQ(toVanillaId(items::IronAxe), items::IronAxe);
    for (ItemId id = 0; id < 512; ++id) {
        EXPECT_EQ(canonicalize(toVanillaId(canonicalize(id))), canonicalize(id)) << id;
    }
}

TEST(ItemIds, ToolMappingCoversAllKindsAndMaterials) {
    std::set<ItemId> seen;
    for (const ToolKind k : kKinds) {
        for (const ToolMaterial m : kMaterials) {
            const ItemId id = toolItemId(k, m);
            EXPECT_NE(id, kAir);
            EXPECT_TRUE(seen.insert(id).second) << "duplicate tool id " << id;
            const auto info = toolInfo(id);
            ASSERT_TRUE(info.has_value()) << id;
            EXPECT_EQ(info->kind, k);
            EXPECT_EQ(info->material, m);
        }
    }
    EXPECT_EQ(seen.size(), 25u);
}

TEST(ItemIds, ToolMappingUsesLegacyPickaxes) {
    EXPECT_EQ(toolItemId(ToolKind::Pickaxe, ToolMaterial::Wood), items::WoodPickaxe);
    EXPECT_EQ(toolItemId(ToolKind::Pickaxe, ToolMaterial::Diamond), items::DiamondPickaxe);
    EXPECT_EQ(toolItemId(ToolKind::Pickaxe, ToolMaterial::Gold), items::GoldPickaxe);
    EXPECT_EQ(toolItemId(ToolKind::Axe, ToolMaterial::Stone), items::StoneAxe);
}

TEST(ItemIds, ToolInfoAcceptsVanillaAliasAndRejectsNonTools) {
    const auto info = toolInfo(items::beta::WoodPickaxe);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->kind, ToolKind::Pickaxe);
    EXPECT_EQ(info->material, ToolMaterial::Wood);
    EXPECT_FALSE(toolInfo(items::Stick).has_value());
    EXPECT_FALSE(toolInfo(items::IronIngot).has_value());
    EXPECT_FALSE(toolInfo(kAir).has_value());
}

TEST(ItemIds, MaxDurabilityMatchesBetaValues) {
    EXPECT_EQ(maxDurability(items::WoodPickaxe), 59);
    EXPECT_EQ(maxDurability(items::StoneSword), 131);
    EXPECT_EQ(maxDurability(items::IronAxe), 250);
    EXPECT_EQ(maxDurability(items::GoldHoe), 32);
    EXPECT_EQ(maxDurability(items::DiamondShovel), 1561);
    EXPECT_EQ(maxDurability(items::Stick), 0);
    EXPECT_EQ(maxDurability(kAir), 0);
}

TEST(ItemIds, ToolIdsNeverCollideWithMaterialsOrBlocks) {
    const std::set<ItemId> materials{items::Stick, items::Coal, items::IronIngot, items::GoldIngot,
                                     items::Diamond, toItemId(BlockId::OakPlanks),
                                     toItemId(BlockId::Cobblestone)};
    for (const ToolKind k : kKinds) {
        for (const ToolMaterial m : kMaterials) {
            EXPECT_EQ(materials.count(toolItemId(k, m)), 0u);
        }
    }
}

TEST(ItemIds, MaxStackSizeIsOneForToolsAndBuckets) {
    EXPECT_EQ(maxStackSize(items::IronSword), 1);
    EXPECT_EQ(maxStackSize(items::WoodPickaxe), 1);
    EXPECT_EQ(maxStackSize(items::MilkBucket), 1);
    EXPECT_EQ(maxStackSize(items::Bucket), 1);
    EXPECT_EQ(maxStackSize(items::Cake), 1);
    EXPECT_EQ(maxStackSize(items::Stick), 64);
    EXPECT_EQ(maxStackSize(toItemId(BlockId::OakPlanks)), 64);
}

// toBlockId() only yields enumerators the codebase actually defines, so the
// result can index BlockRegistry / property tables of size BlockId::Count.
TEST(ItemIds, ToBlockIdRejectsUndefinedNumbersBelow256) {
    EXPECT_FALSE(toBlockId(19).has_value()); // gap in the BlockId enum
    EXPECT_FALSE(toBlockId(22).has_value());
    for (ItemId id = toItemId(BlockId::Count); id < 256; ++id) {
        EXPECT_FALSE(toBlockId(id).has_value()) << id;
    }
    // The legacy number still names its (item-only) BlockDef; the Beta stick has no BlockId.
    ASSERT_TRUE(toBlockId(items::legacy::Stick).has_value());
    EXPECT_EQ(*toBlockId(items::legacy::Stick), BlockId::Stick);
    EXPECT_FALSE(toBlockId(items::Stick).has_value());
}

TEST(ItemIds, ToBlockIdAgreesWithBlockRegistry) {
    betacraft::voxel::BlockRegistry::initialize();
    for (ItemId id = 1; id < 256; ++id) {
        const auto& def = betacraft::voxel::BlockRegistry::get(static_cast<BlockId>(id));
        const bool defined = def.name != "Air";
        EXPECT_EQ(toBlockId(id).has_value(), defined) << id << " " << def.name;
        EXPECT_EQ(isPlaceableBlock(id), defined && !def.isItemOnly) << id << " " << def.name;
    }
}

TEST(ItemIds, PlaceableBlocksExcludeItemsAirAndReservedIds) {
    EXPECT_TRUE(isPlaceableBlock(toItemId(BlockId::Torch)));
    EXPECT_TRUE(isPlaceableBlock(toItemId(BlockId::CraftingTable)));
    EXPECT_FALSE(isPlaceableBlock(kAir));
    EXPECT_FALSE(isPlaceableBlock(items::Stick));
    EXPECT_FALSE(isPlaceableBlock(items::Coal));
    EXPECT_FALSE(isPlaceableBlock(items::DiamondPickaxe));
    EXPECT_FALSE(isPlaceableBlock(items::IronIngot));
    EXPECT_FALSE(isPlaceableBlock(items::beta::Stick));
}

// S2.1: the chest and the burning furnace became real blocks with their Beta ids.
TEST(ItemIds, ChestAndLitFurnaceArePlaceable) {
    EXPECT_EQ(items::Chest, 54);
    EXPECT_EQ(toItemId(BlockId::LitFurnace), 62);
    ASSERT_TRUE(toBlockId(items::Chest).has_value());
    EXPECT_EQ(*toBlockId(items::Chest), BlockId::Chest);
    ASSERT_TRUE(toBlockId(62).has_value());
    EXPECT_EQ(*toBlockId(62), BlockId::LitFurnace);
    EXPECT_TRUE(isPlaceableBlock(items::Chest));
    EXPECT_TRUE(isPlaceableBlock(toItemId(BlockId::LitFurnace)));
}

TEST(ItemIds, LitFurnaceDropsFurnace) {
    betacraft::voxel::BlockRegistry::initialize();
    const auto& lit = betacraft::voxel::BlockRegistry::get(BlockId::LitFurnace);
    EXPECT_EQ(lit.dropId, BlockId::Furnace);
    EXPECT_EQ(lit.lightEmission, 13);
    EXPECT_EQ(betacraft::voxel::BlockRegistry::get(BlockId::Furnace).lightEmission, 0);
    EXPECT_EQ(betacraft::voxel::BlockRegistry::get(BlockId::Chest).dropId, BlockId::Chest);
}
