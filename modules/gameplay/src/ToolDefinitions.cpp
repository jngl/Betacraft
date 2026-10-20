#include "gameplay/ToolDefinitions.hpp"

#include <array>
#include <cstddef>

namespace betacraft::gameplay {

namespace {

constexpr size_t kTypeCount = 5;     // Pickaxe..Hoe (None excluded)
constexpr size_t kMaterialCount = 5; // Wood..Diamond

constexpr std::array<std::array<std::string_view, kMaterialCount>, kTypeCount> kToolNames{{
    {"Wooden Pickaxe", "Stone Pickaxe", "Iron Pickaxe", "Golden Pickaxe", "Diamond Pickaxe"},
    {"Wooden Axe", "Stone Axe", "Iron Axe", "Golden Axe", "Diamond Axe"},
    {"Wooden Shovel", "Stone Shovel", "Iron Shovel", "Golden Shovel", "Diamond Shovel"},
    {"Wooden Sword", "Stone Sword", "Iron Sword", "Golden Sword", "Diamond Sword"},
    {"Wooden Hoe", "Stone Hoe", "Iron Hoe", "Golden Hoe", "Diamond Hoe"},
}};

} // namespace

ItemId toolItemId(ToolType type, ToolMaterial material) noexcept {
    if (type == ToolType::None) {
        return kNoItem;
    }
    return toolItemId(static_cast<ToolKind>(static_cast<uint8_t>(type) - 1U), material);
}

std::optional<ToolDef> findTool(ItemId id) noexcept {
    const auto info = toolInfo(id);
    if (!info) {
        return std::nullopt;
    }
    const auto k = static_cast<size_t>(info->kind);
    const auto m = static_cast<size_t>(info->material);
    return ToolDef{toolItemId(info->kind, info->material), toToolType(info->kind), info->material,
                   materialStats(info->material).maxUses, kToolNames[k][m]};
}

bool isTool(ItemId id) noexcept {
    return toolInfo(id).has_value();
}

} // namespace betacraft::gameplay
