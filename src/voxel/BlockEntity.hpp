#pragma once

#include <array>
#include <cstdint>
#include <variant>

namespace betacraft::voxel {

// Block entity payloads (Beta TileEntity): plain data, independent of the crafting rules
// that operate on them (crafting/Furnace.hpp) so the world and the save code stay below
// crafting in the dependency graph. Ids are in the engine's item scheme (gameplay::ItemId).
struct StoredStack {
    uint16_t id{0};
    uint8_t count{0};
    uint16_t damage{0};

    [[nodiscard]] constexpr bool empty() const noexcept { return id == 0 || count == 0; }
    constexpr bool operator==(const StoredStack&) const noexcept = default;
};

// Beta TileEntityFurnace: slots 0 (input), 1 (fuel), 2 (output) and its three timers.
struct FurnaceData {
    StoredStack input{};
    StoredStack fuel{};
    StoredStack output{};
    int16_t burnTicks{0};      // Ticks the current fuel item still burns (furnaceBurnTime)
    int16_t burnTicksTotal{0}; // Burn time of that item when lit (currentItemBurnTime)
    int16_t cookTicks{0};      // Progress of the item being cooked, 0..kSmeltTicks

    constexpr bool operator==(const FurnaceData&) const noexcept = default;
};

inline constexpr size_t kChestSlots = 27;

// Beta TileEntityChest (single chest).
struct ChestData {
    std::array<StoredStack, kChestSlots> slots{};

    constexpr bool operator==(const ChestData&) const noexcept = default;
};

using BlockEntity = std::variant<FurnaceData, ChestData>;

} // namespace betacraft::voxel
