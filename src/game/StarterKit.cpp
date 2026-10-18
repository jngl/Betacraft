#include "game/StarterKit.hpp"

namespace betacraft::game {

void applyStarterKit(physics::Player& player, core::GameMode mode) {
    if (mode != core::GameMode::Creative) {
        return; // Beta Survival starts empty-handed
    }
    // Hotbar
    player.setInventorySlot(0, {gameplay::items::StonePickaxe, 1});
    player.setInventorySlot(1, {world::BlockId::Cobblestone, 64});
    player.setInventorySlot(2, {world::BlockId::OakPlanks, 64});
    player.setInventorySlot(3, {world::BlockId::Torch, 64});
    player.setInventorySlot(4, {world::BlockId::CraftingTable, 4});
    player.setInventorySlot(5, {world::BlockId::Piston, 16});
    player.setInventorySlot(6, {world::BlockId::Glass, 64});
    player.setInventorySlot(7, {world::BlockId::Bricks, 64});
    player.setInventorySlot(8, {world::BlockId::Glowstone, 32});

    // Main inventory (E to view, swap, craft)
    player.setInventorySlot(9, {world::BlockId::OakLog, 32});
    player.setInventorySlot(10, {world::BlockId::Bookshelf, 32});
    player.setInventorySlot(11, {world::BlockId::TNT, 32});
    player.setInventorySlot(12, {world::BlockId::Furnace, 8});
    player.setInventorySlot(13, {world::BlockId::CobbleSlab, 64});
    player.setInventorySlot(14, {world::BlockId::MossyCobble, 32});
    player.setInventorySlot(15, {world::BlockId::Sandstone, 64});
    player.setInventorySlot(16, {world::BlockId::Wool, 64});
    player.setInventorySlot(17, {world::BlockId::Obsidian, 32});
    player.setInventorySlot(18, {world::BlockId::Dandelion, 16});
    player.setInventorySlot(19, {world::BlockId::Rose, 16});
    player.setInventorySlot(20, {world::BlockId::Sapling, 16});
    player.setInventorySlot(21, {gameplay::items::DiamondPickaxe, 1});
    // Fluid sources stand in for buckets: bucket items (325..327) have no behaviour yet.
    player.setInventorySlot(22, {world::BlockId::Water, 16});
    player.setInventorySlot(23, {world::BlockId::Lava, 16});
}

} // namespace betacraft::game
