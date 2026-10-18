#pragma once

#include "gameplay/ItemIds.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace betacraft::gameplay {

// ToolKind (gameplay/ItemIds.hpp) plus None for empty hands and non-tool items.
enum class ToolType : uint8_t {
    None = 0,
    Pickaxe,
    Axe,
    Shovel,
    Sword,
    Hoe
};

struct ToolDef {
    ItemId id{kNoItem};
    ToolType type{ToolType::None};
    ToolMaterial material{ToolMaterial::Wood};
    int32_t maxDamage{0};
    std::string_view name{};
};

[[nodiscard]] constexpr ToolType toToolType(ToolKind kind) noexcept {
    return static_cast<ToolType>(static_cast<uint8_t>(kind) + 1U);
}

[[nodiscard]] ItemId toolItemId(ToolType, ToolMaterial) noexcept;
[[nodiscard]] std::optional<ToolDef> findTool(ItemId) noexcept;
[[nodiscard]] bool isTool(ItemId) noexcept;

} // namespace betacraft::gameplay
