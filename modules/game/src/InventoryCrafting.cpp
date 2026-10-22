#include "game/InventoryCrafting.hpp"

#include "crafting/BetaRecipes.hpp"
#include "game/ItemBridge.hpp"
#include "gameplay/Armor.hpp"
#include "gameplay/ItemIds.hpp"
#include "voxel/BlockEntityStore.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>
#include <variant>

namespace betacraft::game {

namespace {

[[nodiscard]] int32_t stackLimit(gameplay::ItemId id) noexcept {
    return std::max(1, gameplay::maxStackSize(id));
}

[[nodiscard]] bool sameItem(const physics::InventorySlot& a, const physics::InventorySlot& b) noexcept {
    return a.id == b.id && a.damage == b.damage;
}

[[nodiscard]] physics::InventorySlot fromStored(const voxel::StoredStack& s) noexcept {
    return s.empty() ? physics::InventorySlot{} : physics::InventorySlot{s.id, s.count, s.damage};
}

[[nodiscard]] voxel::StoredStack toStored(const physics::InventorySlot& s) noexcept {
    if (s.isEmpty()) return {};
    return voxel::StoredStack{s.id, static_cast<uint8_t>(std::clamp(s.count, 0, 255)),
                              static_cast<uint16_t>(std::clamp(s.damage, 0, 65535))};
}

// The stored stack behind container slot `index` of `entity` for a screen of `kind`, null
// when the entity is not that kind's payload or the index is out of range.
template <class Entity>
[[nodiscard]] auto* storedSlot(Entity& entity, ui::ScreenKind kind, size_t index) noexcept {
    using Stack = std::conditional_t<std::is_const_v<Entity>, const voxel::StoredStack, voxel::StoredStack>;
    Stack* out = nullptr;
    if (kind == ui::ScreenKind::Chest) {
        if (auto* chest = std::get_if<voxel::ChestData>(&entity); chest != nullptr && index < chest->slots.size()) {
            out = &chest->slots[index];
        }
    } else if (kind == ui::ScreenKind::Furnace) {
        if (auto* furnace = std::get_if<voxel::FurnaceData>(&entity)) {
            switch (index) {
                case ui::kFurnaceInputSlot: out = &furnace->input; break;
                case ui::kFurnaceFuelSlot: out = &furnace->fuel; break;
                case ui::kFurnaceOutputSlot: out = &furnace->output; break;
                default: break;
            }
        }
    }
    return out;
}

// Beta mergeItemStack over the player's main inventory (9-35) then hotbar (0-8): the merge
// pass runs over both ranges before any empty slot is filled. Returns what did not fit.
[[nodiscard]] int32_t insertMainThenHotbar(physics::Player& player, const physics::InventorySlot& stack) noexcept {
    constexpr size_t kHotbar = physics::Player::HOTBAR_SIZE;
    constexpr size_t kTotal = physics::Player::TOTAL_INVENTORY_SIZE;
    physics::InventorySlot left = stack;
    left.count = player.insertStackInRange(left, kHotbar, kTotal, true);
    left.count = player.insertStackInRange(left, 0, kHotbar, true);
    left.count = player.insertStackInRange(left, kHotbar, kTotal);
    left.count = player.insertStackInRange(left, 0, kHotbar);
    return left.isEmpty() ? 0 : left.count;
}

// How many items of `stack` the player inventory could take, capped at stack.count.
[[nodiscard]] int32_t playerRoomFor(const physics::Player& player, const physics::InventorySlot& stack) noexcept {
    const int32_t limit = stackLimit(stack.id);
    int32_t room = 0;
    for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE && room < stack.count; ++i) {
        const physics::InventorySlot slot = player.getInventorySlot(i);
        if (slot.isEmpty()) {
            room += limit;
        } else if (sameItem(slot, stack)) {
            room += std::max(0, limit - slot.count);
        }
    }
    return std::min(room, stack.count);
}

// Merge-then-fill of `stack` into stored container slots. Returns what did not fit.
[[nodiscard]] int32_t insertIntoStored(voxel::StoredStack* slots, size_t count, physics::InventorySlot stack) noexcept {
    const int32_t limit = stackLimit(stack.id);
    for (size_t pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < count && stack.count > 0; ++i) {
            physics::InventorySlot slot = fromStored(slots[i]);
            const bool merge = pass == 0 && !slot.isEmpty() && sameItem(slot, stack);
            const bool fill = pass == 1 && slot.isEmpty();
            if (!merge && !fill) continue;
            const int32_t moved = std::min(stack.count, limit - (fill ? 0 : slot.count));
            if (moved <= 0) continue;
            slot = physics::InventorySlot{stack.id, (fill ? 0 : slot.count) + moved, stack.damage};
            slots[i] = toStored(slot);
            stack.count -= moved;
        }
    }
    return std::max(0, stack.count);
}

} // namespace

// S2.8: the game book is the strict Beta one. Smelting now produces ingots and gems, so the
// raw-ore tool variants, the 6-slab yield and the sugar-cane bookshelf are gone.
InventoryCrafting::InventoryCrafting()
    : InventoryCrafting(crafting::makeBetaRecipeBook(crafting::BetaRecipeOptions::vanilla())) {}

InventoryCrafting::InventoryCrafting(crafting::RecipeRegistry book) : m_book(std::move(book)) {}

std::vector<physics::InventorySlot> InventoryCrafting::open(crafting::GridSize size, physics::Player& player) {
    std::vector<physics::InventorySlot> overflow = returnContents(player);
    m_grid = crafting::CraftingGrid{size};
    m_kind = size == crafting::GridSize::Workbench3x3 ? ui::ScreenKind::Workbench3x3 : ui::ScreenKind::Inventory2x2;
    m_containerPos.reset();
    return overflow;
}

std::vector<physics::InventorySlot> InventoryCrafting::openContainer(ui::ScreenKind kind, core::BlockPos pos,
                                                                     physics::Player& player) {
    if (!ui::isContainerScreen(kind)) {
        return open(kind == ui::ScreenKind::Workbench3x3 ? crafting::GridSize::Workbench3x3
                                                     : crafting::GridSize::Inventory2x2,
                    player);
    }
    std::vector<physics::InventorySlot> overflow = open(crafting::GridSize::Inventory2x2, player);
    m_kind = kind;
    m_containerPos = pos;
    return overflow;
}

std::vector<physics::InventorySlot> InventoryCrafting::close(physics::Player& player) {
    forgetContainer();
    return returnContents(player);
}

void InventoryCrafting::forgetContainer() noexcept {
    m_containerPos.reset();
    m_kind = m_grid.size() == crafting::GridSize::Workbench3x3 ? ui::ScreenKind::Workbench3x3 : ui::ScreenKind::Inventory2x2;
}

size_t InventoryCrafting::containerSlotCount() const noexcept {
    if (!m_containerPos) return 0;
    switch (m_kind) {
        case ui::ScreenKind::Furnace: return ui::kFurnaceSlotCount;
        case ui::ScreenKind::Chest: return voxel::kChestSlots;
        default: return 0;
    }
}

physics::InventorySlot InventoryCrafting::containerSlot(const world::World& world, size_t index) const noexcept {
    if (!m_containerPos) return {};
    const voxel::BlockEntity* entity = world.blockEntities().find(*m_containerPos);
    if (entity == nullptr) return {};
    const voxel::StoredStack* stored = storedSlot(*entity, m_kind, index);
    return stored != nullptr ? fromStored(*stored) : physics::InventorySlot{};
}

physics::InventorySlot InventoryCrafting::gridSlot(size_t index) const noexcept {
    return toInventorySlot(m_grid.at(index));
}

std::vector<physics::InventorySlot> InventoryCrafting::pendingStacks() const {
    std::vector<physics::InventorySlot> pending;
    for (size_t i = 0; i < m_grid.slotCount(); ++i) {
        const physics::InventorySlot slot = gridSlot(i);
        if (!slot.isEmpty()) pending.push_back(slot);
    }
    if (!m_carried.isEmpty()) pending.push_back(m_carried);
    return pending;
}

physics::InventorySlot InventoryCrafting::resultPreview() const noexcept {
    const auto match = m_book.match(m_grid);
    return match ? toInventorySlot(match->result) : physics::InventorySlot{};
}

bool InventoryCrafting::clickInventorySlot(physics::Player& player, size_t index, SlotButton button) {
    if (index >= physics::Player::TOTAL_INVENTORY_SIZE) {
        return false;
    }
    physics::InventorySlot slot = player.getInventorySlot(index);
    if (!clickStack(slot, button)) {
        return false;
    }
    player.setInventorySlot(index, slot);
    return true;
}

bool InventoryCrafting::clickGridSlot(size_t index, SlotButton button) {
    if (index >= m_grid.slotCount()) {
        return false;
    }
    physics::InventorySlot slot = gridSlot(index);
    if (!clickStack(slot, button)) {
        return false;
    }
    const int width = m_grid.width();
    (void)m_grid.set(static_cast<int>(index) % width, static_cast<int>(index) / width, toSlotStack(slot));
    return true;
}

bool InventoryCrafting::clickContainerSlot(world::World& world, size_t index, SlotButton button) {
    if (!m_containerPos || index >= containerSlotCount()) {
        return false;
    }
    const core::BlockPos pos = *m_containerPos;
    voxel::BlockEntity* entity = world.blockEntities().find(pos);
    if (entity == nullptr || !voxel::blockEntityMatches(world.getBlock(pos), *entity)) {
        forgetContainer(); // Broken or unloaded under the open screen
        return false;
    }
    voxel::StoredStack* stored = storedSlot(*entity, m_kind, index);
    if (stored == nullptr) {
        forgetContainer();
        return false;
    }
    physics::InventorySlot slot = fromStored(*stored);
    const bool takeOnly = m_kind == ui::ScreenKind::Furnace && index == ui::kFurnaceOutputSlot;
    if (!clickStack(slot, button, takeOnly)) {
        return false;
    }
    *stored = toStored(slot);
    world.markBlockEntityChanged(pos);
    return true;
}

bool InventoryCrafting::clickResult(physics::Player& player, std::vector<physics::InventorySlot>* overflow) {
    const physics::InventorySlot result = resultPreview();
    if (result.isEmpty()) {
        return false;
    }
    if (!m_carried.isEmpty() &&
        (!sameItem(m_carried, result) || m_carried.count + result.count > stackLimit(result.id))) {
        return false;
    }
    const auto outcome = m_book.craft(m_grid);
    if (!outcome) {
        return false;
    }
    m_carried = physics::InventorySlot{result.id, m_carried.count + result.count, result.damage};
    for (size_t i = 0; i < outcome->byproductCount; ++i) {
        const physics::InventorySlot byproduct = toInventorySlot(outcome->byproducts[i]);
        const int32_t left = player.insertStack(byproduct);
        if (left > 0 && overflow != nullptr) {
            overflow->push_back({byproduct.id, left, byproduct.damage});
        }
    }
    return true;
}

// Beta Container.slotClick for one slot and the cursor stack.
physics::InventorySlot InventoryCrafting::takeCarried(SlotButton button) {
    if (m_carried.isEmpty()) {
        return {};
    }
    const int32_t count = button == SlotButton::Left ? m_carried.count : 1;
    const physics::InventorySlot taken{m_carried.id, count, m_carried.damage};
    m_carried.count -= count;
    if (m_carried.count <= 0) {
        m_carried = {};
    }
    return taken;
}

voxel::BlockEntity* InventoryCrafting::liveContainer(world::World& world) noexcept {
    if (!m_containerPos) {
        return nullptr;
    }
    voxel::BlockEntity* entity = world.blockEntities().find(*m_containerPos);
    if (entity == nullptr || !voxel::blockEntityMatches(world.getBlock(*m_containerPos), *entity)) {
        forgetContainer(); // Broken or unloaded under the open screen
        return nullptr;
    }
    return entity;
}

bool InventoryCrafting::clickArmorSlot(physics::Player& player, size_t index, SlotButton button) {
    (void)button; // Armor stacks hold one item: both buttons move the whole piece
    if (index >= physics::Player::ARMOR_SLOT_COUNT) {
        return false;
    }
    physics::InventorySlot worn = player.getArmorSlot(index);
    if (m_carried.isEmpty()) {
        if (worn.isEmpty()) {
            return false;
        }
        m_carried = worn;
        player.setArmorSlot(index, {});
        return true;
    }
    const std::optional<gameplay::ArmorInfo> armor = gameplay::armorInfo(gameplay::canonicalize(m_carried.id));
    if (!armor || static_cast<size_t>(armor->slot) != index) {
        return false; // Beta SlotArmor.isItemValid
    }
    const physics::InventorySlot piece{m_carried.id, 1, m_carried.damage};
    if (!worn.isEmpty() && m_carried.count > 1) {
        return false; // Nowhere to put the worn piece
    }
    m_carried.count -= 1;
    if (m_carried.count <= 0) {
        m_carried = worn; // Swap (or empty cursor when the slot was empty)
    }
    player.setArmorSlot(index, piece);
    return true;
}

bool InventoryCrafting::shiftClickArmorSlot(physics::Player& player, size_t index) {
    if (index >= physics::Player::ARMOR_SLOT_COUNT) {
        return false;
    }
    const physics::InventorySlot worn = player.getArmorSlot(index);
    if (worn.isEmpty() || insertMainThenHotbar(player, worn) == worn.count) {
        return false;
    }
    player.setArmorSlot(index, {}); // A single item: it either fits whole or not at all
    return true;
}

// Beta ContainerPlayer / ContainerWorkbench / ContainerFurnace / ContainerChest
// .transferStackInSlot for a player slot.
bool InventoryCrafting::shiftClickInventorySlot(physics::Player& player, world::World* world, size_t index) {
    if (index >= physics::Player::TOTAL_INVENTORY_SIZE) {
        return false;
    }
    physics::InventorySlot slot = player.getInventorySlot(index);
    if (slot.isEmpty()) {
        return false;
    }
    int32_t left = slot.count;
    // S6.7: on the player's own screen, an armor piece is worn at once when its slot is free.
    if (m_kind == ui::ScreenKind::Inventory2x2) {
        if (const auto armor = gameplay::armorInfo(gameplay::canonicalize(slot.id))) {
            const auto armorSlot = static_cast<size_t>(armor->slot);
            if (player.getArmorSlot(armorSlot).isEmpty()) {
                player.setArmorSlot(armorSlot, {slot.id, 1, slot.damage});
                slot.count -= 1;
                player.setInventorySlot(index, slot.count > 0 ? slot : physics::InventorySlot{});
                return true;
            }
        }
    }
    if (m_kind == ui::ScreenKind::Chest && m_containerPos && world != nullptr) {
        voxel::BlockEntity* entity = liveContainer(*world);
        auto* chest = entity != nullptr ? std::get_if<voxel::ChestData>(entity) : nullptr;
        if (chest == nullptr) {
            return false;
        }
        left = insertIntoStored(chest->slots.data(), chest->slots.size(), slot);
        if (left != slot.count) {
            world->markBlockEntityChanged(*m_containerPos);
        }
    } else {
        // Clear the slot first so the move never lands back into it.
        player.setInventorySlot(index, {});
        left = index < physics::Player::HOTBAR_SIZE
            ? player.insertStackInRange(slot, physics::Player::HOTBAR_SIZE, physics::Player::TOTAL_INVENTORY_SIZE)
            : player.insertStackInRange(slot, 0, physics::Player::HOTBAR_SIZE);
    }
    const bool moved = left != slot.count;
    slot.count = left;
    player.setInventorySlot(index, left > 0 ? slot : physics::InventorySlot{});
    return moved;
}

bool InventoryCrafting::shiftClickGridSlot(physics::Player& player, size_t index) {
    if (index >= m_grid.slotCount()) {
        return false;
    }
    physics::InventorySlot slot = gridSlot(index);
    if (slot.isEmpty()) {
        return false;
    }
    const int32_t left = insertMainThenHotbar(player, slot);
    if (left == slot.count) {
        return false;
    }
    slot.count = left;
    const int width = m_grid.width();
    (void)m_grid.set(static_cast<int>(index) % width, static_cast<int>(index) / width,
                     toSlotStack(left > 0 ? slot : physics::InventorySlot{}));
    return true;
}

bool InventoryCrafting::shiftClickContainerSlot(physics::Player& player, world::World& world, size_t index) {
    if (!m_containerPos || index >= containerSlotCount()) {
        return false;
    }
    voxel::BlockEntity* entity = liveContainer(world);
    voxel::StoredStack* stored = entity != nullptr ? storedSlot(*entity, m_kind, index) : nullptr;
    if (stored == nullptr) {
        forgetContainer();
        return false;
    }
    physics::InventorySlot slot = fromStored(*stored);
    if (slot.isEmpty()) {
        return false;
    }
    const int32_t left = insertMainThenHotbar(player, slot);
    if (left == slot.count) {
        return false;
    }
    slot.count = left;
    *stored = toStored(left > 0 ? slot : physics::InventorySlot{});
    world.markBlockEntityChanged(*m_containerPos);
    return true;
}

// Beta crafts again for as long as the same result keeps coming out of the result slot. A
// result that would only partly fit is not crafted (Beta would delete the rest).
bool InventoryCrafting::shiftClickResult(physics::Player& player, std::vector<physics::InventorySlot>* overflow) {
    bool crafted = false;
    // Bounded: each craft consumes at least one ingredient of a grid holding at most 9x64.
    for (int guard = 0; guard < 9 * 64; ++guard) {
        const physics::InventorySlot result = resultPreview();
        if (result.isEmpty() || playerRoomFor(player, result) < result.count) {
            break;
        }
        const auto outcome = m_book.craft(m_grid);
        if (!outcome) {
            break;
        }
        (void)insertMainThenHotbar(player, result); // Fits: checked above
        for (size_t i = 0; i < outcome->byproductCount; ++i) {
            const physics::InventorySlot byproduct = toInventorySlot(outcome->byproducts[i]);
            const int32_t left = player.insertStack(byproduct);
            if (left > 0 && overflow != nullptr) {
                overflow->push_back({byproduct.id, left, byproduct.damage});
            }
        }
        crafted = true;
    }
    return crafted;
}

bool InventoryCrafting::clickStack(physics::InventorySlot& slot, SlotButton button, bool takeOnly) {
    if (takeOnly && !m_carried.isEmpty()) {
        // Beta Container.slotClick on a slot that refuses the cursor: the whole slot joins a
        // matching cursor stack when it fits; nothing is ever put down or swapped.
        if (slot.isEmpty() || !sameItem(slot, m_carried) || stackLimit(slot.id) <= 1 ||
            m_carried.count + slot.count > stackLimit(slot.id)) {
            return false;
        }
        m_carried.count += slot.count;
        slot = {};
        return true;
    }
    if (m_carried.isEmpty()) {
        if (slot.isEmpty()) {
            return false;
        }
        const int32_t take = button == SlotButton::Left ? slot.count : (slot.count + 1) / 2;
        m_carried = physics::InventorySlot{slot.id, take, slot.damage};
        slot.count -= take;
        if (slot.count <= 0) {
            slot = {};
        }
        return true;
    }

    if (slot.isEmpty()) {
        const int32_t put = button == SlotButton::Left ? m_carried.count : 1;
        slot = physics::InventorySlot{m_carried.id, put, m_carried.damage};
        m_carried.count -= put;
    } else if (sameItem(slot, m_carried) && stackLimit(slot.id) > 1) {
        const int32_t room = stackLimit(slot.id) - slot.count;
        const int32_t put = std::min(button == SlotButton::Left ? m_carried.count : 1, room);
        if (put <= 0) {
            return false;
        }
        slot.count += put;
        m_carried.count -= put;
    } else {
        std::swap(slot, m_carried);
    }
    if (m_carried.count <= 0) {
        m_carried = {};
    }
    return true;
}

std::vector<physics::InventorySlot> InventoryCrafting::returnContents(physics::Player& player) {
    std::vector<physics::InventorySlot> overflow;
    const auto give = [&](const physics::InventorySlot& stack) {
        const int32_t left = player.insertStack(stack);
        if (left > 0) {
            overflow.push_back({stack.id, left, stack.damage});
        }
    };
    for (size_t i = 0; i < m_grid.slotCount(); ++i) {
        give(gridSlot(i));
    }
    give(m_carried);
    m_grid.clear();
    m_carried = {};
    return overflow;
}

} // namespace betacraft::game
