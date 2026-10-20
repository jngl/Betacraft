#include "crafting/Furnace.hpp"

#include <algorithm>
#include <cstdint>

namespace betacraft::crafting {

namespace {

[[nodiscard]] voxel::StoredStack toStored(const SlotStack& s) noexcept {
    return voxel::StoredStack{s.id, s.count, s.damage};
}

void takeOne(voxel::StoredStack& s) noexcept {
    if (s.count > 0) {
        --s.count;
    }
    if (s.count == 0) {
        s = {};
    }
}

} // namespace

bool canSmelt(const voxel::FurnaceData& furnace, const SmeltingBook& book) noexcept {
    if (furnace.input.empty()) {
        return false;
    }
    const auto result = book.result(furnace.input.id);
    if (!result) {
        return false;
    }
    if (furnace.output.empty()) {
        return true;
    }
    if (canonicalize(furnace.output.id) != canonicalize(result->id) || furnace.output.damage != result->damage) {
        return false;
    }
    const int32_t limit = std::min<int32_t>(kMaxStackSize, maxStackSize(result->id));
    return static_cast<int32_t>(furnace.output.count) + result->count <= limit;
}

FurnaceTickResult tickFurnace(voxel::FurnaceData& furnace, const SmeltingBook& book) noexcept {
    FurnaceTickResult out{};
    const bool wasBurning = furnace.burnTicks > 0;
    if (furnace.burnTicks > 0) {
        --furnace.burnTicks;
        out.changed = true;
    }

    if (furnace.burnTicks == 0 && canSmelt(furnace, book)) {
        const int32_t burn = furnace.fuel.empty() ? 0 : fuelBurnTicks(furnace.fuel.id);
        if (burn > 0) {
            furnace.burnTicks = static_cast<int16_t>(std::min<int32_t>(burn, INT16_MAX));
            furnace.burnTicksTotal = furnace.burnTicks;
            takeOne(furnace.fuel); // Beta consumes the lava bucket whole, without a bucket back
            out.changed = true;
        }
    }

    if (furnace.burnTicks > 0 && canSmelt(furnace, book)) {
        ++furnace.cookTicks;
        out.changed = true;
        if (furnace.cookTicks >= kSmeltTicks) {
            furnace.cookTicks = 0;
            const SlotStack result = *book.result(furnace.input.id);
            if (furnace.output.empty()) {
                furnace.output = toStored(result);
            } else {
                furnace.output.count = static_cast<uint8_t>(furnace.output.count + result.count);
            }
            takeOne(furnace.input);
            out.smelted = true;
        }
    } else if (furnace.cookTicks != 0) {
        furnace.cookTicks = 0;
        out.changed = true;
    }

    if (wasBurning != (furnace.burnTicks > 0)) {
        out.litChanged = true;
        out.changed = true;
    }
    return out;
}

float cookProgress(const voxel::FurnaceData& furnace) noexcept {
    return std::clamp(static_cast<float>(furnace.cookTicks) / static_cast<float>(kSmeltTicks), 0.0f, 1.0f);
}

float burnProgress(const voxel::FurnaceData& furnace) noexcept {
    if (furnace.burnTicks <= 0) {
        return 0.0f;
    }
    // Beta getBurnTimeRemainingScaled falls back to 200 when the total is unknown.
    const int32_t total = furnace.burnTicksTotal > 0 ? furnace.burnTicksTotal : kSmeltTicks;
    return std::clamp(static_cast<float>(furnace.burnTicks) / static_cast<float>(total), 0.0f, 1.0f);
}

} // namespace betacraft::crafting
