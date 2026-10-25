// On the game side: which dynamic lights a frame submits for a given
// player / hotbar state and set of dropped item entities (game::collectDynamicLights, called by
// Application once per rendered frame before WorldRenderer::renderWorld).
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"
#include "game/DynamicLightCollector.hpp"
#include "lighting/DynamicLightManager.hpp"
#include "physics/Player.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>

using namespace betacraft;
using lighting::DynamicLightManager;
using lighting::LightSource;
using voxel::BlockId;

namespace {

class DynLightCollect : public ::testing::Test {
protected:
    void SetUp() override {
        voxel::BlockRegistry::initialize();
        player.setPosition({4.5f, 70.0f, -3.5f});
        player.resetInterpolationHistory();
        player.setRotation(-90.0f, 0.0f);
        for (size_t i = 0; i < physics::Player::HOTBAR_SIZE; ++i) {
            player.setInventorySlot(i, {});
        }
    }

    void hold(BlockId id, int32_t count = 1) {
        player.setInventorySlot(4, {id, count});
        player.setSelectedSlot(4);
    }

    physics::Player player;
    DynamicLightManager manager;
};

} // namespace

TEST_F(DynLightCollect, HeldTorchInTheSelectedSlotLightsTheHand) {
    hold(BlockId::Torch, 64);
    game::collectDynamicLights(manager, player);

    ASSERT_EQ(manager.size(), 1u);
    const lighting::DynamicLight& l = manager.lights()[0];
    EXPECT_EQ(l.source, LightSource::HeldItem);
    EXPECT_FLOAT_EQ(l.radius, 14.0f);
    // At the hand of the camera the frame is rendered from (interpolated, bobbing eyes).
    EXPECT_EQ(l.position, DynamicLightManager::heldLightPosition(player.getEyePosition(), player.getLookDirection()));
}

TEST_F(DynLightCollect, HeldGlowstoneLightsAtFullLevel) {
    hold(BlockId::Glowstone, 32);
    game::collectDynamicLights(manager, player);
    ASSERT_EQ(manager.size(), 1u);
    EXPECT_FLOAT_EQ(manager.lights()[0].radius, 15.0f);
}

TEST_F(DynLightCollect, OnlyTheSelectedHotbarSlotCounts) {
    player.setInventorySlot(0, {BlockId::Torch, 64});
    player.setInventorySlot(1, {BlockId::Stone, 64});
    player.setInventorySlot(physics::Player::HOTBAR_SIZE, {BlockId::Glowstone, 64}); // main inventory
    player.setSelectedSlot(1);
    game::collectDynamicLights(manager, player);
    EXPECT_EQ(manager.size(), 0u) << "a torch elsewhere in the hotbar does not glow";

    player.setSelectedSlot(0);
    game::collectDynamicLights(manager, player);
    ASSERT_EQ(manager.size(), 1u);
    EXPECT_FLOAT_EQ(manager.lights()[0].radius, 14.0f);
}

TEST_F(DynLightCollect, EmptyHandsAndNonEmittersSubmitNothing) {
    game::collectDynamicLights(manager, player);
    EXPECT_EQ(manager.size(), 0u);

    hold(BlockId::Torch, 0); // used-up stack
    game::collectDynamicLights(manager, player);
    EXPECT_EQ(manager.size(), 0u);

    for (const BlockId id : {BlockId::Cobblestone, BlockId::RedstoneOre, BlockId::Lava}) {
        hold(id, 16);
        game::collectDynamicLights(manager, player);
        EXPECT_EQ(manager.size(), 0u) << static_cast<int>(id);
    }
}

TEST_F(DynLightCollect, EachFrameReplacesThePreviousFramesLights) {
    hold(BlockId::Torch, 64);
    game::collectDynamicLights(manager, player);
    game::collectDynamicLights(manager, player);
    EXPECT_EQ(manager.size(), 1u);

    hold(BlockId::Stone, 64);
    game::collectDynamicLights(manager, player);
    EXPECT_EQ(manager.size(), 0u) << "putting the torch away must switch the light off";
}

TEST_F(DynLightCollect, GlowingDroppedItemsLightTheWorld) {
    ecs::Registry reg;
    const glm::vec3 dropPos{10.0f, 64.0f, 2.0f};
    (void)ecs::EntityFactory::spawnItemDrop(reg, dropPos, BlockId::Glowstone, 3);
    (void)ecs::EntityFactory::spawnItemDrop(reg, {11.0f, 64.0f, 2.0f}, BlockId::Cobblestone, 1);
    (void)ecs::EntityFactory::spawnItemDrop(reg, {12.0f, 64.0f, 2.0f}, BlockId::Torch, 1);

    game::collectDynamicLights(manager, player, &reg);

    ASSERT_EQ(manager.size(), 2u);
    for (const lighting::DynamicLight& l : manager.lights()) {
        EXPECT_EQ(l.source, LightSource::DroppedItem);
    }
    const lighting::DynamicLight& glow = manager.lights()[0].radius == 15.0f ? manager.lights()[0] : manager.lights()[1];
    EXPECT_FLOAT_EQ(glow.radius, 15.0f);
    // Lit from the item's centre, not its feet, so it does not sit inside the floor below.
    EXPECT_FLOAT_EQ(glow.position.x, dropPos.x);
    EXPECT_FLOAT_EQ(glow.position.y, dropPos.y + ecs::EntityFactory::ITEM_SIZE * 0.5f);
    EXPECT_FLOAT_EQ(glow.position.z, dropPos.z);
}

TEST_F(DynLightCollect, DroppedItemsFollowTheirRenderInterpolatedPosition) {
    ecs::Registry reg;
    const ecs::EntityId e = ecs::EntityFactory::spawnItemDrop(reg, {0.0f, 64.0f, 0.0f}, BlockId::Glowstone, 1);
    auto& t = reg.get<ecs::TransformComp>(e);
    t.prevPos = {0.0f, 64.0f, 0.0f};
    t.pos = {2.0f, 64.0f, 0.0f};
    player.setRenderAlpha(0.25f);

    game::collectDynamicLights(manager, player, &reg);
    ASSERT_EQ(manager.size(), 1u);
    EXPECT_FLOAT_EQ(manager.lights()[0].position.x, 0.5f);
}

TEST_F(DynLightCollect, HeldLightAndDropsAreCollectedTogether) {
    ecs::Registry reg;
    (void)ecs::EntityFactory::spawnItemDrop(reg, {6.0f, 69.0f, -3.0f}, BlockId::Glowstone, 1);
    hold(BlockId::Torch, 1);

    game::collectDynamicLights(manager, player, &reg);
    ASSERT_EQ(manager.size(), 2u);
    size_t held = 0;
    for (const lighting::DynamicLight& l : manager.lights()) {
        held += l.source == LightSource::HeldItem ? 1u : 0u;
    }
    EXPECT_EQ(held, 1u);
}
