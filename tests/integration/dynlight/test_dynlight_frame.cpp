// On the renderer's CPU side: the dynamic-light frame WorldRenderer
// builds each frame from the collected lights and its own camera (selection, Forward+ tiles in
// framebuffer pixels, UBO bytes), and the light level the first-person hand is shaded with.
// The GL upload itself is covered by test_dynlight_shader.cpp and the e2e binary runs.
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"
#include "game/DynamicLightCollector.hpp"
#include "lighting/DynamicLightFrame.hpp"
#include "lighting/DynamicLightManager.hpp"
#include "physics/Player.hpp"
#include "render/WorldRenderer.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>

#include <cstring>

using namespace betacraft;
using lighting::DynamicLightManager;
using lighting::LightSource;
using render::WorldRenderer;
using voxel::BlockId;

namespace {

class DynLightFrameIntegration : public ::testing::Test {
protected:
    void SetUp() override {
        voxel::BlockRegistry::initialize();
        player.setPosition({0.5f, 70.0f, 0.5f});
        player.resetInterpolationHistory();
        player.setRotation(-90.0f, 0.0f);
        for (size_t i = 0; i < physics::Player::HOTBAR_SIZE; ++i) {
            player.setInventorySlot(i, {});
        }
    }

    void hold(BlockId id) {
        player.setInventorySlot(2, {id, 1});
        player.setSelectedSlot(2);
    }

    void build(glm::ivec2 framebuffer, ecs::Registry* entities = nullptr) {
        game::collectDynamicLights(manager, player, entities);
        WorldRenderer::buildDynamicLightFrame(frame, manager, player, config, framebuffer);
    }

    // gl_FragCoord of a world point for the renderer's camera at this framebuffer size.
    glm::vec2 fragCoordOf(const glm::vec3& worldPos, glm::ivec2 framebuffer) const {
        const glm::mat4 vp = WorldRenderer::cameraViewProjection(
            player, config, static_cast<float>(framebuffer.x) / static_cast<float>(framebuffer.y));
        const glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        return (ndc * 0.5f + 0.5f) * glm::vec2(framebuffer);
    }

    int32_t uboLightCount() const {
        int32_t count = -1;
        std::memcpy(&count, frame.uboBytes().data() + lighting::ubo::kLightCountOffset, sizeof(count));
        return count;
    }

    physics::Player player;
    core::EngineConfig config{};
    DynamicLightManager manager;
    lighting::DynamicLightFrame frame;
};

} // namespace

TEST_F(DynLightFrameIntegration, HeldTorchLightsWhatThePlayerLooksAt) {
    hold(BlockId::Torch);
    const glm::ivec2 fb{1280, 720};
    build(fb);

    ASSERT_EQ(frame.selection().count, 1u);
    EXPECT_EQ(frame.selection().lights[0].source, LightSource::HeldItem);
    EXPECT_EQ(uboLightCount(), 1);

    const glm::vec3 eye = player.getEyePosition();
    const glm::vec3 wall = eye + player.getLookDirection() * 3.0f;
    const glm::vec2 frag = fragCoordOf(wall, fb);
    EXPECT_NEAR(frag.x, 640.0f, 1.0f);
    EXPECT_NEAR(frag.y, 360.0f, 1.0f);
    EXPECT_GT(frame.shade(wall, frag).r, 0.5f);

    const glm::vec3 farWall = eye + player.getLookDirection() * 20.0f;
    EXPECT_EQ(frame.shade(farWall, fragCoordOf(farWall, fb)), glm::vec3(0.0f)) << "beyond the torch's 14 blocks";
}

TEST_F(DynLightFrameIntegration, NoGlowingItemUploadsAnEmptyBlock) {
    hold(BlockId::Cobblestone);
    build({1280, 720});
    EXPECT_EQ(frame.selection().count, 0u);
    EXPECT_EQ(uboLightCount(), 0);
}

TEST_F(DynLightFrameIntegration, TilesAreSizedInFramebufferPixels) {
    // HiDPI: a 1280x720 window with a 2560x1440 drawable. gl_FragCoord counts drawable
    // pixels, so the grid must be built from the framebuffer size handed to glViewport.
    hold(BlockId::Torch);
    build({2560, 1440});
    EXPECT_EQ(frame.grid().tileSizePx(), glm::vec2(160.0f, 160.0f));

    const glm::vec3 wall = player.getEyePosition() + player.getLookDirection() * 3.0f;
    const glm::vec2 frag = fragCoordOf(wall, {2560, 1440});
    EXPECT_NEAR(frag.x, 1280.0f, 1.0f);
    EXPECT_GT(frame.shade(wall, frag).r, 0.5f);
    // The opposite corner tile: the held light lies right of and below the view centre, but
    // its 14-block sphere covers the whole screen, so the corner stays lit.
    EXPECT_NE(frame.grid().mask(lighting::kTileCols - 1, lighting::kTileRows - 1), 0u);
}

TEST_F(DynLightFrameIntegration, DroppedGlowstoneBehindTheCameraIsCulled) {
    ecs::Registry reg;
    const glm::vec3 behind = player.getEyePosition() - player.getLookDirection() * 30.0f;
    (void)ecs::EntityFactory::spawnItemDrop(reg, behind, BlockId::Glowstone, 1);
    const glm::vec3 ahead = player.getEyePosition() + player.getLookDirection() * 6.0f;
    (void)ecs::EntityFactory::spawnItemDrop(reg, ahead, BlockId::Glowstone, 1);

    build({1280, 720}, &reg);
    EXPECT_EQ(manager.size(), 2u);
    ASSERT_EQ(frame.selection().count, 1u);
    EXPECT_EQ(frame.selection().lights[0].source, LightSource::DroppedItem);
    EXPECT_LT(glm::distance(frame.selection().lights[0].position, ahead), 0.5f);
}

TEST_F(DynLightFrameIntegration, HandIsShadedByTheTorchItHolds) {
    hold(BlockId::Torch);
    build({1280, 720});
    EXPECT_EQ(WorldRenderer::handBlockLight(0, frame, player), 14);
    EXPECT_EQ(WorldRenderer::handBlockLight(15, frame, player), 15) << "static light is never dimmed";

    hold(BlockId::Stone);
    build({1280, 720});
    EXPECT_EQ(WorldRenderer::handBlockLight(0, frame, player), 0);
    EXPECT_EQ(WorldRenderer::handBlockLight(7, frame, player), 7);
}

TEST_F(DynLightFrameIntegration, EmptyHandIsLitByNearbyDroppedGlowstone) {
    ecs::Registry reg;
    const glm::vec3 hand =
        DynamicLightManager::heldLightPosition(player.getEyePosition(), player.getLookDirection());
    // Centre of the item 3 blocks in front of the hand: level 15 - 3.
    const glm::vec3 feet =
        hand + player.getLookDirection() * 3.0f - glm::vec3(0.0f, ecs::EntityFactory::ITEM_SIZE * 0.5f, 0.0f);
    (void)ecs::EntityFactory::spawnItemDrop(reg, feet, BlockId::Glowstone, 1);

    build({1280, 720}, &reg);
    EXPECT_EQ(WorldRenderer::handBlockLight(0, frame, player), 12);
}
