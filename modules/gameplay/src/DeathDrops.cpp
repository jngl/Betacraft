#include "gameplay/DeathDrops.hpp"

#include "gameplay/SplitMix64.hpp"

#include <cmath>
#include <numbers>

namespace betacraft::gameplay {


std::vector<DroppedItem> scatterInventoryOnDeath(std::span<ItemStack> inventory,
                                                 const glm::vec3& eyePosition, uint64_t seed) {
    std::vector<DroppedItem> drops;
    SplitMix64 rng(seed);
    const glm::vec3 spawn = eyePosition - glm::vec3{0.0f, kDeathDropEyeOffset, 0.0f};
    for (ItemStack& slot : inventory) {
        if (slot.empty()) {
            slot = ItemStack{};
            continue;
        }
        const float speed = rng.nextFloat() * kDeathDropMaxHorizontalSpeed;
        const float angle = rng.nextFloat() * 2.0f * std::numbers::pi_v<float>;
        drops.push_back(DroppedItem{
            slot, spawn,
            glm::vec3{-std::sin(angle) * speed, kDeathDropVerticalSpeed, std::cos(angle) * speed},
            kDeathDropPickupDelayTicks});
        slot = ItemStack{};
    }
    return drops;
}

} // namespace betacraft::gameplay
