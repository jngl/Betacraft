// Phase 2 integration, P2.3 fluids on the game side: the player can carry water and lava
// sources, Application places them with World::setBlock (which wakes the simulator, see
// test_p2_fluids_world.cpp), and fluid events become sounds at the listener.
#include "tests/test_helpers.hpp"

#include "fluids/FluidSimulator.hpp"
#include "game/FluidEffects.hpp"
#include "game/StarterKit.hpp"
#include "physics/Player.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace betacraft;
using fluids::FluidEvent;
using fluids::FluidEventKind;
using world::BlockId;

TEST(P2FluidsInput, StarterInventoryCarriesPlaceableWaterAndLavaSources) {
    world::BlockRegistry::initialize();
    // Only the Creative kit carries fluid sources (S1.2); Survival starts empty-handed.
    physics::Player survival;
    game::applyStarterKit(survival, core::GameMode::Survival);
    EXPECT_EQ(survival.countItem(BlockId::Water), 0);
    EXPECT_EQ(survival.countItem(BlockId::Lava), 0);
    physics::Player player;
    game::applyStarterKit(player, core::GameMode::Creative);
    EXPECT_GT(player.countItem(BlockId::Water), 0);
    EXPECT_GT(player.countItem(BlockId::Lava), 0);
    // handleRightClickAction refuses item-only entries and needs a replaceable target.
    EXPECT_FALSE(world::BlockRegistry::get(BlockId::Water).isItemOnly);
    EXPECT_FALSE(world::BlockRegistry::get(BlockId::Lava).isItemOnly);
    EXPECT_TRUE(world::BlockRegistry::get(BlockId::Water).replaceable) << "placing a block into water removes it";
}

TEST(P2FluidsGame, NoFizzEventsMeansSilence) {
    const glm::vec3 listener{0.5f, 64.5f, 0.5f};
    EXPECT_EQ(game::loudestFizz({}, listener), 0.0f);
    // A washed-away block would spawn an item entity, not a sound.
    const std::vector<FluidEvent> washed{{FluidEventKind::BlockWashedAway, {0, 64, 0}, BlockId::Dandelion}};
    EXPECT_EQ(game::loudestFizz(washed, listener), 0.0f);
}

TEST(P2FluidsGame, FizzFadesLinearlyOverSixteenBlocks) {
    const glm::vec3 listener{0.5f, 64.5f, 0.5f};
    const auto at = [](int32_t x) { return std::vector<FluidEvent>{{FluidEventKind::Fizz, {x, 64, 0}, BlockId::Obsidian}}; };
    EXPECT_FLOAT_EQ(game::loudestFizz(at(0), listener), 1.0f);
    EXPECT_NEAR(game::loudestFizz(at(8), listener), 0.5f, 1e-5f);
    EXPECT_EQ(game::loudestFizz(at(16), listener), 0.0f);
    EXPECT_EQ(game::loudestFizz(at(-400), listener), 0.0f);
}

TEST(P2FluidsGame, ManyFizzesInOneTickPlayTheNearestOnce) {
    // A freshly loaded cave can harden hundreds of lava cells in one tick.
    const glm::vec3 listener{0.5f, 64.5f, 0.5f};
    std::vector<FluidEvent> events;
    for (int32_t x = 4; x < 300; ++x) events.push_back({FluidEventKind::Fizz, {x, 64, 0}, BlockId::Cobblestone});
    EXPECT_NEAR(game::loudestFizz(events, listener), 0.75f, 1e-5f);
}
