#pragma once

#include "crafting/ItemIds.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace betacraft::crafting {

// Minimal stack view used by the grid. Kept local to the module so it does not
// depend on the ItemStack; integration converts field by field.
struct SlotStack {
    ItemId id{kAir};
    uint8_t count{0};
    uint16_t damage{0};

    [[nodiscard]] constexpr bool empty() const noexcept { return id == kAir || count == 0; }
    constexpr bool operator==(const SlotStack&) const noexcept = default;
};

// The enumerator value is the side length.
enum class GridSize : uint8_t {
    Inventory2x2 = 2,
    Workbench3x3 = 3
};

// Inclusive bounding box of the occupied slots.
struct GridBounds {
    int minX{0};
    int minY{0};
    int maxX{0};
    int maxY{0};

    [[nodiscard]] constexpr int width() const noexcept { return maxX - minX + 1; }
    [[nodiscard]] constexpr int height() const noexcept { return maxY - minY + 1; }
};

// Square crafting matrix: 2x2 in the player inventory, 3x3 on a CraftingTable.
// Storage is always 3x3 so grids are trivially copyable and allocation-free.
class CraftingGrid {
public:
    static constexpr int kMaxDim = 3;

    // Values other than 2/3 are clamped into [2, 3].
    explicit CraftingGrid(GridSize size) noexcept;

    [[nodiscard]] int width() const noexcept { return m_dim; }
    [[nodiscard]] int height() const noexcept { return m_dim; }
    [[nodiscard]] size_t slotCount() const noexcept { return static_cast<size_t>(m_dim * m_dim); }
    [[nodiscard]] GridSize size() const noexcept { return static_cast<GridSize>(m_dim); }

    // Out-of-range reads return an empty stack; out-of-range writes return false.
    [[nodiscard]] SlotStack get(int x, int y) const noexcept;
    [[nodiscard]] SlotStack at(size_t index) const noexcept;
    // Air or count 0 is stored as the canonical empty stack; count is clamped to
    // maxStackSize(id) (64, or 1 for tools, buckets and cake).
    bool set(int x, int y, SlotStack stack) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] size_t occupiedCount() const noexcept;
    [[nodiscard]] std::optional<GridBounds> bounds() const noexcept;

private:
    [[nodiscard]] bool inBounds(int x, int y) const noexcept {
        return x >= 0 && y >= 0 && x < m_dim && y < m_dim;
    }

    int m_dim{0};
    std::array<SlotStack, kMaxDim * kMaxDim> m_slots{};
};

} // namespace betacraft::crafting
