// The creative palette catalogue (every block and item a player can hold, sorted into the
// eight palette categories) and its GL-free navigation state.
#include "gameplay/Armor.hpp"
#include "gameplay/ItemIds.hpp"
#include "render/ItemAppearance.hpp"
#include "ui/CreativePalette.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <span>
#include <string>

using namespace betacraft;
using namespace betacraft::ui;
using gameplay::ItemId;
using gameplay::kNoItem;
using gameplay::toItemId;
using voxel::BlockId;
namespace items = gameplay::items;

namespace {

// How many palette categories hold `id` (0 when it is not in the palette).
size_t categoriesHolding(ItemId id) {
    size_t n = 0;
    for (const PaletteCategory c : kPaletteCategories) {
        const auto entries = paletteEntries(c);
        n += static_cast<size_t>(std::count(entries.begin(), entries.end(), id));
    }
    return n;
}

// Technical states and ids replaced by an item: never offered by the palette. Excluding
// a block is a deliberate decision, so the list is written out here.
const std::set<ItemId> kExcluded = {
    toItemId(BlockId::PistonHead),     toItemId(BlockId::LitFurnace),   toItemId(BlockId::Water),
    toItemId(BlockId::StillWater),     toItemId(BlockId::Lava),         toItemId(BlockId::StillLava),
    toItemId(BlockId::RedstoneTorchOff),
    toItemId(BlockId::RedstoneWire), // replaced by items::Redstone
    toItemId(BlockId::Crops),        // replaced by items::Seeds
    toItemId(BlockId::Farmland),
    toItemId(BlockId::WoodDoor),     // replaced by items::WoodenDoor
    toItemId(BlockId::IronDoor),     // replaced by items::IronDoor
    toItemId(BlockId::Bed),          // replaced by items::Bed
    // Legacy item-only ids 100..105: the palette lists their Beta item numbers instead.
    toItemId(BlockId::Stick), toItemId(BlockId::CoalItem), toItemId(BlockId::WoodPickaxe),
    toItemId(BlockId::StonePickaxe), toItemId(BlockId::IronPickaxe), toItemId(BlockId::DiamondPickaxe),
};

// Every real BlockId (it has a BlockDef) and every item the renderer knows by name,
// armour included. Air is not an id a player can hold.
std::set<ItemId> everyKnownId() {
    voxel::BlockRegistry::initialize();
    std::set<ItemId> ids;
    for (ItemId id = 1; id < 256; ++id) {
        if (voxel::BlockRegistry::get(static_cast<BlockId>(id)).name != "Air") ids.insert(id);
    }
    for (ItemId id = 256; id < 2300; ++id) {
        if (render::itemAppearance(id).name.rfind("Item #", 0) != 0) ids.insert(id);
    }
    return ids;
}

} // namespace

TEST(CreativePaletteCatalogue, CategoriesInTabOrderWithNamesAndIcons) {
    ASSERT_EQ(kPaletteCategories.size(), 8U);
    const std::pair<std::string_view, ItemId> expected[] = {
        {"BUILDING", toItemId(BlockId::Bricks)},  {"DECORATION", toItemId(BlockId::Dandelion)},
        {"NATURE", toItemId(BlockId::Rose)},      {"REDSTONE", items::Redstone},
        {"TOOLS", items::IronPickaxe},            {"COMBAT", items::IronSword},
        {"FOOD", items::Bread},                   {"MATERIALS", items::IronIngot},
    };
    for (size_t i = 0; i < kPaletteCategories.size(); ++i) {
        EXPECT_EQ(static_cast<size_t>(kPaletteCategories[i]), i);
        EXPECT_EQ(paletteCategoryName(kPaletteCategories[i]), expected[i].first) << i;
        EXPECT_EQ(paletteCategoryIcon(kPaletteCategories[i]), expected[i].second) << i;
    }
    EXPECT_EQ(kPaletteCategories.front(), PaletteCategory::Building);
    EXPECT_EQ(kPaletteCategories.back(), PaletteCategory::Materials);
}

// "Every block and item" stays true as content grows: a new id has to be sorted into a
// category or added to kExcluded above.
TEST(CreativePaletteCatalogue, EveryKnownIdIsInExactlyOneCategoryOrExcluded) {
    const std::set<ItemId> known = everyKnownId();
    ASSERT_TRUE(known.contains(toItemId(BlockId::Stone)));
    ASSERT_TRUE(known.contains(items::Cookie));
    ASSERT_TRUE(known.contains(gameplay::kFirstArmorItem));
    ASSERT_TRUE(known.contains(gameplay::kLastArmorItem));
    for (const ItemId id : known) {
        const std::string name = render::itemAppearance(id).name;
        if (kExcluded.contains(id)) {
            EXPECT_EQ(categoriesHolding(id), 0U) << id << " " << name << " is excluded";
        } else {
            EXPECT_EQ(categoriesHolding(id), 1U) << id << " " << name << " is not sorted into exactly one category";
        }
    }
    // Nothing unknown sneaks in either.
    for (const PaletteCategory c : kPaletteCategories) {
        for (const ItemId id : paletteEntries(c)) {
            EXPECT_TRUE(known.contains(id)) << id;
            EXPECT_FALSE(gameplay::isLegacyItemId(id)) << id;
        }
    }
}

TEST(CreativePaletteCatalogue, NoEntryAppearsTwice) {
    std::map<ItemId, int> seen;
    for (const PaletteCategory c : kPaletteCategories) {
        for (const ItemId id : paletteEntries(c)) ++seen[id];
    }
    for (const auto& [id, n] : seen) EXPECT_EQ(n, 1) << id;
    EXPECT_FALSE(seen.contains(kNoItem));
}

TEST(CreativePaletteCatalogue, SettledEdgeCases) {
    const auto in = [](ItemId id) { return paletteCategoryOf(id); };
    EXPECT_EQ(in(toItemId(BlockId::Stone)), PaletteCategory::Building);
    EXPECT_EQ(in(toItemId(BlockId::Obsidian)), PaletteCategory::Nature);
    EXPECT_EQ(in(toItemId(BlockId::Bedrock)), PaletteCategory::Nature);
    EXPECT_EQ(in(items::Cake), PaletteCategory::Decoration);
    EXPECT_EQ(in(toItemId(BlockId::MobSpawner)), PaletteCategory::Decoration);
    EXPECT_EQ(in(items::WoodenDoor), PaletteCategory::Redstone);
    EXPECT_EQ(in(items::IronDoor), PaletteCategory::Redstone);
    EXPECT_EQ(in(toItemId(BlockId::TNT)), PaletteCategory::Redstone);
    for (const ItemId bucket : {items::Bucket, items::WaterBucket, items::LavaBucket, items::MilkBucket}) {
        EXPECT_EQ(in(bucket), PaletteCategory::Tools) << bucket;
    }
    EXPECT_EQ(in(items::FlintAndSteel), PaletteCategory::Tools);
    EXPECT_EQ(in(items::Snowball), PaletteCategory::Combat);
    EXPECT_EQ(in(items::Egg), PaletteCategory::Combat);
    EXPECT_EQ(in(gameplay::armorItemId(gameplay::ArmorMaterial::Chain, gameplay::ArmorSlot::Boots)),
              PaletteCategory::Combat);
    for (const ItemId material : {items::Bowl, items::GlowstoneDust, items::Sugar, items::Bone}) {
        EXPECT_EQ(in(material), PaletteCategory::Materials) << material;
    }
    EXPECT_EQ(in(items::MushroomStew), PaletteCategory::Food);
    // Pickaxes, stick and coal through their Beta item numbers.
    EXPECT_EQ(in(items::DiamondPickaxe), PaletteCategory::Tools);
    EXPECT_EQ(in(items::Stick), PaletteCategory::Materials);
    EXPECT_EQ(in(toItemId(BlockId::DiamondPickaxe)), std::nullopt) << "legacy id 105";
    EXPECT_EQ(in(toItemId(BlockId::RedstoneWire)), std::nullopt);
    EXPECT_EQ(in(kNoItem), std::nullopt);
}

TEST(CreativePaletteCatalogue, RelatedEntriesSitTogether) {
    const auto building = paletteEntries(PaletteCategory::Building);
    ASSERT_GE(building.size(), 1U);
    EXPECT_EQ(building[0], toItemId(BlockId::Stone));

    const auto nature = paletteEntries(PaletteCategory::Nature);
    const auto pos = [&](ItemId id) { return std::find(nature.begin(), nature.end(), id) - nature.begin(); };
    EXPECT_EQ(pos(toItemId(BlockId::BirchLog)), pos(toItemId(BlockId::OakLog)) + 1);
    EXPECT_EQ(pos(toItemId(BlockId::PineLog)), pos(toItemId(BlockId::BirchLog)) + 1);
    EXPECT_EQ(pos(toItemId(BlockId::IronOre)), pos(toItemId(BlockId::CoalOre)) + 1);

    // Tools by material: the wooden shovel, pickaxe, axe and hoe, then the stone ones...
    const auto tools = paletteEntries(PaletteCategory::Tools);
    ASSERT_GE(tools.size(), 8U);
    const ItemId expected[] = {items::WoodShovel, items::WoodPickaxe, items::WoodAxe, items::WoodHoe,
                               items::StoneShovel, items::StonePickaxe, items::StoneAxe, items::StoneHoe};
    for (size_t i = 0; i < std::size(expected); ++i) EXPECT_EQ(tools[i], expected[i]) << i;
}

// --- Navigation state ---

TEST(PaletteNavigationTest, StartsOnBuildingUnscrolled) {
    const PaletteNavigation nav;
    EXPECT_EQ(nav.category(), PaletteCategory::Building);
    EXPECT_EQ(nav.scrollRow(), 0U);
    EXPECT_EQ(nav.visibleItem(0), toItemId(BlockId::Stone));
    // paletteItem(i) is slot i of that initial state.
    for (size_t i = 0; i < kPaletteVisibleSlots; ++i) EXPECT_EQ(paletteItem(i), nav.visibleItem(i)) << i;
}

TEST(PaletteNavigationTest, SlotPastTheContentIsEmpty) {
    PaletteNavigation nav;
    nav.selectCategory(PaletteCategory::Food);
    const size_t n = paletteEntries(PaletteCategory::Food).size();
    ASSERT_LT(n, kPaletteVisibleSlots);
    EXPECT_EQ(nav.visibleItem(0), items::Apple);
    EXPECT_NE(nav.visibleItem(n - 1), kNoItem);
    EXPECT_EQ(nav.visibleItem(n), kNoItem);
    EXPECT_EQ(nav.visibleItem(kPaletteVisibleSlots - 1), kNoItem);
    EXPECT_EQ(nav.visibleItem(kPaletteVisibleSlots), kNoItem) << "outside the 9x4 grid";
    EXPECT_EQ(paletteItem(1000), kNoItem);
}

TEST(PaletteNavigationTest, SmallCategoryDoesNotScrollOrShowAScrollbar) {
    PaletteNavigation nav;
    nav.selectCategory(PaletteCategory::Food);
    EXPECT_FALSE(nav.scrollbarVisible());
    nav.scroll(-3); // wheel down
    EXPECT_EQ(nav.scrollRow(), 0U);
    nav.scroll(2); // wheel up
    EXPECT_EQ(nav.scrollRow(), 0U);
}

// No category exceeds 36 entries yet, so the scrolling rules are pinned on a test catalogue:
// Building 50 entries (6 rows: offsets 0..2), Decoration exactly 36, Nature 37.
namespace {
template <size_t N>
constexpr std::array<ItemId, N> numbered() {
    std::array<ItemId, N> a{};
    for (size_t i = 0; i < N; ++i) a[i] = static_cast<ItemId>(256 + i);
    return a;
}
constexpr auto kFifty = numbered<50>();
constexpr auto kThirtySix = numbered<36>();
constexpr auto kThirtySeven = numbered<37>();

std::span<const ItemId> testCatalogue(PaletteCategory c) noexcept {
    switch (c) {
        case PaletteCategory::Building: return kFifty;
        case PaletteCategory::Decoration: return kThirtySix;
        case PaletteCategory::Nature: return kThirtySeven;
        default: return paletteEntries(c);
    }
}
} // namespace

TEST(PaletteNavigationTest, ScrollIsClampedToTheLastFullGrid) {
    PaletteNavigation nav(testCatalogue);
    EXPECT_TRUE(nav.scrollbarVisible());
    EXPECT_FLOAT_EQ(nav.scrollbarThumb().top, 0.0f);
    EXPECT_FLOAT_EQ(nav.scrollbarThumb().height, 4.0f / 6.0f);

    nav.scroll(1); // wheel up at the top
    EXPECT_EQ(nav.scrollRow(), 0U);
    nav.scroll(-1); // wheel down one row
    EXPECT_EQ(nav.scrollRow(), 1U);
    EXPECT_EQ(nav.visibleItem(0), kFifty[9]);
    nav.scroll(-10);
    EXPECT_EQ(nav.scrollRow(), 2U);
    EXPECT_EQ(nav.visibleItem(0), kFifty[18]);
    EXPECT_EQ(nav.visibleItem(31), kFifty[49]);
    EXPECT_EQ(nav.visibleItem(32), kNoItem) << "the last row is partly empty";
    EXPECT_FLOAT_EQ(nav.scrollbarThumb().top, 2.0f / 6.0f);
    nav.scroll(10);
    EXPECT_EQ(nav.scrollRow(), 0U);
}

TEST(PaletteNavigationTest, ScrollbarOnlyAbove36Entries) {
    PaletteNavigation nav(testCatalogue);
    nav.selectCategory(PaletteCategory::Decoration); // exactly 36
    EXPECT_FALSE(nav.scrollbarVisible());
    nav.scroll(-1);
    EXPECT_EQ(nav.scrollRow(), 0U);
    nav.selectCategory(PaletteCategory::Nature); // 37
    EXPECT_TRUE(nav.scrollbarVisible());
    nav.scroll(-1);
    EXPECT_EQ(nav.scrollRow(), 1U);
    EXPECT_EQ(nav.visibleItem(0), kThirtySeven[9]);
}

TEST(PaletteNavigationTest, SelectingACategoryResetsScroll) {
    PaletteNavigation nav(testCatalogue);
    nav.scroll(-2);
    ASSERT_EQ(nav.scrollRow(), 2U);
    nav.selectCategory(PaletteCategory::Tools);
    EXPECT_EQ(nav.category(), PaletteCategory::Tools);
    EXPECT_EQ(nav.scrollRow(), 0U);
    EXPECT_EQ(nav.visibleItem(0), items::WoodShovel);
    EXPECT_FALSE(nav.scrollbarVisible());
    nav.selectCategory(PaletteCategory::Building);
    EXPECT_EQ(nav.scrollRow(), 0U) << "back to the top, not where it was left";
}
