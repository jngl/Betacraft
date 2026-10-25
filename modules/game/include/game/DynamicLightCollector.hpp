#pragma once

#include "ecs/Registry.hpp"
#include "lighting/DynamicLightManager.hpp"
#include "physics/Player.hpp"

#include <optional>

namespace betacraft::game {

// Game-state side of the dynamic lights, run once per rendered frame before
// WorldRenderer::renderWorld: replaces the manager's lights with the item in the player's
// selected hotbar slot and every glowing dropped item in `entities`. Entity positions are
// interpolated with `entityAlpha`, the alpha of the clock they tick on (the 20 TPS game tick in
// Application); without it, the player's render alpha.
void collectDynamicLights(lighting::DynamicLightManager& manager, const physics::Player& player,
                          ecs::Registry* entities = nullptr, std::optional<float> entityAlpha = std::nullopt);

} // namespace betacraft::game
