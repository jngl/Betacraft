#include "crafting/BetaRecipes.hpp"

#include "gameplay/Armor.hpp"

#include <array>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace betacraft::crafting {

namespace {

using voxel::BlockId;

constexpr ItemId kPlanks = toItemId(BlockId::OakPlanks);
constexpr ItemId kCobble = toItemId(BlockId::Cobblestone);

class BookBuilder {
public:
    explicit BookBuilder(RecipeRegistry& registry) noexcept : m_registry(registry) {}

    void shaped(std::string_view label, std::initializer_list<std::string_view> rows,
                std::initializer_list<PatternKey> keys, RecipeResult result) {
        record(m_registry.addShaped(rows, keys, result, true, label));
    }

    void shapeless(std::string_view label, std::initializer_list<Ingredient> ingredients, RecipeResult result) {
        record(m_registry.addShapeless(ingredients, result, label));
    }

    [[nodiscard]] BookStats stats() const noexcept { return m_stats; }

private:
    void record(RecipeError e) noexcept {
        if (e == RecipeError::None) {
            ++m_stats.added;
        } else {
            ++m_stats.failed;
        }
    }

    RecipeRegistry& m_registry;
    BookStats m_stats{};
};

[[nodiscard]] Ingredient vanillaMaterial(ToolMaterial m) noexcept {
    switch (m) {
        case ToolMaterial::Wood: return Ingredient::of(kPlanks);
        case ToolMaterial::Stone: return Ingredient::of(kCobble);
        case ToolMaterial::Iron: return Ingredient::of(items::IronIngot);
        case ToolMaterial::Gold: return Ingredient::of(items::GoldIngot);
        case ToolMaterial::Diamond: return Ingredient::of(items::Diamond);
    }
    return Ingredient{};
}

// The legacy CraftingSystem had no smelting outputs, so it used raw ores where
// Beta uses ingots/gems. Registered as a separate recipe (not an anyOf
// alternative) so that a single craft cannot mix ingots and ores.
[[nodiscard]] std::optional<Ingredient> legacyOreMaterial(ToolMaterial m) noexcept {
    switch (m) {
        case ToolMaterial::Iron: return Ingredient::of(toItemId(BlockId::IronOre));
        case ToolMaterial::Gold: return Ingredient::of(toItemId(BlockId::GoldOre));
        case ToolMaterial::Diamond: return Ingredient::of(toItemId(BlockId::DiamondOre));
        case ToolMaterial::Wood:
        case ToolMaterial::Stone: break;
    }
    return std::nullopt;
}

constexpr std::array<std::string_view, 5> kMaterialNames{"Wooden", "Stone", "Iron", "Gold", "Diamond"};
constexpr std::array<std::string_view, 5> kToolNames{"Pickaxe", "Axe", "Shovel", "Sword", "Hoe"};

void addTool(BookBuilder& b, std::string_view label, ToolKind kind, const Ingredient& x, RecipeResult result) {
    const Ingredient stick = Ingredient::of(items::Stick);
    const std::initializer_list<PatternKey> keys{{'X', x}, {'#', stick}};
    // Patterns from Beta 1.7.3 RecipesTools / RecipesWeapons. Axe and hoe are
    // asymmetric: their mirrored layout is accepted by the matcher.
    switch (kind) {
        case ToolKind::Pickaxe: b.shaped(label, {"XXX", " # ", " # "}, keys, result); break;
        case ToolKind::Axe: b.shaped(label, {"XX", "X#", " #"}, keys, result); break;
        case ToolKind::Shovel: b.shaped(label, {"X", "#", "#"}, keys, result); break;
        case ToolKind::Sword: b.shaped(label, {"X", "X", "#"}, keys, result); break;
        case ToolKind::Hoe: b.shaped(label, {"XX", " #", " #"}, keys, result); break;
    }
}

void addTools(BookBuilder& b, const BetaRecipeOptions& options) {
    for (size_t m = 0; m < kMaterialNames.size(); ++m) {
        const auto material = static_cast<ToolMaterial>(m);
        const auto ore = options.acceptLegacyOres ? legacyOreMaterial(material) : std::nullopt;
        for (size_t k = 0; k < kToolNames.size(); ++k) {
            const auto kind = static_cast<ToolKind>(k);
            const std::string label = std::string{kMaterialNames[m]} + " " + std::string{kToolNames[k]};
            const RecipeResult result{toolItemId(kind, material), 1};
            addTool(b, label, kind, vanillaMaterial(material), result);
            if (ore) {
                addTool(b, label + " (Ore)", kind, *ore, result);
            }
        }
    }
}

} // namespace

BookStats registerBetaRecipes(RecipeRegistry& registry, const BetaRecipeOptions& options) {
    BookBuilder b{registry};
    const bool ores = options.acceptLegacyOres;
    const Ingredient planks = Ingredient::of(kPlanks);
    const Ingredient cobble = Ingredient::of(kCobble);
    const Ingredient iron = vanillaMaterial(ToolMaterial::Iron);
    const Ingredient ironOre = Ingredient::of(toItemId(BlockId::IronOre));
    const Ingredient redstone = Ingredient::of(items::Redstone);
    const Ingredient redstoneOre = Ingredient::of(toItemId(BlockId::RedstoneOre));
    const Ingredient sugarCane = Ingredient::of(toItemId(BlockId::SugarCane));

    // --- Basic wood processing (all fit the 2x2 inventory grid) ---
    // The codebase has a single plank block, so every log type yields oak planks.
    b.shapeless("4 Planks",
                {Ingredient::anyOf({toItemId(BlockId::OakLog), toItemId(BlockId::BirchLog),
                                    toItemId(BlockId::PineLog)})},
                {kPlanks, 4});
    b.shaped("4 Sticks", {"#", "#"}, {{'#', planks}}, {items::Stick, 4});
    b.shaped("Workbench", {"##", "##"}, {{'#', planks}}, {toItemId(BlockId::CraftingTable), 1});
    b.shaped("4 Torches", {"C", "#"}, {{'C', Ingredient::of(items::Coal)}, {'#', Ingredient::of(items::Stick)}},
             {toItemId(BlockId::Torch), 4});
    b.shaped("Sandstone", {"##", "##"}, {{'#', Ingredient::of(toItemId(BlockId::Sand))}},
             {toItemId(BlockId::Sandstone), 1});
    // Glowstone and snow blocks drop their items when mined; these turn them back.
    b.shaped("Glowstone", {"##", "##"}, {{'#', Ingredient::of(items::GlowstoneDust)}},
             {toItemId(BlockId::Glowstone), 1});
    b.shaped("Snow Block", {"##", "##"}, {{'#', Ingredient::of(items::Snowball)}},
             {toItemId(BlockId::SnowBlock), 1});

    // --- Workbench-only blocks ---
    b.shaped("Furnace", {"###", "# #", "###"}, {{'#', cobble}}, {toItemId(BlockId::Furnace), 1});
    b.shaped("Chest", {"###", "# #", "###"}, {{'#', planks}}, {items::Chest, 1});
    // Legacy CraftingSystem yielded 6 slabs; vanilla Beta 1.7.3 yields 3.
    b.shaped("Stone Slabs", {"###"}, {{'#', cobble}},
             {toItemId(BlockId::CobbleSlab), static_cast<uint8_t>(options.legacySlabYield ? 6 : 3)});
    b.shaped("3 Paper", {"###"}, {{'#', sugarCane}}, {items::Paper, 3});
    b.shaped("Book", {"#", "#", "#"}, {{'#', Ingredient::of(items::Paper)}}, {items::Book, 1});
    b.shaped("Bookshelf", {"###", "BBB", "###"}, {{'#', planks}, {'B', Ingredient::of(items::Book)}},
             {toItemId(BlockId::Bookshelf), 1});
    if (options.legacySugarCaneBookshelf) {
        // Legacy shortcut kept because no book item existed before this module.
        b.shaped("Bookshelf (Sugar Cane)", {"###", "SSS", "###"}, {{'#', planks}, {'S', sugarCane}},
                 {toItemId(BlockId::Bookshelf), 1});
    }
    b.shaped("Piston", {"TTT", "#X#", "#R#"}, {{'T', planks}, {'#', cobble}, {'X', iron}, {'R', redstone}},
             {toItemId(BlockId::Piston), 1});
    if (ores) {
        b.shaped("Piston (Ore)", {"TTT", "#X#", "#R#"},
                 {{'T', planks}, {'#', cobble}, {'X', ironOre}, {'R', redstoneOre}},
                 {toItemId(BlockId::Piston), 1});
    }

    // --- Tools & weapons: 5 kinds x 5 materials ---
    addTools(b, options);

    // --- Containers / by-products ---
    b.shaped("Bucket", {"# #", " # "}, {{'#', iron}}, {items::Bucket, 1});
    if (ores) {
        b.shaped("Bucket (Ore)", {"# #", " # "}, {{'#', ironOre}}, {items::Bucket, 1});
    }
    b.shaped("Cake", {"MMM", "SES", "WWW"},
             {{'M', Ingredient::of(items::MilkBucket).withRemainder(items::Bucket)},
              {'S', Ingredient::of(items::Sugar)},
              {'E', Ingredient::of(items::Egg)},
              {'W', Ingredient::of(items::Wheat)}},
             {items::Cake, 1});

    // --- S5 survival blocks (Beta 1.7.3 yields: 2 ladders, 2 fences, 4 stairs) ---
    const Ingredient stick = Ingredient::of(items::Stick);
    b.shaped("2 Ladders", {"# #", "###", "# #"}, {{'#', stick}}, {toItemId(BlockId::Ladder), 2});
    b.shaped("2 Fences", {"###", "###"}, {{'#', stick}}, {toItemId(BlockId::Fence), 2});
    b.shaped("4 Wooden Stairs", {"#  ", "## ", "###"}, {{'#', planks}}, {toItemId(BlockId::WoodStairs), 4});
    b.shaped("4 Cobblestone Stairs", {"#  ", "## ", "###"}, {{'#', cobble}},
             {toItemId(BlockId::CobbleStairs), 4});
    // S5.6-S5.8 (Beta 1.7.3 RecipesFood / RecipesCrafting / CraftingManager: 1 door, 1 bed).
    b.shaped("Wooden Door", {"##", "##", "##"}, {{'#', planks}}, {items::WoodenDoor, 1});
    // S8.5 redstone (Beta 1.7.3 CraftingManager / RecipesCrafting: one of each, iron door 1).
    const Ingredient stone = Ingredient::of(toItemId(BlockId::Stone));
    b.shaped("Iron Door", {"##", "##", "##"}, {{'#', iron}}, {items::IronDoor, 1});
    if (ores) {
        b.shaped("Iron Door (Ore)", {"##", "##", "##"}, {{'#', ironOre}}, {items::IronDoor, 1});
    }
    b.shaped("Lever", {"X", "#"}, {{'X', stick}, {'#', cobble}}, {toItemId(BlockId::Lever), 1});
    b.shaped("Redstone Torch", {"X", "#"}, {{'X', redstone}, {'#', stick}}, {toItemId(BlockId::RedstoneTorchOn), 1});
    b.shaped("Stone Pressure Plate", {"##"}, {{'#', stone}}, {toItemId(BlockId::StonePressurePlate), 1});
    b.shaped("Wooden Pressure Plate", {"##"}, {{'#', planks}}, {toItemId(BlockId::WoodPressurePlate), 1});
    b.shaped("Button", {"#", "#"}, {{'#', stone}}, {toItemId(BlockId::StoneButton), 1});
    b.shaped("Bread", {"###"}, {{'#', Ingredient::of(items::Wheat)}}, {items::Bread, 1});
    b.shaped("Bed", {"###", "XXX"}, {{'#', Ingredient::of(toItemId(BlockId::Wool))}, {'X', planks}},
             {items::Bed, 1});
    // S6.6 (Beta 1.7.3 CraftingManager): flint and steel, shapeless; TNT from gunpowder and sand.
    b.shapeless("Flint and Steel", {iron, Ingredient::of(items::Flint)}, {items::FlintAndSteel, 1});
    b.shaped("TNT", {"X#X", "#X#", "X#X"},
             {{'X', Ingredient::of(items::Gunpowder)}, {'#', Ingredient::of(toItemId(BlockId::Sand))}},
             {toItemId(BlockId::TNT), 1});
    // S6.7 armor (Beta 1.7.3 RecipesArmor, same order: per material, helmet to boots). Chain
    // armor is not craftable.
    for (const auto& [material, x] : {std::pair{gameplay::ArmorMaterial::Leather, Ingredient::of(items::Leather)},
                                      std::pair{gameplay::ArmorMaterial::Iron, iron},
                                      std::pair{gameplay::ArmorMaterial::Diamond, Ingredient::of(items::Diamond)},
                                      std::pair{gameplay::ArmorMaterial::Gold, Ingredient::of(items::GoldIngot)}}) {
        const auto armor = [&](gameplay::ArmorSlot slot) {
            const ItemId id = gameplay::armorItemId(material, slot);
            return std::pair{gameplay::armorName(id), RecipeResult{id, 1}};
        };
        const std::initializer_list<PatternKey> keys{{'X', x}};
        const auto [helmet, helmetResult] = armor(gameplay::ArmorSlot::Helmet);
        b.shaped(helmet, {"XXX", "X X"}, keys, helmetResult);
        const auto [plate, plateResult] = armor(gameplay::ArmorSlot::Chestplate);
        b.shaped(plate, {"X X", "XXX", "XXX"}, keys, plateResult);
        const auto [legs, legsResult] = armor(gameplay::ArmorSlot::Leggings);
        b.shaped(legs, {"XXX", "X X", "X X"}, keys, legsResult);
        const auto [boots, bootsResult] = armor(gameplay::ArmorSlot::Boots);
        b.shaped(boots, {"X X", "X X"}, keys, bootsResult);
    }
    return b.stats();
}

RecipeRegistry makeBetaRecipeBook(const BetaRecipeOptions& options) {
    RecipeRegistry reg;
    (void)registerBetaRecipes(reg, options);
    return reg;
}

} // namespace betacraft::crafting
