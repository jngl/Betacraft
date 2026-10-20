#pragma once

#include "core/Types.hpp"
#include "crafting/CraftingGrid.hpp"
#include "crafting/RecipeRegistry.hpp"
#include "physics/Player.hpp"
#include "ui/ScreenKind.hpp"
#include "voxel/BlockEntity.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace betacraft::world { class World; }

namespace betacraft::game {

enum class SlotButton : uint8_t {
    Left, // Pick up / put down / merge / swap the whole stack
    Right // Put down one item, or pick up half of a stack
};

// Logic of the inventory screen (roadmap P2.6), free of GL so tests drive it directly: the
// 2x2 grid of the player inventory or the 3x3 grid of an opened CraftingTable, the result
// slot previewing RecipeRegistry::match, and the stack carried by the cursor. Clicks follow
// Beta's Container.slotClick rules. A furnace or chest screen (S2.6) shows the block entity
// at containerPos() instead of a grid; its slots live in World::blockEntities().
class InventoryCrafting {
public:
    // The default book is the strict Beta 1.7.3 one (BetaRecipeOptions::vanilla()).
    InventoryCrafting();
    explicit InventoryCrafting(crafting::RecipeRegistry book);

    [[nodiscard]] const crafting::RecipeRegistry& recipes() const noexcept { return m_book; }

    // Switches to a fresh grid of the given size. Anything left in the previous grid and the
    // cursor goes back to the player first; what does not fit is returned (to drop in-world).
    [[nodiscard]] std::vector<physics::InventorySlot> open(crafting::GridSize size, physics::Player& player);
    // Empties the grid and the cursor into the player inventory (Beta returns them on close)
    // and returns whatever did not fit.
    [[nodiscard]] std::vector<physics::InventorySlot> close(physics::Player& player);

    // Opens a furnace or chest screen on the block entity at `pos` (create it first with
    // BlockEntityStore::getOrCreate). The grid and the cursor go back to the player as in
    // open(); what does not fit is returned. Any other kind opens the matching grid.
    [[nodiscard]] std::vector<physics::InventorySlot> openContainer(ui::ScreenKind kind, core::BlockPos pos,
                                                                    physics::Player& player);
    [[nodiscard]] ui::ScreenKind kind() const noexcept { return m_kind; }
    // Position of the opened furnace / chest, nullopt for the grid screens.
    [[nodiscard]] std::optional<core::BlockPos> containerPos() const noexcept { return m_containerPos; }
    // Slots of the opened container: 3 for a furnace (kFurnace*Slot), 27 for a chest, else 0.
    [[nodiscard]] size_t containerSlotCount() const noexcept;
    // Empty when no container is open, the index is out of range or the entity is gone.
    [[nodiscard]] physics::InventorySlot containerSlot(const world::World& world, size_t index) const noexcept;

    [[nodiscard]] crafting::GridSize gridSize() const noexcept { return m_grid.size(); }
    [[nodiscard]] const crafting::CraftingGrid& grid() const noexcept { return m_grid; }
    [[nodiscard]] physics::InventorySlot gridSlot(size_t index) const noexcept;
    [[nodiscard]] const physics::InventorySlot& carried() const noexcept { return m_carried; }
    // The stacks the screen holds outside the player inventory: the grid, then the cursor.
    // A save made with the screen open folds them back into the saved inventory.
    [[nodiscard]] std::vector<physics::InventorySlot> pendingStacks() const;
    // What clicking the result slot would hand out: the highest-priority matching recipe.
    [[nodiscard]] physics::InventorySlot resultPreview() const noexcept;

    // Each returns true when the click changed something.
    bool clickInventorySlot(physics::Player& player, size_t index, SlotButton button = SlotButton::Left);
    bool clickGridSlot(size_t index, SlotButton button = SlotButton::Left);
    // Same click rules on a slot of the opened container. The furnace output is take-only
    // (Beta SlotFurnace). Every change marks the entity for saving. When the block entity is
    // gone (block broken, chunk unloaded) the screen switches back to the inventory grid,
    // containerPos() becomes nullopt and false is returned: the caller closes the screen.
    bool clickContainerSlot(world::World& world, size_t index, SlotButton button = SlotButton::Left);
    // Crafts once into the cursor when the cursor is empty or holds the same stackable item
    // with room for the whole result (Beta SlotCrafting). Byproducts (empty buckets) go to the
    // player inventory; the ones that do not fit are returned through `overflow`.
    bool clickResult(physics::Player& player, std::vector<physics::InventorySlot>* overflow = nullptr);

    // Click outside the panel (Beta slotClick with slot -999): the left button throws the
    // whole cursor stack, the right button one item. Returns what left the cursor (empty
    // when nothing is carried); the caller drops it in the world.
    [[nodiscard]] physics::InventorySlot takeCarried(SlotButton button);

    // S6.7 armor slot `index` (gameplay::ArmorSlot) of the player (Beta SlotArmor): it takes
    // only the matching piece, one at a time; a matching cursor piece swaps with a worn one.
    bool clickArmorSlot(physics::Player& player, size_t index, SlotButton button = SlotButton::Left);
    // Shift-click on a worn piece: back into the main inventory, then the hotbar.
    bool shiftClickArmorSlot(physics::Player& player, size_t index);

    // Shift-click (Beta transferStackInSlot, S3.5). Each moves as much of the clicked stack as
    // fits and leaves the rest in place; items are never created or lost. Each returns true
    // when something moved.
    // Player slot: into the open chest when `world` is given and a chest screen is open,
    // otherwise hotbar (0-8) <-> main inventory (9-35), also with a furnace open. On the
    // player's inventory screen an armor piece goes straight into its empty armor slot (S6.7).
    bool shiftClickInventorySlot(physics::Player& player, world::World* world, size_t index);
    // Grid slot: into the main inventory, then the hotbar.
    bool shiftClickGridSlot(physics::Player& player, size_t index);
    // Container slot (chest, furnace input / fuel / output): into the main inventory, then the
    // hotbar. Marks the entity for saving; a vanished entity behaves like clickContainerSlot.
    bool shiftClickContainerSlot(physics::Player& player, world::World& world, size_t index);
    // Crafts repeatedly straight into the player inventory while the recipe matches and the
    // whole result fits (main inventory, then hotbar). Byproducts that do not fit are returned
    // through `overflow`.
    bool shiftClickResult(physics::Player& player, std::vector<physics::InventorySlot>* overflow = nullptr);

private:
    // `takeOnly`: the slot accepts nothing from the cursor (Beta Slot.isItemValid false).
    bool clickStack(physics::InventorySlot& slot, SlotButton button, bool takeOnly = false);
    void forgetContainer() noexcept;
    // The open container's entity, or null (and the screen forgets it) when it is gone.
    [[nodiscard]] voxel::BlockEntity* liveContainer(world::World& world) noexcept;
    [[nodiscard]] std::vector<physics::InventorySlot> returnContents(physics::Player& player);

    crafting::RecipeRegistry m_book;
    crafting::CraftingGrid m_grid{crafting::GridSize::Inventory2x2};
    physics::InventorySlot m_carried{};
    ui::ScreenKind m_kind{ui::ScreenKind::Inventory2x2};
    std::optional<core::BlockPos> m_containerPos;
};

} // namespace betacraft::game
