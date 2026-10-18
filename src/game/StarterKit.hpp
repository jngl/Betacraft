#pragma once

#include "core/Config.hpp"
#include "physics/Player.hpp"

namespace betacraft::game {

// Fills a new world's inventory. Creative gets the historical engine kit (blocks, tools and
// the fluid sources that stand in for buckets); Survival gets nothing, as in Beta.
void applyStarterKit(physics::Player& player, core::GameMode mode);

} // namespace betacraft::game
