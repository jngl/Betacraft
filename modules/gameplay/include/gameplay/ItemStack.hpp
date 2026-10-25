#pragma once

#include "gameplay/ToolDefinitions.hpp"

#include <cstdint>
#include <optional>

namespace betacraft::gameplay {

enum class WearResult : uint8_t {
    Unchanged, // Not damageable, empty, or a non-positive amount
    Worn,      // Damage increased, item still usable
    Broken     // Damage exceeded maxDamage: one item of the stack was destroyed
};

// Replacement for physics::InventorySlot that can carry tool wear.
// maxDamage is cached in the stack so HUD/serialisation do not need a registry lookup.
struct ItemStack {
    ItemId id{kNoItem};
    int32_t count{0};
    int32_t damage{0};
    int32_t maxDamage{0}; // 0 = not damageable (blocks, sticks, coal...)

    [[nodiscard]] constexpr bool empty() const noexcept { return id == kNoItem || count <= 0; }
    [[nodiscard]] constexpr bool isDamageable() const noexcept { return !empty() && maxDamage > 0; }

    // Number of 1-point uses left before the item breaks (Beta: breaks when damage > max).
    [[nodiscard]] constexpr int32_t remainingUses() const noexcept {
        return isDamageable() ? maxDamage + 1 - damage : 0;
    }

    // 1.0 = pristine, 0.0 = next use breaks it. Non-damageable items report 1.0 so a HUD
    // durability bar can be driven unconditionally.
    [[nodiscard]] float durabilityFraction() const noexcept;

    // Beta 1.7.3 ItemStack.damageItem semantics.
    WearResult applyWear(int32_t amount) noexcept;

    constexpr bool operator==(const ItemStack&) const noexcept = default;
};

// maxStackSize(ItemId) lives with the id scheme in gameplay/ItemIds.hpp.

// Tool definition of the held stack, nullopt for empty hands and non-tool items.
[[nodiscard]] std::optional<ToolDef> toolOf(const ItemStack& stack) noexcept;

// Beta ItemFlintAndSteel: 64 uses. It is no tool (digging and hitting leave it alone)
// but it carries its wear like one.
inline constexpr int32_t kFlintAndSteelMaxDamage = 64;

// Builds a normalised stack: count clamped to [0, maxStackSize], maxDamage filled from the
// tool table (armor and flint and steel too), and any empty result collapsed to ItemStack{}.
[[nodiscard]] ItemStack makeStack(ItemId id, int32_t count) noexcept;

// Durability cost per action, straight from Beta's ItemTool / ItemSword / ItemHoe overrides.
[[nodiscard]] constexpr int32_t wearForBlockBreak(ToolType type) noexcept {
    switch (type) {
        case ToolType::Pickaxe:
        case ToolType::Axe:
        case ToolType::Shovel:  return 1;
        case ToolType::Sword:   return 2; // Swords are not digging tools
        case ToolType::Hoe:
        case ToolType::None:    return 0;
    }
    return 0;
}

[[nodiscard]] constexpr int32_t wearForEntityHit(ToolType type) noexcept {
    switch (type) {
        case ToolType::Sword:   return 1;
        case ToolType::Pickaxe:
        case ToolType::Axe:
        case ToolType::Shovel:  return 2; // Digging tools abused as weapons wear twice as fast
        case ToolType::Hoe:
        case ToolType::None:    return 0;
    }
    return 0;
}

// Convenience wrappers applying the rules above to the held stack.
// Note: Beta wears tools on *every* broken block, even instant-break ones (flowers, torches).
WearResult onBlockBroken(ItemStack& held) noexcept;
WearResult onEntityHit(ItemStack& held) noexcept;
WearResult onSoilTilled(ItemStack& held) noexcept;

} // namespace betacraft::gameplay
