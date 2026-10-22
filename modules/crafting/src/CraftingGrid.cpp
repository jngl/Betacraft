#include "crafting/CraftingGrid.hpp"

#include <algorithm>

namespace betacraft::crafting {

namespace {
// GridSize is a uint8_t enum, so a cast from container/UI data can carry any
// value; clamping keeps every index inside the fixed 9-slot backing store.
[[nodiscard]] constexpr int clampDim(GridSize size) noexcept {
    return std::clamp(static_cast<int>(size), static_cast<int>(GridSize::Inventory2x2),
                      static_cast<int>(GridSize::Workbench3x3));
}
} // namespace

CraftingGrid::CraftingGrid(GridSize size) noexcept : m_dim(clampDim(size)) {}

SlotStack CraftingGrid::get(int x, int y) const noexcept {
    if (!inBounds(x, y)) {
        return {};
    }
    return m_slots[static_cast<size_t>(y * m_dim + x)];
}

SlotStack CraftingGrid::at(size_t index) const noexcept {
    return index < slotCount() ? m_slots[index] : SlotStack{};
}

bool CraftingGrid::set(int x, int y, SlotStack stack) noexcept {
    if (!inBounds(x, y)) {
        return false;
    }
    // Canonical empty representation keeps equality comparisons and the
    // recipe matcher free of "Air with count 7" corner cases.
    if (stack.empty()) {
        stack = SlotStack{};
    } else {
        // Per-item cap (1 for tools/buckets/cake): durability and container
        // remainders are applied per slot, which is only sound for one item.
        stack.count = std::min(stack.count, maxStackSize(stack.id));
    }
    m_slots[static_cast<size_t>(y * m_dim + x)] = stack;
    return true;
}

void CraftingGrid::clear() noexcept {
    m_slots.fill(SlotStack{});
}

bool CraftingGrid::isEmpty() const noexcept {
    return occupiedCount() == 0;
}

size_t CraftingGrid::occupiedCount() const noexcept {
    size_t n = 0;
    for (size_t i = 0; i < slotCount(); ++i) {
        if (!m_slots[i].empty()) {
            ++n;
        }
    }
    return n;
}

std::optional<GridBounds> CraftingGrid::bounds() const noexcept {
    GridBounds box{m_dim, m_dim, -1, -1};
    for (int y = 0; y < m_dim; ++y) {
        for (int x = 0; x < m_dim; ++x) {
            if (get(x, y).empty()) {
                continue;
            }
            box.minX = std::min(box.minX, x);
            box.minY = std::min(box.minY, y);
            box.maxX = std::max(box.maxX, x);
            box.maxY = std::max(box.maxY, y);
        }
    }
    if (box.maxX < 0) {
        return std::nullopt;
    }
    return box;
}

} // namespace betacraft::crafting
