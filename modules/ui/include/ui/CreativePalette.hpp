#pragma once

#include "gameplay/Armor.hpp"
#include "gameplay/ItemIds.hpp"
#include "voxel/Block.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>

namespace betacraft::ui {

// The creative palette: every block and item a player can hold, handed out for free in
// the Creative inventory screen, each in exactly one palette category (GLOSSARY.md).
// Entries are gameplay::ItemId in the canonical Beta numbering: pickaxes, stick and coal
// by their Beta item numbers (never the legacy BlockIds 100..105), doors, bed and redstone
// by their items, not their blocks. Technical states (piston head, lit furnace, still
// liquids, unlit redstone torch, crops, farmland) are left out.

// In category order; the first one is where the palette opens. Grows only at the end.
enum class PaletteCategory : uint8_t { Building, Decoration, Nature, Redstone, Tools, Combat, Food, Materials };

inline constexpr std::array<PaletteCategory, 8> kPaletteCategories{
    PaletteCategory::Building, PaletteCategory::Decoration, PaletteCategory::Nature, PaletteCategory::Redstone,
    PaletteCategory::Tools,    PaletteCategory::Combat,     PaletteCategory::Food,   PaletteCategory::Materials};

// The palette grid: 9 columns x 4 visible rows.
inline constexpr size_t kPaletteColumns = 9;
inline constexpr size_t kPaletteVisibleRows = 4;
inline constexpr size_t kPaletteVisibleSlots = kPaletteColumns * kPaletteVisibleRows;

namespace detail {

using gameplay::ItemId;
namespace items = gameplay::items;
[[nodiscard]] constexpr ItemId asItem(voxel::BlockId id) noexcept { return gameplay::toItemId(id); }
using voxel::BlockId;

inline constexpr ItemId kBuilding[] = {
    asItem(BlockId::Stone),     asItem(BlockId::Cobblestone), asItem(BlockId::MossyCobble), asItem(BlockId::OakPlanks),
    asItem(BlockId::Sandstone), asItem(BlockId::Bricks),      asItem(BlockId::Glass),       asItem(BlockId::CobbleSlab),
    asItem(BlockId::WoodStairs), asItem(BlockId::CobbleStairs), asItem(BlockId::Glowstone), asItem(BlockId::Wool),
};

inline constexpr ItemId kDecoration[] = {
    asItem(BlockId::Torch),     asItem(BlockId::Ladder),        asItem(BlockId::Fence),   asItem(BlockId::Bookshelf),
    asItem(BlockId::Chest),     asItem(BlockId::CraftingTable), asItem(BlockId::Furnace), items::Bed,
    asItem(BlockId::MobSpawner), items::Cake,
};

inline constexpr ItemId kNature[] = {
    asItem(BlockId::Dirt),      asItem(BlockId::Grass),     asItem(BlockId::SnowGrass),   asItem(BlockId::Sand),
    asItem(BlockId::Gravel),    asItem(BlockId::Clay),      asItem(BlockId::SnowBlock),   asItem(BlockId::Ice),
    asItem(BlockId::OakLog),    asItem(BlockId::BirchLog),  asItem(BlockId::PineLog),     asItem(BlockId::Leaves),
    asItem(BlockId::Sapling),   asItem(BlockId::CoalOre),   asItem(BlockId::IronOre),     asItem(BlockId::GoldOre),
    asItem(BlockId::RedstoneOre), asItem(BlockId::LapisOre), asItem(BlockId::DiamondOre), asItem(BlockId::Obsidian),
    asItem(BlockId::Bedrock),   asItem(BlockId::Cactus),    asItem(BlockId::SugarCane),   asItem(BlockId::Dandelion),
    asItem(BlockId::Rose),      asItem(BlockId::TallGrass), asItem(BlockId::DeadBush),
};

inline constexpr ItemId kRedstone[] = {
    items::Redstone,         asItem(BlockId::RedstoneTorchOn),   asItem(BlockId::Lever),
    asItem(BlockId::StoneButton), asItem(BlockId::StonePressurePlate), asItem(BlockId::WoodPressurePlate),
    asItem(BlockId::Piston),      asItem(BlockId::TNT),               items::WoodenDoor,
    items::IronDoor,
};

// Shovel, pickaxe, axe, hoe of each material, wood to diamond.
inline constexpr ItemId kTools[] = {
    items::WoodShovel,    items::WoodPickaxe,    items::WoodAxe,    items::WoodHoe,
    items::StoneShovel,   items::StonePickaxe,   items::StoneAxe,   items::StoneHoe,
    items::IronShovel,    items::IronPickaxe,    items::IronAxe,    items::IronHoe,
    items::GoldShovel,    items::GoldPickaxe,    items::GoldAxe,    items::GoldHoe,
    items::DiamondShovel, items::DiamondPickaxe, items::DiamondAxe, items::DiamondHoe,
    items::FlintAndSteel, items::Bucket,         items::WaterBucket, items::LavaBucket,
    items::MilkBucket,
};

// Swords, bow and arrows, then the 20 armour pieces (298..317, by material), throwables.
inline constexpr ItemId kCombatWeapons[] = {
    items::WoodSword, items::StoneSword, items::IronSword, items::GoldSword, items::DiamondSword,
    items::Bow,       items::Arrow,
};
inline constexpr size_t kArmorPieceCount = gameplay::kLastArmorItem - gameplay::kFirstArmorItem + 1;
inline constexpr ItemId kCombatThrowables[] = {items::Snowball, items::Egg};
inline constexpr auto kCombat = [] {
    std::array<ItemId, std::size(kCombatWeapons) + kArmorPieceCount + std::size(kCombatThrowables)> a{};
    auto out = std::copy(std::begin(kCombatWeapons), std::end(kCombatWeapons), a.begin());
    for (ItemId id = gameplay::kFirstArmorItem; id <= gameplay::kLastArmorItem; ++id) *out++ = id;
    std::copy(std::begin(kCombatThrowables), std::end(kCombatThrowables), out);
    return a;
}();

inline constexpr ItemId kFood[] = {
    items::Apple,          items::GoldenApple,  items::Bread, items::RawPorkchop,
    items::CookedPorkchop, items::MushroomStew, items::Cookie,
};

inline constexpr ItemId kMaterials[] = {
    items::Coal,   items::IronIngot,     items::GoldIngot, items::Diamond, items::Stick,     items::Bowl,
    items::String, items::Feather,       items::Gunpowder, items::Flint,   items::Leather,   items::Seeds,
    items::Wheat,  items::Paper,         items::Book,      items::GlowstoneDust, items::Bone, items::Sugar,
};

// What each palette category shows: its title, the item drawn on its tab, its entries.
struct PaletteCategoryInfo {
    std::string_view name;
    ItemId icon{gameplay::kNoItem};
    std::span<const ItemId> entries;
};

// Indexed by PaletteCategory, in category order.
inline constexpr std::array<PaletteCategoryInfo, kPaletteCategories.size()> kPaletteCategoryTable{{
    {"BUILDING", asItem(BlockId::Bricks), kBuilding},
    {"DECORATION", asItem(BlockId::Dandelion), kDecoration},
    {"NATURE", asItem(BlockId::Rose), kNature},
    {"REDSTONE", items::Redstone, kRedstone},
    {"TOOLS", items::IronPickaxe, kTools},
    {"COMBAT", items::IronSword, kCombat},
    {"FOOD", items::Bread, kFood},
    {"MATERIALS", items::IronIngot, kMaterials},
}};

static_assert([] {
    for (size_t i = 0; i < kPaletteCategories.size(); ++i) {
        if (static_cast<size_t>(kPaletteCategories[i]) != i) return false;
    }
    return true;
}(), "kPaletteCategories lists PaletteCategory in enum order, so it indexes kPaletteCategoryTable");

[[nodiscard]] constexpr const PaletteCategoryInfo* categoryInfo(PaletteCategory category) noexcept {
    const auto index = static_cast<size_t>(category);
    return index < kPaletteCategoryTable.size() ? &kPaletteCategoryTable[index] : nullptr;
}

} // namespace detail

// The entries of `category`, in display order.
[[nodiscard]] constexpr std::span<const gameplay::ItemId> paletteEntries(PaletteCategory category) noexcept {
    const auto* info = detail::categoryInfo(category);
    return info ? info->entries : std::span<const gameplay::ItemId>{};
}

namespace detail {
inline constexpr size_t kPaletteEntryCount = [] {
    size_t n = 0;
    for (const PaletteCategory c : kPaletteCategories) n += paletteEntries(c).size();
    return n;
}();
inline constexpr auto kAllPaletteEntries = [] {
    std::array<ItemId, kPaletteEntryCount> all{};
    size_t n = 0;
    for (const PaletteCategory c : kPaletteCategories) {
        for (const ItemId id : paletteEntries(c)) all[n++] = id;
    }
    return all;
}();
} // namespace detail

// Every entry of the whole palette: each category's entries, in category order.
[[nodiscard]] constexpr std::span<const gameplay::ItemId> allPaletteEntries() noexcept {
    return detail::kAllPaletteEntries;
}

// The palette title while `category` is shown ("BUILDING", "NATURE"...).
[[nodiscard]] constexpr std::string_view paletteCategoryName(PaletteCategory category) noexcept {
    const auto* info = detail::categoryInfo(category);
    return info ? info->name : std::string_view{};
}

// The representative item drawn on the category's tab.
[[nodiscard]] constexpr gameplay::ItemId paletteCategoryIcon(PaletteCategory category) noexcept {
    const auto* info = detail::categoryInfo(category);
    return info ? info->icon : gameplay::kNoItem;
}

// The category that offers `id`; nullopt when the palette does not.
[[nodiscard]] constexpr std::optional<PaletteCategory> paletteCategoryOf(gameplay::ItemId id) noexcept {
    for (const PaletteCategory c : kPaletteCategories) {
        const auto entries = paletteEntries(c);
        if (id != gameplay::kNoItem && std::find(entries.begin(), entries.end(), id) != entries.end()) {
            return c;
        }
    }
    return std::nullopt;
}

// Where the scrollbar thumb sits, as fractions of the track (0 = top, 1 = bottom).
struct PaletteScrollbarThumb {
    float top{0.0f};
    float height{1.0f};
};

// What the palette grid shows: the active category and how many rows it is scrolled down.
// A plain value with no GL; the game keeps one per open world (never saved). The catalogue
// is injectable so the scrolling rules can be tested on categories larger than today's.
class PaletteNavigation {
public:
    using Catalogue = std::span<const gameplay::ItemId> (*)(PaletteCategory) noexcept;

    constexpr PaletteNavigation() noexcept = default;
    constexpr explicit PaletteNavigation(Catalogue catalogue) noexcept : m_catalogue(catalogue) {}

    [[nodiscard]] constexpr PaletteCategory category() const noexcept { return m_category; }
    [[nodiscard]] constexpr size_t scrollRow() const noexcept { return m_scrollRow; }

    // Shows `category` from its first row.
    constexpr void selectCategory(PaletteCategory category) noexcept {
        m_category = category;
        m_scrollRow = 0;
    }

    // Mouse wheel, one row per step: positive (wheel up) towards the first row, negative
    // towards the last. Clamped to [0, rows - 4], so the grid never shows an empty page.
    constexpr void scroll(int32_t wheelY) noexcept {
        const auto row = static_cast<int64_t>(m_scrollRow) - wheelY;
        m_scrollRow = static_cast<size_t>(std::clamp<int64_t>(row, 0, static_cast<int64_t>(maxScrollRow())));
    }

    // Rows of the active category, the last one possibly partly filled.
    [[nodiscard]] constexpr size_t rowCount() const noexcept {
        return (entries().size() + kPaletteColumns - 1) / kPaletteColumns;
    }
    [[nodiscard]] constexpr size_t maxScrollRow() const noexcept {
        return rowCount() > kPaletteVisibleRows ? rowCount() - kPaletteVisibleRows : 0;
    }

    // The item in visible grid slot `slot` (row-major, 0..35); kNoItem for an empty slot.
    [[nodiscard]] constexpr gameplay::ItemId visibleItem(size_t slot) const noexcept {
        if (slot >= kPaletteVisibleSlots) {
            return gameplay::kNoItem;
        }
        const size_t index = m_scrollRow * kPaletteColumns + slot;
        const auto list = entries();
        return index < list.size() ? list[index] : gameplay::kNoItem;
    }

    // Only a category with more than 36 entries scrolls, and only then is the bar drawn.
    [[nodiscard]] constexpr bool scrollbarVisible() const noexcept {
        return entries().size() > kPaletteVisibleSlots;
    }
    [[nodiscard]] constexpr PaletteScrollbarThumb scrollbarThumb() const noexcept {
        const size_t rows = rowCount();
        if (rows <= kPaletteVisibleRows) {
            return {};
        }
        const auto r = static_cast<float>(rows);
        return {static_cast<float>(m_scrollRow) / r, static_cast<float>(kPaletteVisibleRows) / r};
    }

    [[nodiscard]] constexpr bool operator==(const PaletteNavigation&) const noexcept = default;

private:
    [[nodiscard]] constexpr std::span<const gameplay::ItemId> entries() const noexcept {
        return m_catalogue(m_category);
    }

    Catalogue m_catalogue{&paletteEntries};
    PaletteCategory m_category{PaletteCategory::Building};
    size_t m_scrollRow{0};
};

// The item in visible palette slot `index` when the palette shows Building, unscrolled
// (kNoItem for an empty slot or past the grid).
[[nodiscard]] constexpr gameplay::ItemId paletteItem(size_t index) noexcept {
    return PaletteNavigation{}.visibleItem(index);
}

} // namespace betacraft::ui
