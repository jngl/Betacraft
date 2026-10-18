#pragma once

#include "gameplay/ItemIds.hpp"
#include "world/Block.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace betacraft::render {

// Atlas tiles of the procedural item icons that have no BlockDef (TextureAtlas.cpp):
// [kind][material] for every tool the legacy pickaxe tiles 132..135 do not cover, plus the
// generic sprites below.
inline constexpr uint8_t kGoldPickaxeTile = 136;
inline constexpr uint8_t kFirstToolTile = 137;    // Axes, shovels, swords, hoes: 5 materials each
inline constexpr uint8_t kGlowstoneDustTile = 157;
inline constexpr uint8_t kGenericItemTile = 158;  // Tinted per item
// S6.7 armor icons, drawn light so each material tints them: helmet, chestplate, leggings,
// boots (gameplay::ArmorSlot order). S6.10 mob spawner cage at Beta's own tile.
inline constexpr std::array<uint8_t, 4> kArmorIconTiles{238, 239, 254, 223};
inline constexpr uint8_t kMobSpawnerTile = 65;

// How an item looks in the HUD, in the hand and as a dropped entity.
struct ItemAppearance {
    std::string name;
    std::array<uint8_t, 6> faceTiles{}; // PosX, NegX, PosY, NegY, PosZ, NegZ
    glm::vec3 tint{1.0f};
    bool flat{true}; // Thin card (items, tools, flora, torches) rather than a cube
    std::optional<world::BlockId> block; // The world block it is, if any
    bool tintGrassTop{false};
    bool tintFoliage{false};

    [[nodiscard]] uint8_t iconTile() const noexcept { return faceTiles[4]; }
};

// Tool icon tile for kinds/materials without a BlockDef tile; nullopt for the four legacy
// pickaxes (their BlockDef already has one).
[[nodiscard]] std::optional<uint8_t> toolIconTile(gameplay::ToolKind kind, gameplay::ToolMaterial material) noexcept;

[[nodiscard]] ItemAppearance itemAppearance(gameplay::ItemId id);

} // namespace betacraft::render
