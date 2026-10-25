#include <gtest/gtest.h>
#include "physics/Player.hpp"
#include "world/World.hpp"
#include "core/Config.hpp"

#include <cmath>

namespace betacraft::physics {

TEST(PhysicsFixedStepTest, InterpolationAlphaAccuracy) {
    Player player;
    const glm::vec3 startPos{10.0f, 65.0f, 10.0f};
    player.setPosition(startPos);
    player.resetInterpolationHistory();

    // Verify initial state
    EXPECT_EQ(player.getInterpolatedPosition(0.0f), startPos);
    EXPECT_EQ(player.getInterpolatedPosition(0.5f), startPos);
    EXPECT_EQ(player.getInterpolatedPosition(1.0f), startPos);

    // Simulate one step manually or update position
    world::World world(12345, "");
    PlayerInput input{.forward = true};
    constexpr float FIXED_DT = 1.0f / 60.0f;
    player.fixedUpdate(FIXED_DT, input, world);

    const glm::vec3 prevPos = startPos;
    const glm::vec3 currPos = player.getPosition();
    EXPECT_NE(prevPos, currPos);

    // Alpha 0.0 -> prevPos
    const glm::vec3 interp0 = player.getInterpolatedPosition(0.0f);
    EXPECT_NEAR(interp0.x, prevPos.x, 1e-5f);
    EXPECT_NEAR(interp0.y, prevPos.y, 1e-5f);
    EXPECT_NEAR(interp0.z, prevPos.z, 1e-5f);

    // Alpha 1.0 -> currPos
    const glm::vec3 interp1 = player.getInterpolatedPosition(1.0f);
    EXPECT_NEAR(interp1.x, currPos.x, 1e-5f);
    EXPECT_NEAR(interp1.y, currPos.y, 1e-5f);
    EXPECT_NEAR(interp1.z, currPos.z, 1e-5f);

    // Alpha 0.5 -> exact midpoint
    const glm::vec3 interpMid = player.getInterpolatedPosition(0.5f);
    const glm::vec3 expectedMid = glm::mix(prevPos, currPos, 0.5f);
    EXPECT_NEAR(interpMid.x, expectedMid.x, 1e-5f);
    EXPECT_NEAR(interpMid.y, expectedMid.y, 1e-5f);
    EXPECT_NEAR(interpMid.z, expectedMid.z, 1e-5f);

    // Interpolated eye position
    const glm::vec3 eye0 = player.getInterpolatedEyePosition(0.0f);
    const glm::vec3 eye1 = player.getInterpolatedEyePosition(1.0f);
    const glm::vec3 eyeMid = player.getInterpolatedEyePosition(0.5f);
    EXPECT_NEAR(eyeMid.x, (eye0.x + eye1.x) * 0.5f, 1e-4f);
    EXPECT_NEAR(eyeMid.y, (eye0.y + eye1.y) * 0.5f, 1e-4f);
    EXPECT_NEAR(eyeMid.z, (eye0.z + eye1.z) * 0.5f, 1e-4f);

    // Interpolated view matrix
    const glm::mat4 view0 = player.getInterpolatedViewMatrix(0.0f);
    const glm::mat4 view1 = player.getInterpolatedViewMatrix(1.0f);
    EXPECT_NE(view0, view1);
}

TEST(PhysicsFixedStepTest, ResetInterpolationHistoryOnTeleport) {
    Player player;
    player.setPosition(glm::vec3(5.0f, 60.0f, 5.0f));

    world::World world(12345, "");
    PlayerInput input{.forward = true};
    player.fixedUpdate(1.0f / 60.0f, input, world);

    // Teleport player
    const glm::vec3 teleportPos{100.0f, 80.0f, 200.0f};
    player.setPosition(teleportPos);

    // After setPosition, history must be reset to teleportPos
    EXPECT_EQ(player.getInterpolatedPosition(0.0f), teleportPos);
    EXPECT_EQ(player.getInterpolatedPosition(0.5f), teleportPos);
    EXPECT_EQ(player.getInterpolatedPosition(1.0f), teleportPos);
}

TEST(PhysicsFixedStepTest, ZeroLatencyYawPitch) {
    Player player;
    player.setRotation(0.0f, 0.0f);

    player.rotate(45.0f, 30.0f);
    EXPECT_FLOAT_EQ(player.getYaw(), 45.0f);
    EXPECT_FLOAT_EQ(player.getPitch(), 30.0f);

    // Rotation is immediate, view direction responds without tick delay
    const glm::vec3 dir = player.getLookDirection();
    EXPECT_GT(dir.x, 0.0f);
    EXPECT_GT(dir.y, 0.0f);
}

TEST(PhysicsFixedStepTest, DeterminismAcrossFramerates) {
    world::World world(42, "");
    // Ensure chunks around spawn are loaded and solid flat terrain
    world.ensureChunkLoaded({0, 0});
    world.ensureChunkLoaded({1, 0});
    world.ensureChunkLoaded({0, 1});
    world.ensureChunkLoaded({1, 1});
    for (int32_t x = 0; x < 32; ++x) {
        for (int32_t z = 0; z < 32; ++z) {
            world.setBlock(x, 60, z, voxel::BlockId::Stone);
        }
    }

    const glm::vec3 startPos{16.0f, 61.0f, 16.0f};
    PlayerInput input{.forward = true, .jump = true};
    constexpr float TOTAL_TIME = 2.0f;
    constexpr float FIXED_DT = 1.0f / 60.0f;

    auto runSimulation = [&](float frameDt) -> glm::vec3 {
        Player player;
        player.setPosition(startPos);
        player.resetInterpolationHistory();

        float accumulator = 0.0f;
        const int totalFrames = static_cast<int>(std::round(TOTAL_TIME / frameDt));

        for (int f = 0; f < totalFrames; ++f) {
            accumulator += frameDt;
            constexpr float MAX_ACCUMULATOR = 0.25f;
            accumulator = std::min(accumulator, MAX_ACCUMULATOR);

            while (accumulator >= FIXED_DT) {
                player.fixedUpdate(FIXED_DT, input, world);
                accumulator -= FIXED_DT;
            }
        }

        return player.getPosition();
    };

    const glm::vec3 pos30 = runSimulation(1.0f / 30.0f);
    const glm::vec3 pos60 = runSimulation(1.0f / 60.0f);
    const glm::vec3 pos144 = runSimulation(1.0f / 144.0f);

    // Trajectories and final position must be identical regardless of frame rate
    EXPECT_NEAR(pos30.x, pos60.x, 1e-4f);
    EXPECT_NEAR(pos30.y, pos60.y, 1e-4f);
    EXPECT_NEAR(pos30.z, pos60.z, 1e-4f);

    EXPECT_NEAR(pos60.x, pos144.x, 1e-4f);
    EXPECT_NEAR(pos60.y, pos144.y, 1e-4f);
    EXPECT_NEAR(pos60.z, pos144.z, 1e-4f);
}

TEST(PhysicsFixedStepTest, SpiralOfDeathAccumulatorClamp) {
    Player player;
    world::World world(42, "");
    PlayerInput input{.forward = true};
    constexpr float FIXED_DT = 1.0f / 60.0f;
    constexpr float MAX_ACCUMULATOR = 0.25f;

    // Simulate a massive 1.5-second frame freeze
    float accumulator = 1.5f;
    accumulator = std::min(accumulator, MAX_ACCUMULATOR);
    EXPECT_FLOAT_EQ(accumulator, 0.25f);

    int ticksExecuted = 0;
    while (accumulator >= FIXED_DT) {
        player.fixedUpdate(FIXED_DT, input, world);
        accumulator -= FIXED_DT;
        ++ticksExecuted;
    }

    // 0.25s / (1/60) = exactly 15 ticks
    EXPECT_EQ(ticksExecuted, 15);
    EXPECT_LT(accumulator, FIXED_DT);
    EXPECT_GE(accumulator, 0.0f);
}

// (bug 9): the walking bob moves the camera only. Raycasts, melee and drops use the
// gameplay eye, which must sit exactly at feet + eye height.
TEST(PhysicsFixedStepTest, BobbingMovesCameraButNotGameplayEye) {
    world::World world(42, "");
    world.ensureChunkLoaded({0, 0});
    world.ensureChunkLoaded({1, 0});
    for (int32_t x = 0; x < 32; ++x) {
        for (int32_t z = 0; z < 16; ++z) {
            world.setBlock(x, 119, z, voxel::BlockId::Stone);
        }
    }

    Player player;
    player.setRotation(0.0f, 0.0f); // Walking along +X
    player.setPosition({2.5f, 120.0f, 8.5f});
    for (int i = 0; i < 10; ++i) {
        player.fixedUpdate(1.0f / 60.0f, {}, world);
    }
    ASSERT_TRUE(player.isOnGround());

    const PlayerInput walk{.forward = true};
    for (int i = 0; i < 30; ++i) {
        player.fixedUpdate(1.0f / 60.0f, walk, world);
    }
    ASSERT_GT(player.getBobbingIntensity(), 0.0f);

    for (const float a : {0.0f, 0.5f, 1.0f}) {
        const glm::vec3 pos = player.getInterpolatedPosition(a);
        const glm::vec3 eye = player.getInterpolatedEyePosition(a);
        EXPECT_FLOAT_EQ(eye.y, pos.y + core::EngineConfig::PLAYER_EYE_HEIGHT) << "alpha " << a;
        const glm::vec3 camera = player.getCameraEyePosition(a);
        EXPECT_FLOAT_EQ(camera.x, eye.x);
        EXPECT_FLOAT_EQ(camera.z, eye.z);
        EXPECT_NE(camera.y, eye.y) << "the camera bobs, alpha " << a;
    }
    player.setRenderAlpha(1.0f);
    EXPECT_EQ(player.getEyePosition(), player.getInterpolatedEyePosition(1.0f));
}

} // namespace betacraft::physics
