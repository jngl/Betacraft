#include "game/StarterKit.hpp"

namespace betacraft::game {

void applyStarterKit(physics::Player& player, core::GameMode mode) {
    if (mode != core::GameMode::Creative) {
        return; // Beta Survival starts empty-handed
    }
    // Hotbar
    player.setInventorySlot(0, {gameplay::items::StonePickaxe, 1});
    player.setInventorySlot(1, {voxel::BlockId::Cobblestone, 64});
    player.setInventorySlot(2, {voxel::BlockId::OakPlanks, 64});
    player.setInventorySlot(3, {voxel::BlockId::Torch, 64});
    player.setInventorySlot(4, {voxel::BlockId::CraftingTable, 4});
    player.setInventorySlot(5, {voxel::BlockId::Piston, 16});
    player.setInventorySlot(6, {voxel::BlockId::Glass, 64});
    player.setInventorySlot(7, {voxel::BlockId::Bricks, 64});
    player.setInventorySlot(8, {voxel::BlockId::Glowstone, 32});

    // Main inventory (E to view, swap, craft)
    player.setInventorySlot(9, {voxel::BlockId::OakLog, 32});
    player.setInventorySlot(10, {voxel::BlockId::Bookshelf, 32});
    player.setInventorySlot(11, {voxel::BlockId::TNT, 32});
    player.setInventorySlot(12, {voxel::BlockId::Furnace, 8});
    player.setInventorySlot(13, {voxel::BlockId::CobbleSlab, 64});
    player.setInventorySlot(14, {voxel::BlockId::MossyCobble, 32});
    player.setInventorySlot(15, {voxel::BlockId::Sandstone, 64});
    player.setInventorySlot(16, {voxel::BlockId::Wool, 64});
    player.setInventorySlot(17, {voxel::BlockId::Obsidian, 32});
    player.setInventorySlot(18, {voxel::BlockId::Dandelion, 16});
    player.setInventorySlot(19, {voxel::BlockId::Rose, 16});
    player.setInventorySlot(20, {voxel::BlockId::Sapling, 16});
    player.setInventorySlot(21, {gameplay::items::DiamondPickaxe, 1});
    // Fluid sources stand in for buckets: bucket items (325..327) have no behaviour yet.
    player.setInventorySlot(22, {voxel::BlockId::Water, 16});
    player.setInventorySlot(23, {voxel::BlockId::Lava, 16});
}

} // namespace betacraft::game
