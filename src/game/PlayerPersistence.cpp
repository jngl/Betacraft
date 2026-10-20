#include "game/PlayerPersistence.hpp"
#include "core/Logger.hpp"
#include "gameplay/Armor.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::game {

namespace {

// Canonical ids the engine can hold: defined blocks below 256 (the legacy item ids are
// folded before this check), Beta 1.7.3 item numbers 256..359 and the two records.
[[nodiscard]] bool isKnownItemId(gameplay::ItemId id) noexcept {
    if (id == gameplay::kNoItem || gameplay::isLegacyItemId(id)) return false;
    if (id < 256) {
        const auto block = static_cast<voxel::BlockId>(id);
        return voxel::BlockRegistry::get(block).id == block;
    }
    return id <= 359 || id == 2256 || id == 2257;
}

} // namespace

storage::PlayerSaveData capturePlayer(const physics::Player& player, std::span<const physics::InventorySlot> pending) {
    return capturePlayer(player, pending, nullptr);
}

storage::PlayerSaveData capturePlayer(const physics::Player& player, std::span<const physics::InventorySlot> pending,
                                      std::vector<physics::InventorySlot>* overflow) {
    physics::Player copy = player;
    for (const physics::InventorySlot& stack : pending) {
        if (stack.isEmpty()) continue;
        if (const int32_t lost = copy.insertStack(stack); lost > 0) {
            if (overflow != nullptr) {
                overflow->push_back({stack.id, lost, stack.damage});
            } else {
                core::Logger::warn("PlayerPersistence", "Inventory full: ", lost, " pending item(s) not saved");
            }
        }
    }

    storage::PlayerSaveData data{};
    data.mode = copy.gameMode();
    data.health = copy.health().current;
    data.air = copy.breath().air;
    data.selectedSlot = copy.getSelectedSlot();
    data.fallDistance = copy.getFallDistance();
    data.fireTicks = copy.burning().fireTicks;
    for (size_t i = 0; i < data.inventory.size() && i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
        const physics::InventorySlot slot = copy.getInventorySlot(i);
        if (!slot.isEmpty()) {
            data.inventory[i] = storage::SavedStack{slot.id, slot.count, slot.damage};
        }
    }
    for (size_t i = 0; i < data.armor.size() && i < physics::Player::ARMOR_SLOT_COUNT; ++i) {
        const physics::InventorySlot slot = copy.getArmorSlot(i);
        if (!slot.isEmpty()) {
            data.armor[i] = storage::SavedStack{slot.id, slot.count, slot.damage};
        }
    }
    return data;
}

void restorePlayer(physics::Player& player, const storage::PlayerSaveData& data, uint32_t itemsScheme) {
    player.setGameMode(data.mode);
    player.setHealth(std::clamp(data.health, 0, gameplay::kPlayerMaxHealth));
    player.breath().air = std::clamp(data.air, 0, gameplay::kMaxAirTicks);
    player.setSelectedSlot(std::clamp(data.selectedSlot, 0, static_cast<int32_t>(physics::Player::HOTBAR_SIZE) - 1));
    player.setFallDistance(std::isfinite(data.fallDistance) ? data.fallDistance : 0.0f);
    player.burning().fireTicks = std::clamp(data.fireTicks, 0, gameplay::kLavaFireTicks);

    for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE && i < data.inventory.size(); ++i) {
        const storage::SavedStack& saved = data.inventory[i];
        if (saved.id == gameplay::kNoItem || saved.count <= 0) {
            player.setInventorySlot(i, {});
            continue;
        }
        if (gameplay::isLegacyItemId(saved.id) && itemsScheme >= storage::kCurrentItemsScheme) {
            core::Logger::warn("PlayerPersistence", "Legacy item id ", saved.id, " in an items_scheme ", itemsScheme,
                               " save (slot ", i, "): folded");
        }
        const gameplay::ItemId id = gameplay::canonicalize(saved.id);
        if (!isKnownItemId(id)) {
            core::Logger::warn("PlayerPersistence", "Unknown item id ", saved.id, " in slot ", i, ": slot emptied");
            player.setInventorySlot(i, {});
            continue;
        }
        const int32_t count = std::clamp(saved.count, 1, std::max(1, gameplay::maxStackSize(id)));
        player.setInventorySlot(i, physics::InventorySlot{id, count, std::max(0, saved.damage)});
    }

    // S6.8: an armor slot holds one piece of its own type; anything else empties it.
    for (size_t i = 0; i < physics::Player::ARMOR_SLOT_COUNT && i < data.armor.size(); ++i) {
        const storage::SavedStack& saved = data.armor[i];
        player.setArmorSlot(i, {});
        if (saved.id == gameplay::kNoItem || saved.count <= 0) {
            continue;
        }
        const gameplay::ItemId id = gameplay::canonicalize(saved.id);
        const std::optional<gameplay::ArmorInfo> armor = gameplay::armorInfo(id);
        if (!armor || static_cast<size_t>(armor->slot) != i) {
            core::Logger::warn("PlayerPersistence", "Item id ", saved.id, " cannot be worn in armor slot ", i,
                               ": slot emptied");
            continue;
        }
        player.setArmorSlot(i, physics::InventorySlot{id, 1, std::clamp(saved.damage, 0, armor->maxDamage)});
    }
}

} // namespace betacraft::game
