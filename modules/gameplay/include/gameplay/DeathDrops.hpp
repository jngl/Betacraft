#pragma once

#include "gameplay/ItemStack.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace betacraft::gameplay {

// Beta EntityItem.delayBeforeCanPickup for items thrown by the player: 2 s at 20 TPS.
inline constexpr int32_t kDeathDropPickupDelayTicks = 40;
// Beta dropPlayerItemWithRandomChoice(stack, true), converted from blocks/tick to m/s.
inline constexpr float kDeathDropMaxHorizontalSpeed = 0.5f * 20.0f;
inline constexpr float kDeathDropVerticalSpeed = 0.2f * 20.0f;
inline constexpr float kDeathDropEyeOffset = 0.3f; // Spawned 0.3 m below the eyes

// Spawn request for a floating item entity (the ECS ItemDropComponent will consume it).
struct DroppedItem {
    ItemStack stack{};
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f}; // m/s
    int32_t pickupDelayTicks{kDeathDropPickupDelayTicks};
};

// Empties `inventory` into drop requests scattered in random horizontal directions. Uses its
// own SplitMix64 stream (not <random> distributions) so the result is identical on every
// standard library for a given seed.
[[nodiscard]] std::vector<DroppedItem> scatterInventoryOnDeath(std::span<ItemStack> inventory,
                                                               const glm::vec3& eyePosition,
                                                               uint64_t seed);

} // namespace betacraft::gameplay
