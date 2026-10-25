#include "render/ItemAppearance.hpp"

#include "crafting/ItemIds.hpp"
#include "gameplay/Armor.hpp"
#include "gameplay/ToolDefinitions.hpp"

#include <string_view>

namespace betacraft::render {

namespace {

[[nodiscard]] constexpr std::array<uint8_t, 6> allFaces(uint8_t tile) noexcept {
    return {tile, tile, tile, tile, tile, tile};
}

struct NamedItem {
    gameplay::ItemId id;
    std::string_view name;
    glm::vec3 tint;
};

namespace items = gameplay::items;

// Items at 256 and above that the recipe book or the drop tables can produce. They share the
// generic sprite, told apart by tint.
constexpr std::array<NamedItem, 33> kNamedItems{{
    {items::Diamond, "Diamond", {0.40f, 0.93f, 0.96f}},
    {items::IronIngot, "Iron Ingot", {0.86f, 0.86f, 0.88f}},
    {items::GoldIngot, "Gold Ingot", {0.98f, 0.84f, 0.25f}},
    {items::Apple, "Apple", {0.86f, 0.12f, 0.10f}},
    {items::Bowl, "Bowl", {0.55f, 0.38f, 0.20f}},
    {items::MushroomStew, "Mushroom Stew", {0.70f, 0.52f, 0.36f}},
    {items::Feather, "Feather", {0.96f, 0.96f, 0.94f}},
    {items::Seeds, "Seeds", {0.45f, 0.70f, 0.25f}},
    {items::Wheat, "Wheat", {0.85f, 0.75f, 0.35f}},
    {items::Bread, "Bread", {0.80f, 0.60f, 0.30f}},
    {items::RawPorkchop, "Raw Porkchop", {0.95f, 0.60f, 0.60f}},
    {items::CookedPorkchop, "Cooked Porkchop", {0.72f, 0.45f, 0.28f}},
    {items::GoldenApple, "Golden Apple", {0.98f, 0.84f, 0.25f}},
    {items::Bucket, "Bucket", {0.72f, 0.72f, 0.75f}},
    {items::WaterBucket, "Water Bucket", {0.30f, 0.45f, 0.95f}},
    {items::LavaBucket, "Lava Bucket", {0.95f, 0.45f, 0.10f}},
    {items::Redstone, "Redstone", {0.80f, 0.08f, 0.06f}},
    {items::Snowball, "Snowball", {0.97f, 0.98f, 1.00f}},
    {items::MilkBucket, "Milk Bucket", {0.98f, 0.98f, 0.95f}},
    {items::Paper, "Paper", {0.95f, 0.95f, 0.90f}},
    {items::Book, "Book", {0.55f, 0.30f, 0.18f}},
    {items::Egg, "Egg", {0.93f, 0.88f, 0.75f}},
    {items::Sugar, "Sugar", {0.98f, 0.98f, 0.98f}},
    {items::Cake, "Cake", {0.95f, 0.85f, 0.75f}},
    {items::Cookie, "Cookie", {0.78f, 0.52f, 0.28f}},
    // Mob drops, flint and flint and steel.
    {items::FlintAndSteel, "Flint and Steel", {0.62f, 0.62f, 0.66f}},
    {items::Bow, "Bow", {0.60f, 0.42f, 0.22f}},
    {items::Arrow, "Arrow", {0.82f, 0.80f, 0.76f}},
    {items::String, "String", {0.92f, 0.92f, 0.92f}},
    {items::Gunpowder, "Gunpowder", {0.38f, 0.38f, 0.38f}},
    {items::Flint, "Flint", {0.22f, 0.22f, 0.24f}},
    {items::Leather, "Leather", {0.62f, 0.36f, 0.18f}},
    {items::Bone, "Bone", {0.93f, 0.92f, 0.86f}},
}};

} // namespace

std::optional<uint8_t> toolIconTile(gameplay::ToolKind kind, gameplay::ToolMaterial material) noexcept {
    const auto k = static_cast<uint8_t>(kind);
    const auto m = static_cast<uint8_t>(material);
    if (kind == gameplay::ToolKind::Pickaxe) {
        return material == gameplay::ToolMaterial::Gold ? std::optional<uint8_t>{kGoldPickaxeTile} : std::nullopt;
    }
    return static_cast<uint8_t>(kFirstToolTile + (k - 1U) * 5U + m);
}

ItemAppearance itemAppearance(gameplay::ItemId rawId) {
    const gameplay::ItemId id = gameplay::canonicalize(rawId);
    ItemAppearance out{};
    // Stick, coal and the four pickaxes keep the sprite and name of their legacy BlockDef.
    std::optional<voxel::BlockId> block = gameplay::legacyItemBlock(id);
    if (!block) block = crafting::toBlockId(id);
    if (block && *block != voxel::BlockId::Air) {
        const voxel::BlockDef& def = voxel::BlockRegistry::get(*block);
        out.name = std::string(def.name);
        out.faceTiles = def.faceTiles;
        out.flat = def.isItemOnly || def.shape == voxel::RenderShape::Cross || def.shape == voxel::RenderShape::Torch ||
                   def.shape == voxel::RenderShape::Ladder || def.shape == voxel::RenderShape::Crops ||
                   def.shape == voxel::RenderShape::Lever || def.id == voxel::BlockId::RedstoneWire;
        out.tintGrassTop = def.tintGrassTop;
        out.tintFoliage = def.tintFoliage;
        if (!def.isItemOnly) {
            out.block = *block;
        }
        return out;
    }
    if (const auto tool = gameplay::findTool(id)) {
        const auto info = gameplay::toolInfo(id);
        out.name = std::string(tool->name);
        out.faceTiles = allFaces(toolIconTile(info->kind, info->material).value_or(kGenericItemTile));
        return out;
    }
    // Armor: one sprite per slot, tinted by material.
    if (const std::optional<gameplay::ArmorInfo> armor = gameplay::armorInfo(id)) {
        static constexpr std::array<glm::vec3, 5> kArmorTints{{
            {0.63f, 0.40f, 0.24f}, // Leather
            {0.60f, 0.60f, 0.62f}, // Chain
            {0.86f, 0.86f, 0.88f}, // Iron
            {0.40f, 0.93f, 0.96f}, // Diamond
            {0.98f, 0.84f, 0.25f}, // Gold
        }};
        out.name = std::string(gameplay::armorName(id));
        out.faceTiles = allFaces(kArmorIconTiles[static_cast<size_t>(armor->slot)]);
        out.tint = kArmorTints[static_cast<size_t>(*gameplay::armorMaterial(id))];
        return out;
    }
    // The door and the bed items show their block's sprite.
    if (id == items::WoodenDoor) {
        out.name = "Wooden Door";
        out.faceTiles = allFaces(voxel::kDoorUpperTile);
        return out;
    }
    if (id == items::IronDoor) {
        out.name = "Iron Door";
        out.faceTiles = allFaces(voxel::kIronDoorUpperTile);
        return out;
    }
    if (id == items::Bed) {
        out.name = "Bed";
        out.faceTiles = allFaces(voxel::kBedHeadTopTile);
        return out;
    }
    if (id == items::GlowstoneDust) {
        out.name = "Glowstone Dust";
        out.faceTiles = allFaces(kGlowstoneDustTile);
        return out;
    }
    out.faceTiles = allFaces(kGenericItemTile);
    for (const NamedItem& item : kNamedItems) {
        if (item.id == id) {
            out.name = std::string(item.name);
            out.tint = item.tint;
            return out;
        }
    }
    out.name = "Item #" + std::to_string(id);
    out.tint = glm::vec3(0.75f);
    return out;
}

} // namespace betacraft::render
