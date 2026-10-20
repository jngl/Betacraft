#include "voxel/Block.hpp"

namespace betacraft::voxel {

std::array<BlockDef, 256> BlockRegistry::s_defs{};
bool BlockRegistry::s_initialized = false;

namespace {

constexpr std::array<uint8_t, 6> allFaces(uint8_t tile) noexcept {
    return {tile, tile, tile, tile, tile, tile};
}

constexpr std::array<uint8_t, 6> topBottomSide(uint8_t top, uint8_t bottom, uint8_t side) noexcept {
    // PosX, NegX, PosY (top), NegY (bottom), PosZ, NegZ
    return {side, side, top, bottom, side, side};
}

constexpr std::array<uint8_t, 6> frontTopBottomSide(uint8_t front, uint8_t top, uint8_t bottom, uint8_t side) noexcept {
    // PosZ is Front
    return {side, side, top, bottom, front, side};
}

} // namespace

void BlockRegistry::initialize() noexcept {
    if (s_initialized) {
        return;
    }

    for (size_t i = 0; i < s_defs.size(); ++i) {
        s_defs[i] = BlockDef{};
        s_defs[i].id = static_cast<BlockId>(i);
    }

    auto reg = [](BlockDef def) {
        s_defs[static_cast<size_t>(def.id)] = def;
    };

    // Air (0)
    reg(BlockDef{
        .id = BlockId::Air,
        .name = "Air",
        .solid = false,
        .opaque = false,
        .selectable = false,
        .replaceable = true,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 0.0f,
        .shape = RenderShape::None,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::Air,
        .faceTiles = allFaces(0)
    });

    // Stone (1) -> drops Cobblestone
    reg(BlockDef{
        .id = BlockId::Stone,
        .name = "Stone",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 1.5f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::Cobblestone,
        .faceTiles = allFaces(1)
    });

    // Grass (2) -> top=0, bottom=2(Dirt), side=3 -> drops Dirt
    reg(BlockDef{
        .id = BlockId::Grass,
        .name = "Grass Block",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 0.6f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Grass,
        .dropId = BlockId::Dirt,
        .tintGrassTop = true,
        .faceTiles = topBottomSide(0, 2, 3)
    });

    // Dirt (3)
    reg(BlockDef{
        .id = BlockId::Dirt,
        .name = "Dirt",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 0.5f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Dirt,
        .dropId = BlockId::Dirt,
        .faceTiles = allFaces(2)
    });

    // Cobblestone (4)
    reg(BlockDef{
        .id = BlockId::Cobblestone,
        .name = "Cobblestone",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 2.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::Cobblestone,
        .faceTiles = allFaces(16)
    });

    // Oak Planks (5)
    reg(BlockDef{
        .id = BlockId::OakPlanks,
        .name = "Wooden Planks",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 2.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::OakPlanks,
        .faceTiles = allFaces(4)
    });

    // Sapling (6)
    reg(BlockDef{
        .id = BlockId::Sapling,
        .name = "Sapling",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 0.0f,
        .shape = RenderShape::Cross,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Grass,
        .dropId = BlockId::Sapling,
        .faceTiles = allFaces(15)
    });

    // Bedrock (7)
    reg(BlockDef{
        .id = BlockId::Bedrock,
        .name = "Bedrock",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = -1.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::Air,
        .faceTiles = allFaces(17)
    });

    // Water (8)
    reg(BlockDef{
        .id = BlockId::Water,
        .name = "Water",
        .solid = false,
        .opaque = false,
        .selectable = false,
        .replaceable = true,
        .lightEmission = 0,
        .lightOpacity = 2,
        .hardness = -1.0f,
        .shape = RenderShape::Liquid,
        .layer = RenderLayer::Translucent,
        .sound = SoundCategory::Water,
        .dropId = BlockId::Air,
        .faceTiles = allFaces(205)
    });

    // Stationary water (9): identical to Water, the nibble holds the level
    {
        BlockDef stillWater = s_defs[static_cast<size_t>(BlockId::Water)];
        stillWater.id = BlockId::StillWater;
        stillWater.name = "Stationary Water";
        reg(stillWater);
    }

    // Lava (10) / Stationary lava (11): Beta lightValue 1.0 and lightOpacity 255, render pass 0
    reg(BlockDef{
        .id = BlockId::Lava,
        .name = "Lava",
        .solid = false,
        .opaque = false,
        .selectable = false,
        .replaceable = true,
        .lightEmission = 15,
        .lightOpacity = 15,
        .hardness = -1.0f,
        .shape = RenderShape::Liquid,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Water,
        .dropId = BlockId::Air,
        .faceTiles = allFaces(237)
    });
    {
        BlockDef stillLava = s_defs[static_cast<size_t>(BlockId::Lava)];
        stillLava.id = BlockId::StillLava;
        stillLava.name = "Stationary Lava";
        reg(stillLava);
    }

    // Sand (12)
    reg(BlockDef{
        .id = BlockId::Sand,
        .name = "Sand",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 0.5f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Sand,
        .dropId = BlockId::Sand,
        .faceTiles = allFaces(18)
    });

    // Gravel (13)
    reg(BlockDef{
        .id = BlockId::Gravel,
        .name = "Gravel",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 0.6f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Sand,
        .dropId = BlockId::Gravel,
        .faceTiles = allFaces(19)
    });

    // Gold Ore (14)
    reg(BlockDef{
        .id = BlockId::GoldOre,
        .name = "Gold Ore",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 3.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::GoldOre,
        .faceTiles = allFaces(32)
    });

    // Iron Ore (15)
    reg(BlockDef{
        .id = BlockId::IronOre,
        .name = "Iron Ore",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 3.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::IronOre,
        .faceTiles = allFaces(33)
    });

    // Coal Ore (16)
    reg(BlockDef{
        .id = BlockId::CoalOre,
        .name = "Coal Ore",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 3.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::CoalItem,
        .faceTiles = allFaces(34)
    });

    // Oak Log (17)
    reg(BlockDef{
        .id = BlockId::OakLog,
        .name = "Oak Wood",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 2.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::OakLog,
        .faceTiles = topBottomSide(21, 21, 20)
    });

    // Leaves (18)
    reg(BlockDef{
        .id = BlockId::Leaves,
        .name = "Leaves",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 1,
        .hardness = 0.2f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Grass,
        .dropId = BlockId::Sapling,
        .tintFoliage = true,
        .faceTiles = allFaces(52)
    });

    // Glass (20)
    reg(BlockDef{
        .id = BlockId::Glass,
        .name = "Glass",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 0.3f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Glass,
        .dropId = BlockId::Glass,
        .faceTiles = allFaces(49)
    });

    // Lapis Ore (21)
    reg(BlockDef{
        .id = BlockId::LapisOre,
        .name = "Lapis Lazuli Ore",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 3.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::LapisOre,
        .faceTiles = allFaces(160)
    });

    // Sandstone (24)
    reg(BlockDef{
        .id = BlockId::Sandstone,
        .name = "Sandstone",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 0.8f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::Sandstone,
        .faceTiles = topBottomSide(176, 208, 192)
    });

    // Tall Grass (31) - Beta 1.6/1.7 signature flora!
    reg(BlockDef{
        .id = BlockId::TallGrass,
        .name = "Tall Grass",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = true,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 0.0f,
        .shape = RenderShape::Cross,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Grass,
        .dropId = BlockId::Air,
        .tintGrassTop = true,
        .faceTiles = allFaces(39)
    });

    // Dead Bush (32)
    reg(BlockDef{
        .id = BlockId::DeadBush,
        .name = "Dead Shrub",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = true,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 0.0f,
        .shape = RenderShape::Cross,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Grass,
        .dropId = BlockId::Stick,
        .faceTiles = allFaces(55)
    });

    // Piston (33) - Iconic Beta 1.7 feature!
    reg(BlockDef{
        .id = BlockId::Piston,
        .name = "Piston",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 0.5f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::Piston,
        .faceTiles = topBottomSide(107, 109, 108)
    });

    // Piston Head (34)
    reg(BlockDef{
        .id = BlockId::PistonHead,
        .name = "Piston Extension",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 0.5f,
        .shape = RenderShape::Slab,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::Piston,
        .faceTiles = topBottomSide(107, 107, 108)
    });

    // Wool (35)
    reg(BlockDef{
        .id = BlockId::Wool,
        .name = "Wool",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 0.8f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Cloth,
        .dropId = BlockId::Wool,
        .faceTiles = allFaces(64)
    });

    // Dandelion (37)
    reg(BlockDef{
        .id = BlockId::Dandelion,
        .name = "Dandelion",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 0.0f,
        .shape = RenderShape::Cross,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Grass,
        .dropId = BlockId::Dandelion,
        .faceTiles = allFaces(13)
    });

    // Rose (38)
    reg(BlockDef{
        .id = BlockId::Rose,
        .name = "Rose",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 0.0f,
        .shape = RenderShape::Cross,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Grass,
        .dropId = BlockId::Rose,
        .faceTiles = allFaces(12)
    });

    // Cobble Slab (44)
    reg(BlockDef{
        .id = BlockId::CobbleSlab,
        .name = "Stone Slab",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 2.0f,
        .shape = RenderShape::Slab,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::CobbleSlab,
        .faceTiles = topBottomSide(6, 6, 5)
    });

    // Bricks (45)
    reg(BlockDef{
        .id = BlockId::Bricks,
        .name = "Bricks",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 2.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::Bricks,
        .faceTiles = allFaces(7)
    });

    // TNT (46)
    reg(BlockDef{
        .id = BlockId::TNT,
        .name = "TNT",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 0.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Grass,
        .dropId = BlockId::TNT,
        .faceTiles = topBottomSide(9, 10, 8)
    });

    // Bookshelf (47)
    reg(BlockDef{
        .id = BlockId::Bookshelf,
        .name = "Bookshelf",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 1.5f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::Bookshelf,
        .faceTiles = topBottomSide(4, 4, 35)
    });

    // Mossy Cobblestone (48)
    reg(BlockDef{
        .id = BlockId::MossyCobble,
        .name = "Moss Stone",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 2.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::MossyCobble,
        .faceTiles = allFaces(36)
    });

    // Obsidian (49)
    reg(BlockDef{
        .id = BlockId::Obsidian,
        .name = "Obsidian",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 10.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::Obsidian,
        .faceTiles = allFaces(37)
    });

    // Torch (50)
    reg(BlockDef{
        .id = BlockId::Torch,
        .name = "Torch",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 14,
        .lightOpacity = 0,
        .hardness = 0.0f,
        .shape = RenderShape::Torch,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::Torch,
        .faceTiles = allFaces(80)
    });

    // Mob spawner (52, S6.10): Beta BlockMobSpawner, a see-through cage that drops nothing.
    reg(BlockDef{
        .id = BlockId::MobSpawner,
        .name = "Monster Spawner",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 5.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::Air,
        .faceTiles = allFaces(65)
    });

    // Chest (54): a full cube in Beta 1.7.3; its 27 slots live in the World's BlockEntityStore.
    reg(BlockDef{
        .id = BlockId::Chest,
        .name = "Chest",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 2.5f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::Chest,
        .faceTiles = frontTopBottomSide(27, 25, 25, 26)
    });

    // Diamond Ore (56)
    reg(BlockDef{
        .id = BlockId::DiamondOre,
        .name = "Diamond Ore",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 3.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::DiamondOre,
        .faceTiles = allFaces(50)
    });

    // Crafting Table (58)
    reg(BlockDef{
        .id = BlockId::CraftingTable,
        .name = "Workbench",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 2.5f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::CraftingTable,
        .faceTiles = frontTopBottomSide(60, 43, 4, 59)
    });

    // Furnace (61)
    reg(BlockDef{
        .id = BlockId::Furnace,
        .name = "Furnace",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 3.5f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::Furnace,
        .faceTiles = frontTopBottomSide(44, 62, 62, 45)
    });

    // Lit Furnace (62): the burning furnace keeps the Furnace's block entity (World
    // applyBlockEdit treats the two as compatible) and drops a plain furnace.
    reg(BlockDef{
        .id = BlockId::LitFurnace,
        .name = "Furnace",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 13,
        .lightOpacity = 15,
        .hardness = 3.5f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::Furnace,
        .faceTiles = frontTopBottomSide(61, 62, 62, 45)
    });

    // Redstone Ore (73)
    reg(BlockDef{
        .id = BlockId::RedstoneOre,
        .name = "Redstone Ore",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 4,
        .lightOpacity = 15,
        .hardness = 3.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::RedstoneOre,
        .faceTiles = allFaces(51)
    });

    // Snow Grass (78)
    reg(BlockDef{
        .id = BlockId::SnowGrass,
        .name = "Snowy Grass",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 0.6f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Grass,
        .dropId = BlockId::Dirt,
        .faceTiles = topBottomSide(66, 2, 68)
    });

    // Ice (79)
    reg(BlockDef{
        .id = BlockId::Ice,
        .name = "Ice",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 2,
        .hardness = 0.5f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Translucent,
        .sound = SoundCategory::Glass,
        .dropId = BlockId::Ice,
        .faceTiles = allFaces(67)
    });

    // Snow Block (80)
    reg(BlockDef{
        .id = BlockId::SnowBlock,
        .name = "Snow",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 0.2f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Cloth,
        .dropId = BlockId::SnowBlock,
        .faceTiles = allFaces(66)
    });

    // Cactus (81)
    reg(BlockDef{
        .id = BlockId::Cactus,
        .name = "Cactus",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 1,
        .hardness = 0.4f,
        .shape = RenderShape::Cactus,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Cloth,
        .dropId = BlockId::Cactus,
        .faceTiles = topBottomSide(69, 71, 70)
    });

    // Clay (82)
    reg(BlockDef{
        .id = BlockId::Clay,
        .name = "Clay",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 0.6f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Sand,
        .dropId = BlockId::Clay,
        .faceTiles = allFaces(72)
    });

    // Sugar Cane (83)
    reg(BlockDef{
        .id = BlockId::SugarCane,
        .name = "Sugar Cane",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 0.0f,
        .shape = RenderShape::Cross,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Grass,
        .dropId = BlockId::SugarCane,
        .faceTiles = allFaces(73)
    });

    // Glowstone (89)
    reg(BlockDef{
        .id = BlockId::Glowstone,
        .name = "Glowstone",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 15,
        .lightOpacity = 15,
        .hardness = 0.3f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Glass,
        .dropId = BlockId::Glowstone,
        .faceTiles = allFaces(105)
    });

    // Birch Log (90)
    reg(BlockDef{
        .id = BlockId::BirchLog,
        .name = "Birch Wood",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 2.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::BirchLog,
        .faceTiles = topBottomSide(21, 21, 117)
    });

    // Pine Log (91)
    reg(BlockDef{
        .id = BlockId::PineLog,
        .name = "Pine Wood",
        .solid = true,
        .opaque = true,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 2.0f,
        .shape = RenderShape::Cube,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::PineLog,
        .faceTiles = topBottomSide(21, 21, 116)
    });

    // --- S5.4 metadata-dependent shapes (geometry and boxes: BlockShapes.hpp) ---

    // Bed (26): 9/16 high; the head half (meta bit 3) shows the pillow tiles. Dropped as the
    // bed item by the game rules (S5.8), not by dropId.
    reg(BlockDef{
        .id = BlockId::Bed,
        .name = "Bed",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 0.2f,
        .shape = RenderShape::Bed,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Cloth,
        .dropId = BlockId::Air,
        // Sides of the foot half, foot top, planks underneath (head tiles: BlockShapes mesher)
        .faceTiles = topBottomSide(kBedFootTopTile, 4, kBedFootSideTile)
    });

    // Wooden stairs (53) and cobblestone stairs (67): Beta setLightOpacity(255), like slabs.
    reg(BlockDef{
        .id = BlockId::WoodStairs,
        .name = "Wooden Stairs",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 2.0f,
        .shape = RenderShape::Stairs,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::WoodStairs,
        .faceTiles = allFaces(4)
    });
    reg(BlockDef{
        .id = BlockId::CobbleStairs,
        .name = "Cobblestone Stairs",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 2.0f,
        .shape = RenderShape::Stairs,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::CobbleStairs,
        .faceTiles = allFaces(16)
    });

    // Crops (59): walk-through, tile 88 + growth stage. Harvest drops come from S5.7.
    reg(BlockDef{
        .id = BlockId::Crops,
        .name = "Crops",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 0.0f,
        .shape = RenderShape::Crops,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Grass,
        .dropId = BlockId::Air,
        .faceTiles = allFaces(kCropsFirstTile)
    });

    // Farmland (60): 15/16 high, wet top (tile 86) while its moisture metadata is above 0.
    reg(BlockDef{
        .id = BlockId::Farmland,
        .name = "Farmland",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 15,
        .hardness = 0.6f,
        .shape = RenderShape::Farmland,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Dirt,
        .dropId = BlockId::Dirt,
        .faceTiles = topBottomSide(kFarmlandDryTile, 2, 2)
    });

    // Wooden door (64): 3/16 thick, lower half tile 97, upper half 81. One item per door is
    // dropped by the game rules (S5.6).
    reg(BlockDef{
        .id = BlockId::WoodDoor,
        .name = "Wooden Door",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 3.0f,
        .shape = RenderShape::Door,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::Air,
        .faceTiles = allFaces(kDoorLowerTile)
    });

    // Ladder (65): a 2/16 plate against its wall, which the player climbs (S5.5).
    reg(BlockDef{
        .id = BlockId::Ladder,
        .name = "Ladder",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 0.4f,
        .shape = RenderShape::Ladder,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::Ladder,
        .faceTiles = allFaces(kLadderTile)
    });

    // Fence (85): a post with bars towards neighbouring fences, collides 1.5 high (S5.9).
    reg(BlockDef{
        .id = BlockId::Fence,
        .name = "Fence",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .lightOpacity = 0,
        .hardness = 2.0f,
        .shape = RenderShape::Fence,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::Fence,
        .faceTiles = allFaces(4)
    });

    // S8.5 redstone (Beta 1.7.3 hardness; none of them collides). The wire's power, the
    // lever's state and the plates' and button's press live in the metadata.
    reg(BlockDef{
        .id = BlockId::RedstoneWire,
        .name = "Redstone Wire",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .hardness = 0.0f,
        .shape = RenderShape::Flat,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::Air, // The redstone item (Harvesting)
        .faceTiles = allFaces(kRedstoneWireOffTile)
    });
    reg(BlockDef{
        .id = BlockId::Lever,
        .name = "Lever",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .hardness = 0.5f,
        .shape = RenderShape::Lever,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::Lever,
        .faceTiles = allFaces(kLeverTile)
    });
    reg(BlockDef{
        .id = BlockId::StonePressurePlate,
        .name = "Stone Pressure Plate",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .hardness = 0.5f,
        .shape = RenderShape::Flat,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::StonePressurePlate,
        .faceTiles = allFaces(1)
    });
    reg(BlockDef{
        .id = BlockId::IronDoor,
        .name = "Iron Door",
        .solid = true,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .hardness = 5.0f,
        .shape = RenderShape::Door,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::Air, // One iron door item per door (Harvesting)
        .faceTiles = allFaces(kIronDoorLowerTile)
    });
    reg(BlockDef{
        .id = BlockId::WoodPressurePlate,
        .name = "Wooden Pressure Plate",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .hardness = 0.5f,
        .shape = RenderShape::Flat,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::WoodPressurePlate,
        .faceTiles = allFaces(4)
    });
    reg(BlockDef{
        .id = BlockId::RedstoneTorchOff,
        .name = "Redstone Torch",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 0,
        .hardness = 0.0f,
        .shape = RenderShape::Torch,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::RedstoneTorchOn,
        .faceTiles = allFaces(kRedstoneTorchOffTile)
    });
    reg(BlockDef{
        .id = BlockId::RedstoneTorchOn,
        .name = "Redstone Torch",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .lightEmission = 7, // Beta setLightValue(0.5F)
        .hardness = 0.0f,
        .shape = RenderShape::Torch,
        .layer = RenderLayer::AlphaCutout,
        .sound = SoundCategory::Wood,
        .dropId = BlockId::RedstoneTorchOn,
        .faceTiles = allFaces(kRedstoneTorchOnTile)
    });
    reg(BlockDef{
        .id = BlockId::StoneButton,
        .name = "Button",
        .solid = false,
        .opaque = false,
        .selectable = true,
        .replaceable = false,
        .hardness = 0.5f,
        .shape = RenderShape::Lever,
        .layer = RenderLayer::Opaque,
        .sound = SoundCategory::Stone,
        .dropId = BlockId::StoneButton,
        .faceTiles = allFaces(1)
    });

    // Items / Tools (100..105)
    reg(BlockDef{
        .id = BlockId::Stick,
        .name = "Stick",
        .solid = false,
        .opaque = false,
        .selectable = false,
        .replaceable = false,
        .isItemOnly = true,
        .shape = RenderShape::None,
        .dropId = BlockId::Stick,
        .faceTiles = allFaces(130)
    });

    reg(BlockDef{
        .id = BlockId::CoalItem,
        .name = "Coal",
        .solid = false,
        .opaque = false,
        .selectable = false,
        .replaceable = false,
        .isItemOnly = true,
        .shape = RenderShape::None,
        .dropId = BlockId::CoalItem,
        .faceTiles = allFaces(131)
    });

    reg(BlockDef{
        .id = BlockId::WoodPickaxe,
        .name = "Wooden Pickaxe",
        .solid = false,
        .opaque = false,
        .selectable = false,
        .replaceable = false,
        .isItemOnly = true,
        .toolMultiplier = 2.5f,
        .shape = RenderShape::None,
        .dropId = BlockId::WoodPickaxe,
        .faceTiles = allFaces(132)
    });

    reg(BlockDef{
        .id = BlockId::StonePickaxe,
        .name = "Stone Pickaxe",
        .solid = false,
        .opaque = false,
        .selectable = false,
        .replaceable = false,
        .isItemOnly = true,
        .toolMultiplier = 4.0f,
        .shape = RenderShape::None,
        .dropId = BlockId::StonePickaxe,
        .faceTiles = allFaces(133)
    });

    reg(BlockDef{
        .id = BlockId::IronPickaxe,
        .name = "Iron Pickaxe",
        .solid = false,
        .opaque = false,
        .selectable = false,
        .replaceable = false,
        .isItemOnly = true,
        .toolMultiplier = 6.0f,
        .shape = RenderShape::None,
        .dropId = BlockId::IronPickaxe,
        .faceTiles = allFaces(134)
    });

    reg(BlockDef{
        .id = BlockId::DiamondPickaxe,
        .name = "Diamond Pickaxe",
        .solid = false,
        .opaque = false,
        .selectable = false,
        .replaceable = false,
        .isItemOnly = true,
        .toolMultiplier = 9.0f,
        .shape = RenderShape::None,
        .dropId = BlockId::DiamondPickaxe,
        .faceTiles = allFaces(135)
    });

    // S6.5 blast resistance: Beta setHardness makes it the hardness, setResistance raises it.
    for (BlockDef& def : s_defs) {
        def.blastResistance = def.hardness > 0.0f ? def.hardness : 0.0f;
    }
    const auto resist = [](BlockId id, float value) { s_defs[static_cast<size_t>(id)].blastResistance = value; };
    for (const BlockId id : {BlockId::Stone, BlockId::Cobblestone, BlockId::Bricks, BlockId::MossyCobble,
                             BlockId::CobbleSlab, BlockId::CobbleStairs}) {
        resist(id, 6.0f); // setResistance(10): 30 / 5; stairs follow their model block
    }
    for (const BlockId id : {BlockId::OakPlanks, BlockId::WoodStairs, BlockId::Fence}) {
        resist(id, 3.0f); // setResistance(5)
    }
    resist(BlockId::Obsidian, 1200.0f);   // setResistance(2000)
    resist(BlockId::Bedrock, 3.6e6f);     // setResistance(6000000)
    for (const BlockId id : {BlockId::Water, BlockId::StillWater, BlockId::Lava, BlockId::StillLava}) {
        resist(id, 100.0f);               // setHardness(100)
    }

    s_initialized = true;
}

const BlockDef& BlockRegistry::get(BlockId id) noexcept {
    if (!s_initialized) {
        initialize();
    }
    return s_defs[static_cast<size_t>(id)];
}

float BlockRegistry::getPickaxeSpeedMultiplier(BlockId heldItem, BlockId targetBlock) noexcept {
    const auto& toolDef = get(heldItem);
    const auto& targetDef = get(targetBlock);
    if (toolDef.toolMultiplier > 1.0f && targetDef.sound == SoundCategory::Stone) {
        return toolDef.toolMultiplier;
    }
    return 1.0f;
}

} // namespace betacraft::voxel
