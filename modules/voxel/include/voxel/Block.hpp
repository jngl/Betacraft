#pragma once

#include "core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace betacraft::voxel {

enum class BlockId : uint8_t {
    Air = 0,
    Stone = 1,
    Grass = 2,
    Dirt = 3,
    Cobblestone = 4,
    OakPlanks = 5,
    Sapling = 6,
    Bedrock = 7,
    Water = 8,       // Engine's canonical water (Beta "flowing water"); worldgen and old saves use it
    StillWater = 9,  // Beta stationary water: same block, kept so Beta ids 8..11 all resolve
    Lava = 10,       // Canonical lava (Beta "flowing lava", what Beta caves fill with)
    StillLava = 11,  // Beta stationary lava
    Sand = 12,
    Gravel = 13,
    GoldOre = 14,
    IronOre = 15,
    CoalOre = 16,
    OakLog = 17,
    Leaves = 18,
    Glass = 20,
    LapisOre = 21,
    Sandstone = 24,
    Bed = 26,        // S5.4/S5.8: meta bits 0-1 orientation, bit 3 head half
    TallGrass = 31,
    DeadBush = 32,
    Piston = 33,
    PistonHead = 34,
    Wool = 35,
    Dandelion = 37,
    Rose = 38,
    CobbleSlab = 44,
    Bricks = 45,
    TNT = 46,
    Bookshelf = 47,
    MossyCobble = 48,
    Obsidian = 49,
    Torch = 50,
    MobSpawner = 52, // S6.10 dungeon spawner; its state lives in game::MobSpawner (not saved)
    WoodStairs = 53, // S5.10: meta 0..3 = the side the high step faces (+X, -X, +Z, -Z)
    Chest = 54,
    RedstoneWire = 55, // S8.5: meta = power 0..15
    DiamondOre = 56,
    CraftingTable = 58,
    Crops = 59,      // S5.4/S5.7: meta = growth stage 0..7
    Farmland = 60,   // S5.4/S5.7: meta = moisture 0..7
    Furnace = 61,
    LitFurnace = 62, // Burning furnace: same block entity as Furnace, light 13
    WoodDoor = 64,   // S5.4/S5.6: meta bits 0-1 orientation, bit 2 open, bit 3 upper half
    Ladder = 65,     // S5.5: meta 2..5 = Beta side of the wall it hangs on
    CobbleStairs = 67,
    Lever = 69,              // S8.5: meta 0x7 = attachment (kAttach*), 0x8 = on
    StonePressurePlate = 70, // S8.5: meta 0x1 = pressed
    IronDoor = 71,           // S8.5: door metadata (S5.6); opened by redstone only
    WoodPressurePlate = 72,  // S8.5: meta 0x1 = pressed
    RedstoneOre = 73,
    RedstoneTorchOff = 75,   // S8.5: meta = attachment, like Lever
    RedstoneTorchOn = 76,    // Light 7
    StoneButton = 77,        // S8.5: meta 0x7 = attachment, 0x8 = pressed
    SnowGrass = 78,
    Ice = 79,
    SnowBlock = 80,
    Cactus = 81,
    Clay = 82,
    SugarCane = 83,
    Fence = 85,
    Glowstone = 89,
    BirchLog = 90,
    PineLog = 91,

    // Craftable Tools & Items (IDs >= 100)
    Stick = 100,
    CoalItem = 101,
    WoodPickaxe = 102,
    StonePickaxe = 103,
    IronPickaxe = 104,
    DiamondPickaxe = 105,

    Count = 106
};

enum class RenderShape : uint8_t {
    None = 0,
    Cube,
    Cross,
    Torch,
    Slab,
    Cactus,
    Liquid,
    // S5.4 shapes whose geometry and boxes come from voxel/BlockShapes.hpp. All but Fence
    // depend on the metadata (Fence depends on its neighbours).
    Ladder,
    Door,
    Bed,
    Fence,
    Crops,
    Farmland,
    Stairs,
    // S8.5: a plate lying on the floor (redstone wire, pressure plates) and a small part
    // attached to a wall or the floor (lever, button), both from voxel/BlockShapes.hpp.
    Flat,
    Lever
};

// Whether a metadata change can alter the geometry of a block of this shape (S5.4): such
// changes re-mesh the section. Liquids keep their full-cube look whatever their level.
[[nodiscard]] constexpr bool shapeDependsOnMetadata(RenderShape shape) noexcept {
    switch (shape) {
        case RenderShape::Ladder:
        case RenderShape::Door:
        case RenderShape::Bed:
        case RenderShape::Crops:
        case RenderShape::Farmland:
        case RenderShape::Stairs:
        case RenderShape::Flat:
        case RenderShape::Lever:
            return true;
        default:
            return false;
    }
}

// Shapes drawn from boxes or planes inside the cell (BlockShapes.hpp) instead of the
// whole-cell face loop of the meshers.
[[nodiscard]] constexpr bool isDetailedShape(RenderShape shape) noexcept {
    return shape >= RenderShape::Ladder && shape <= RenderShape::Lever;
}

enum class RenderLayer : uint8_t {
    Opaque = 0,
    AlphaCutout,
    Translucent
};

enum class SoundCategory : uint8_t {
    Stone = 0,
    Grass,
    Dirt,
    Wood,
    Sand,
    Glass,
    Cloth,
    Water
};

// Atlas tiles of the S5.4 shapes (Beta terrain.png positions where the procedural atlas has
// them free).
inline constexpr uint8_t kDoorUpperTile = 81;
inline constexpr uint8_t kLadderTile = 83;
inline constexpr uint8_t kFarmlandWetTile = 86;
inline constexpr uint8_t kFarmlandDryTile = 87;
inline constexpr uint8_t kCropsFirstTile = 88; // + growth stage 0..7
inline constexpr uint8_t kDoorLowerTile = 97;
inline constexpr uint8_t kBedHeadTopTile = 100;
inline constexpr uint8_t kBedFootTopTile = 101;
inline constexpr uint8_t kBedHeadSideTile = 102;
inline constexpr uint8_t kBedFootSideTile = 103;
// S8.5 redstone tiles (Beta terrain.png positions).
inline constexpr uint8_t kIronDoorUpperTile = 82;
inline constexpr uint8_t kLeverTile = 96;
inline constexpr uint8_t kIronDoorLowerTile = 98;
inline constexpr uint8_t kRedstoneTorchOnTile = 99;
inline constexpr uint8_t kRedstoneTorchOffTile = 115;
inline constexpr uint8_t kRedstoneWireOnTile = 164;
inline constexpr uint8_t kRedstoneWireOffTile = 165;

struct BlockDef {
    BlockId id{BlockId::Air};
    std::string_view name{"Air"};
    bool solid{false};          // Collidable AABB
    bool opaque{false};         // Blocks neighbor faces and light
    bool selectable{false};     // Raycastable by player
    bool replaceable{true};     // Can be overwritten when placing a block (Air, Water, TallGrass)
    bool isItemOnly{false};     // Tool/Resource item, cannot be placed as a world block
    uint8_t lightEmission{0};   // 0..15 (e.g., Torch = 14, Glowstone = 15)
    uint8_t lightOpacity{0};    // 0..15 light attenuation
    float hardness{0.0f};       // Beta 1.7.3 Block.blockHardness (see gameplay::computeDig); < 0: unbreakable
    float toolMultiplier{1.0f}; // Speed multiplier when using a pickaxe
    RenderShape shape{RenderShape::None};
    RenderLayer layer{RenderLayer::Opaque};
    SoundCategory sound{SoundCategory::Stone};
    BlockId dropId{BlockId::Air};
    bool tintGrassTop{false};
    bool tintFoliage{false};

    // Atlas tile indices (0..255 in 16x16 grid): PosX, NegX, PosY, NegY, PosZ, NegZ
    std::array<uint8_t, 6> faceTiles{0, 0, 0, 0, 0, 0};
    // S6.5 Beta Block.getExplosionResistance (blockResistance / 5): what one block takes off
    // an explosion ray (gameplay::explosionBlocks). BlockRegistry::initialize sets it to the
    // hardness (never below 0), then applies Beta's setResistance overrides.
    float blastResistance{0.0f};

    [[nodiscard]] constexpr uint8_t tileForFace(core::FaceDir dir) const noexcept {
        return faceTiles[static_cast<size_t>(dir)];
    }
};

// Still and flowing variants are one liquid: the per-voxel metadata nibble, not the id,
// carries the fluid level (see Chunk::getMetadata).
[[nodiscard]] constexpr bool isWaterBlock(BlockId id) noexcept {
    return id == BlockId::Water || id == BlockId::StillWater;
}

[[nodiscard]] constexpr bool isLavaBlock(BlockId id) noexcept {
    return id == BlockId::Lava || id == BlockId::StillLava;
}

// S5.6/S8.5: both door blocks share the geometry and the two-half metadata.
[[nodiscard]] constexpr bool isDoorBlock(BlockId id) noexcept {
    return id == BlockId::WoodDoor || id == BlockId::IronDoor;
}

[[nodiscard]] constexpr bool isSameLiquid(BlockId a, BlockId b) noexcept {
    return (isWaterBlock(a) && isWaterBlock(b)) || (isLavaBlock(a) && isLavaBlock(b));
}

class BlockRegistry {
public:
    static void initialize() noexcept;

    [[nodiscard]] static const BlockDef& get(BlockId id) noexcept;

    [[nodiscard]] static bool isSolid(BlockId id) noexcept {
        return get(id).solid;
    }

    [[nodiscard]] static bool isOpaque(BlockId id) noexcept {
        return get(id).opaque;
    }

    [[nodiscard]] static bool isLiquid(BlockId id) noexcept {
        return isWaterBlock(id) || isLavaBlock(id);
    }

    [[nodiscard]] static float getPickaxeSpeedMultiplier(BlockId heldItem, BlockId targetBlock) noexcept;

private:
    static std::array<BlockDef, 256> s_defs;
    static bool s_initialized;
};

} // namespace betacraft::voxel
