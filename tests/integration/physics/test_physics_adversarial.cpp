// Adversarial checks on player physics against a real World: the same 2 s walk-and-jump at 144,
// 60, 30 and 10 FPS and under random 3-45 ms frames, interpolation clamping, monotonicity and the
// teleport snap, landing after 1 s lag spikes, walls and inner corners at walking and sprinting
// speed, the sneak ledge guard (also at negative coordinates), and the Creative void safety net.
#include "physics/Player.hpp"
#include "test_support/TempDir.hpp"
#include "voxel/Block.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <utility>

using namespace betacraft;
using namespace betacraft::physics;
using namespace betacraft::voxel;

// ============================================================================
// SUITE 1: Fixed Timestep Determinism & Interpolation
// ============================================================================

TEST(AdversarialPhysics, DeterminismAcrossFramerateAndJitter) {
    test::TempDir tempDir;
    world::World world(777, tempDir.path());
    world.ensureChunkLoaded({0, 0});
    world.ensureChunkLoaded({1, 0});
    world.ensureChunkLoaded({0, 1});
    world.ensureChunkLoaded({1, 1});
    for (int x = 0; x < 32; ++x) {
        for (int z = 0; z < 32; ++z) {
            world.setBlock(x, 60, z, BlockId::Stone);
        }
    }

    const glm::vec3 spawnPos{16.0f, 61.0f, 16.0f};
    PlayerInput input{.forward = true, .jump = true};
    constexpr float TOTAL_TIME = 2.0f; // 2 seconds = 120 ticks
    constexpr float FIXED_DT = 1.0f / 60.0f;
    constexpr float MAX_ACCUMULATOR = 0.25f;

    auto runSimWithConstantDt = [&](float frameDt) -> std::pair<glm::vec3, int> {
        Player player;
        player.setPosition(spawnPos);
        player.resetInterpolationHistory();

        float accumulator = 0.0f;
        int totalFrames = static_cast<int>(std::round(TOTAL_TIME / frameDt));
        int totalTicks = 0;

        for (int f = 0; f < totalFrames; ++f) {
            accumulator = std::min(accumulator + frameDt, MAX_ACCUMULATOR);
            while (accumulator >= FIXED_DT - 1e-5f) {
                player.fixedUpdate(FIXED_DT, input, world);
                accumulator -= FIXED_DT;
                if (accumulator < 0.0f) accumulator = 0.0f;
                totalTicks++;
            }
        }
        return {player.getPosition(), totalTicks};
    };

    auto [pos144, ticks144] = runSimWithConstantDt(1.0f / 144.0f);
    auto [pos60, ticks60]   = runSimWithConstantDt(1.0f / 60.0f);
    auto [pos30, ticks30]   = runSimWithConstantDt(1.0f / 30.0f);
    auto [pos10, ticks10]   = runSimWithConstantDt(1.0f / 10.0f);

    // 120 ticks in 2 seconds at 60 TPS
    ASSERT_EQ(ticks144, 120) << "144 FPS must execute exactly 120 ticks";
    ASSERT_EQ(ticks60, 120) << "60 FPS must execute exactly 120 ticks";
    ASSERT_EQ(ticks30, 120) << "30 FPS must execute exactly 120 ticks";
    ASSERT_EQ(ticks10, 120) << "10 FPS must execute exactly 120 ticks";

    // Positions must be numerically identical within floating point epsilon
    ASSERT_NEAR(pos144.x, pos60.x, 1e-4f) << "144 FPS vs 60 FPS X drift";
    ASSERT_NEAR(pos144.y, pos60.y, 1e-4f) << "144 FPS vs 60 FPS Y drift";
    ASSERT_NEAR(pos144.z, pos60.z, 1e-4f) << "144 FPS vs 60 FPS Z drift";

    ASSERT_NEAR(pos30.x, pos60.x, 1e-4f) << "30 FPS vs 60 FPS X drift";
    ASSERT_NEAR(pos30.y, pos60.y, 1e-4f) << "30 FPS vs 60 FPS Y drift";
    ASSERT_NEAR(pos30.z, pos60.z, 1e-4f) << "30 FPS vs 60 FPS Z drift";

    ASSERT_NEAR(pos10.x, pos60.x, 1e-4f) << "10 FPS vs 60 FPS X drift";
    ASSERT_NEAR(pos10.y, pos60.y, 1e-4f) << "10 FPS vs 60 FPS Y drift";
    ASSERT_NEAR(pos10.z, pos60.z, 1e-4f) << "10 FPS vs 60 FPS Z drift";

    // Chaotic frame jitter test:
    // Every frame has random dt between 3ms (333 FPS) and 45ms (22 FPS)
    Player playerJitter;
    playerJitter.setPosition(spawnPos);
    playerJitter.resetInterpolationHistory();

    std::mt19937 rng(9999);
    std::uniform_real_distribution<float> jitterDist(0.003f, 0.045f);

    float elapsed = 0.0f;
    float jitterAccumulator = 0.0f;
    int jitterTicks = 0;

    while (elapsed < TOTAL_TIME) {
        float dt = jitterDist(rng);
        if (elapsed + dt > TOTAL_TIME) {
            dt = TOTAL_TIME - elapsed;
        }
        elapsed += dt;
        jitterAccumulator = std::min(jitterAccumulator + dt, MAX_ACCUMULATOR);
        while (jitterAccumulator >= FIXED_DT - 1e-5f) {
            playerJitter.fixedUpdate(FIXED_DT, input, world);
            jitterAccumulator -= FIXED_DT;
            if (jitterAccumulator < 0.0f) jitterAccumulator = 0.0f;
            jitterTicks++;
        }
    }

    ASSERT_EQ(jitterTicks, 120) << "Chaotic jitter must execute exactly 120 ticks";
    glm::vec3 posJitter = playerJitter.getPosition();
    ASSERT_NEAR(posJitter.x, pos60.x, 1e-4f) << "Jitter X position drift";
    ASSERT_NEAR(posJitter.y, pos60.y, 1e-4f) << "Jitter Y position drift";
    ASSERT_NEAR(posJitter.z, pos60.z, 1e-4f) << "Jitter Z position drift";
}

TEST(AdversarialPhysics, InterpolationMidpointAndTeleportSnap) {
    Player player;
    glm::vec3 startPos{0.0f, 60.0f, 0.0f};
    player.setPosition(startPos);
    player.resetInterpolationHistory();

    test::TempDir tempDir;
    world::World world(42, tempDir.path());
    PlayerInput input{.forward = true};
    player.fixedUpdate(1.0f / 60.0f, input, world);

    glm::vec3 p0 = player.getInterpolatedPosition(0.0f);
    glm::vec3 pHalf = player.getInterpolatedPosition(0.5f);
    glm::vec3 p1 = player.getInterpolatedPosition(1.0f);

    // Convex hull: pHalf must be exactly between p0 and p1
    glm::vec3 expectedMid = (p0 + p1) * 0.5f;
    ASSERT_NEAR(pHalf.x, expectedMid.x, 1e-5f) << "Interpolation midpoint X mismatch";
    ASSERT_NEAR(pHalf.y, expectedMid.y, 1e-5f) << "Interpolation midpoint Y mismatch";
    ASSERT_NEAR(pHalf.z, expectedMid.z, 1e-5f) << "Interpolation midpoint Z mismatch";

    // Teleportation snap
    glm::vec3 teleportPos{500.0f, 120.0f, -300.0f};
    player.setPosition(teleportPos);
    // setPosition internally calls resetInterpolationHistory()
    glm::vec3 snap0 = player.getInterpolatedPosition(0.0f);
    glm::vec3 snapHalf = player.getInterpolatedPosition(0.5f);
    glm::vec3 snap1 = player.getInterpolatedPosition(1.0f);

    ASSERT_EQ(snap0, teleportPos) << "Teleport snap at alpha 0.0 failed";
    ASSERT_EQ(snapHalf, teleportPos) << "Teleport snap at alpha 0.5 failed";
    ASSERT_EQ(snap1, teleportPos) << "Teleport snap at alpha 1.0 failed";
}

TEST(AdversarialPhysics, ConsecutiveLagSpikesAndNoTunneling) {
    test::TempDir tempDir;
    world::World world(999, tempDir.path());
    world.ensureChunkLoaded({0, 0});
    // Create solid stone floor at Y=60 and clear air shaft above
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            world.setBlock(x, 60, z, BlockId::Stone);
            for (int y = 61; y <= 75; ++y) {
                world.setBlock(x, y, z, BlockId::Air);
            }
        }
    }

    // Spawn player falling from Y=65
    Player player;
    player.setPosition(glm::vec3(8.0f, 65.0f, 8.0f));
    player.resetInterpolationHistory();

    constexpr float FIXED_DT = 1.0f / 60.0f;
    constexpr float MAX_ACCUMULATOR = 0.25f;
    PlayerInput input{};

    // Simulate 20 consecutive massive lag spikes of 1.0 second each
    float accumulator = 0.0f;
    for (int spike = 0; spike < 20; ++spike) {
        accumulator = std::min(accumulator + 1.0f, MAX_ACCUMULATOR);
        int ticksInSpike = 0;
        while (accumulator >= FIXED_DT - 1e-5f) {
            player.fixedUpdate(FIXED_DT, input, world);
            accumulator -= FIXED_DT;
            if (accumulator < 0.0f) accumulator = 0.0f;
            ticksInSpike++;
        }
        // Each spike must execute exactly 15 ticks, never more
        ASSERT_EQ(ticksInSpike, 15) << "Ticks per lag spike must be clamped to 15";
    }

    // Player must have fallen and landed on top of Stone block at Y=60 (feet at Y=61.0f)
    // Box min.y is player.position.y. Player must NOT tunnel through stone floor into void!
    const glm::vec3 finalPos = player.getPosition();
    ASSERT_GE(finalPos.y, 60.99f) << "Player tunneled through solid ground during lag spikes";
    ASSERT_LE(finalPos.y, 61.05f) << "Player hovered above the ground after lag spikes";
    ASSERT_TRUE(player.isOnGround()) << "Player must be marked as onGround after landing";
}

// ============================================================================
// SUITE 2: Collision Stepping, Walls and Ledges
// ============================================================================

TEST(AdversarialPhysics, AlphaInterpolationMonotonicityAndBounds) {
    test::TempDir tempDir;
    world::World world(123, tempDir.path());
    world.ensureChunkLoaded({0, 0});
    // Platform at Y = 100
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            world.setBlock(x, 100, z, BlockId::Stone);
        }
    }

    Player player;
    player.setPosition(glm::vec3(8.0f, 101.0f, 8.0f));
    player.resetInterpolationHistory();

    // Advance 1 tick forward
    PlayerInput input{.forward = true};
    constexpr float FIXED_DT = 1.0f / 60.0f;
    player.fixedUpdate(FIXED_DT, input, world);

    // m_prevPosition is start (8.0, 101.0, 8.0), m_position has moved
    const glm::vec3 p0 = player.getInterpolatedPosition(0.0f);
    const glm::vec3 p1 = player.getInterpolatedPosition(1.0f);
    EXPECT_NE(p0, p1);

    // Clamping checks
    EXPECT_EQ(player.getInterpolatedPosition(-0.5f), p0);
    EXPECT_EQ(player.getInterpolatedPosition(0.0f), p0);
    EXPECT_EQ(player.getInterpolatedPosition(1.0f), p1);
    EXPECT_EQ(player.getInterpolatedPosition(1.5f), p1);

    // Strict monotonicity along movement vector
    float prevDist = -1.0f;
    for (int step = 0; step <= 20; ++step) {
        const float alpha = step / 20.0f;
        const glm::vec3 p = player.getInterpolatedPosition(alpha);
        const float distFromP0 = glm::distance(p, p0);
        EXPECT_GE(distFromP0, prevDist - 1e-6f);
        prevDist = distFromP0;
    }
}

TEST(AdversarialPhysics, LandingFlushOnBlockBoundary) {
    test::TempDir tempDir;
    world::World world(999, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Create solid stone floor high at Y = 100 to avoid procedural terrain
    for (int32_t x = 0; x < 16; ++x) {
        for (int32_t z = 0; z < 16; ++z) {
            world.setBlock(x, 100, z, BlockId::Stone);
        }
    }

    Player player;
    // Drop player from Y = 105.25f (in air above sky platform)
    player.setPosition(glm::vec3(8.0f, 105.25f, 8.0f));
    player.resetInterpolationHistory();

    PlayerInput noInput{};
    constexpr float FIXED_DT = 1.0f / 60.0f;

    // Simulate 60 ticks (1.0 second) to let player fall and land
    for (int t = 0; t < 60; ++t) {
        player.fixedUpdate(FIXED_DT, noInput, world);
    }

    // Player bounding box height is 1.8f, feet should land flush on stone top (Y = 101.0f)
    EXPECT_TRUE(player.isOnGround());
    EXPECT_NEAR(player.getPosition().y, 101.0f, 1e-3f);
    EXPECT_FLOAT_EQ(player.getPosition().x, 8.0f);
    EXPECT_FLOAT_EQ(player.getPosition().z, 8.0f);
}

TEST(AdversarialPhysics, HorizontalWallCollisionPreventsPenetration) {
    test::TempDir tempDir;
    world::World world(999, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Stone floor at Y = 100
    for (int32_t x = 0; x < 16; ++x) {
        for (int32_t z = 0; z < 16; ++z) {
            world.setBlock(x, 100, z, BlockId::Stone);
        }
    }

    // Solid wall at X = 10 from Y = 101 to Y = 103
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t y = 101; y <= 103; ++y) {
            world.setBlock(10, y, z, BlockId::Stone);
        }
    }

    Player player;
    // Place player at X = 8.5 (facing +X, yaw = 0)
    player.setPosition(glm::vec3(8.5f, 101.0f, 8.0f));
    player.setRotation(0.0f, 0.0f); // 0 deg = +X in Betacraft
    player.resetInterpolationHistory();

    PlayerInput forwardInput{.forward = true};
    constexpr float FIXED_DT = 1.0f / 60.0f;

    // Settle first tick then walk forward for 60 ticks into the wall
    for (int t = 0; t < 60; ++t) {
        player.fixedUpdate(FIXED_DT, forwardInput, world);
    }

    // Player half-width is 0.3f. Wall is at X=10.0f (min.x = 10.0f).
    // Player max.x = player.x + 0.3f <= 10.0f => player.x <= 9.7f.
    EXPECT_LE(player.getPosition().x, 9.701f);
    EXPECT_GE(player.getPosition().x, 9.60f); // Pressed closely against wall
}

TEST(AdversarialPhysics, SneakLedgeProtectionDeterminism) {
    test::TempDir tempDir;
    world::World world(777, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // High platform in sky at Y = 100, extending from X=0..8, Z=0..16
    // Top surface of platform is at Y = 101.0f, platform edge is at X = 9.0f
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t x = 0; x <= 8; ++x) {
            world.setBlock(x, 100, z, BlockId::Cobblestone);
        }
    }

    Player player;
    // Place player at (7.5f, 101.0f, 8.0f) facing +X (yaw = 0)
    player.setPosition(glm::vec3(7.5f, 101.0f, 8.0f));
    player.setRotation(0.0f, 0.0f);
    player.resetInterpolationHistory();

    // Settle player on ground
    PlayerInput settleInput{};
    for (int i = 0; i < 10; ++i) {
        player.fixedUpdate(1.0f / 60.0f, settleInput, world);
    }
    ASSERT_TRUE(player.isOnGround());

    // Walk towards edge at X=9.0 with sneak held for 120 ticks (2.0s)
    PlayerInput sneakForward{.forward = true, .sneak = true};
    for (int i = 0; i < 120; ++i) {
        player.fixedUpdate(1.0f / 60.0f, sneakForward, world);
    }

    // Player must remain on ground and NOT fall off ledge (Y must remain 101.0f)
    EXPECT_TRUE(player.isOnGround());
    EXPECT_NEAR(player.getPosition().y, 101.0f, 1e-3f);
    // Player position X must be bounded before stepping off platform edge (X=9.0)
    EXPECT_LE(player.getPosition().x, 9.35f);
    EXPECT_GE(player.getPosition().x, 8.5f);
}

TEST(AdversarialPhysics, InnerCornerCollisionResolution) {
    test::TempDir tempDir;
    world::World world(888, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Floor at Y = 100
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t x = 0; x < 16; ++x) {
            world.setBlock(x, 100, z, BlockId::Stone);
        }
    }

    // Wall 1 at X = 10, Wall 2 at Z = 10 (forming an inside corner)
    for (int32_t y = 101; y <= 103; ++y) {
        for (int32_t z = 0; z < 16; ++z) {
            world.setBlock(10, y, z, BlockId::Stone);
        }
        for (int32_t x = 0; x < 16; ++x) {
            world.setBlock(x, y, 10, BlockId::Stone);
        }
    }

    Player player;
    // Place player at (8.5f, 101.0f, 8.5f) facing diagonal +X +Z (yaw = 45)
    player.setPosition(glm::vec3(8.5f, 101.0f, 8.5f));
    player.setRotation(45.0f, 0.0f);
    player.resetInterpolationHistory();

    PlayerInput forwardInput{.forward = true};
    constexpr float FIXED_DT = 1.0f / 60.0f;

    for (int t = 0; t < 60; ++t) {
        player.fixedUpdate(FIXED_DT, forwardInput, world);
    }

    // Player must be safely stopped by both walls without tunneling through the corner
    EXPECT_LE(player.getPosition().x, 9.701f);
    EXPECT_LE(player.getPosition().z, 9.701f);
    EXPECT_GE(player.getPosition().x, 9.50f);
    EXPECT_GE(player.getPosition().z, 9.50f);
    EXPECT_NEAR(player.getPosition().y, 101.0f, 1e-3f);
}

// ============================================================================
// SUITE 3: Player Collision Edge Cases at World Boundaries
// ============================================================================

class AdversarialPlayerCollision : public ::testing::Test {
protected:
    test::TempDir tempDir;
    std::unique_ptr<world::World> world;

    void SetUp() override {
        BlockRegistry::initialize();
        world = std::make_unique<world::World>(173ULL, tempDir.path());
    }
};

TEST_F(AdversarialPlayerCollision, NegativeCoordinatesFloorAndBoundaryMovement) {
    // Load chunks covering [-20..20]
    for (int cz = -2; cz <= 1; ++cz) {
        for (int cx = -2; cx <= 1; ++cx) {
            world->ensureChunkLoaded({cx, cz});
        }
    }

    // Build solid floor at Y = 60 across negative to positive range [-20..20]
    for (int z = -20; z <= 20; ++z) {
        for (int x = -20; x <= 20; ++x) {
            world->setBlock(x, 60, z, BlockId::Stone);
            for (int y = 61; y <= 70; ++y) {
                world->setBlock(x, y, z, BlockId::Air);
            }
        }
    }

    Player player;
    // Spawn player at negative coordinates
    player.setPosition(glm::vec3(-10.5f, 61.0f, -10.5f));
    player.setRotation(45.0f, 0.0f); // Face toward positive origin (+X, +Z)
    player.resetInterpolationHistory();

    PlayerInput input{.forward = true};
    // Move across chunk boundary toward origin
    for (int t = 0; t < 120; ++t) {
        player.fixedUpdate(1.0f / 60.0f, input, *world);
    }

    // Player must remain on ground at Y = 61.0f without falling into the void
    const glm::vec3 pos = player.getPosition();
    EXPECT_NEAR(pos.y, 61.0f, 0.05f);
    EXPECT_TRUE(player.isOnGround());
}

TEST_F(AdversarialPlayerCollision, SneakLedgeGuardAtNegativeCoordinates) {
    world->ensureChunkLoaded({-1, -1});

    // Create a single solitary block at (-5, 60, -5), surrounded by void
    world->setBlock(-5, 60, -5, BlockId::Stone);

    Player player;
    // Stand on the solitary block (feet at Y=61.0f)
    player.setPosition(glm::vec3(-4.5f, 61.0f, -4.5f));
    player.resetInterpolationHistory();

    // Verify on ground initially
    player.fixedUpdate(1.0f / 60.0f, {}, *world);
    EXPECT_TRUE(player.isOnGround());

    // Try to walk forward off the ledge while sneaking
    PlayerInput sneakInput{.forward = true, .sneak = true};
    for (int t = 0; t < 60; ++t) {
        player.fixedUpdate(1.0f / 60.0f, sneakInput, *world);
    }

    // Sneak ledge guard must have prevented player from walking off the solitary block!
    const glm::vec3 pos = player.getPosition();
    EXPECT_NEAR(pos.y, 61.0f, 0.05f) << "Player must not fall off ledge while sneaking";
    EXPECT_TRUE(player.isOnGround());
    EXPECT_GE(pos.x, -5.2f);
    EXPECT_LE(pos.x, -3.8f);
    EXPECT_GE(pos.z, -5.2f);
    EXPECT_LE(pos.z, -3.8f);
}

TEST_F(AdversarialPlayerCollision, VoidFallRecoveryAndCeilingBoundary) {
    world->ensureChunkLoaded({0, 0});

    Player player;
    player.setGameMode(core::GameMode::Creative); // The void safety net is Creative-only
    // Drop player into deep void below Y = -15.0f
    player.setPosition(glm::vec3(8.0f, -16.0f, 8.0f));
    player.resetInterpolationHistory();

    player.fixedUpdate(1.0f / 60.0f, {}, *world);

    // Engine must detect Y < -15.0f and teleport back to Y = 95.0f safely
    EXPECT_GE(player.getPosition().y, 90.0f) << "Player below void must be teleported back to safety";

    // Ceiling boundary test: high elevation above Y = 128
    // Build solid ceiling at Y = 127
    for (int z = 6; z <= 10; ++z) {
        for (int x = 6; x <= 10; ++x) {
            world->setBlock(x, 127, z, BlockId::Stone);
        }
    }

    player.setPosition(glm::vec3(8.0f, 140.0f, 8.0f));
    player.resetInterpolationHistory();

    // Fall down from Y = 140 onto ceiling at Y = 127 (feet should land at Y = 128.0f)
    for (int t = 0; t < 120; ++t) {
        player.fixedUpdate(1.0f / 60.0f, {}, *world);
    }

    EXPECT_NEAR(player.getPosition().y, 128.0f, 0.05f);
    EXPECT_TRUE(player.isOnGround()) << "Player must land on world ceiling without tunneling";
}

TEST_F(AdversarialPlayerCollision, HighSpeedImpactNoTunneling) {
    world->ensureChunkLoaded({0, 0});

    // Create 1-block thick stone wall at X = 10
    for (int y = 60; y <= 65; ++y) {
        for (int z = 0; z < 16; ++z) {
            world->setBlock(10, y, z, BlockId::Stone);
        }
    }
    // Solid floor at Y = 60
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            world->setBlock(x, 60, z, BlockId::Stone);
        }
    }

    Player player;
    player.setPosition(glm::vec3(8.0f, 61.0f, 8.0f));
    player.resetInterpolationHistory();

    // Sprint right into the wall
    PlayerInput sprintInput{.forward = true, .sprint = true};
    for (int t = 0; t < 120; ++t) {
        player.fixedUpdate(1.0f / 60.0f, sprintInput, *world);
    }

    // Player width = 0.6f => half width = 0.3f. Wall at X = 10.
    // Contact position should be <= 10.0f - 0.3f = 9.7f
    EXPECT_LE(player.getPosition().x, 9.72f) << "Player must not tunnel through wall at high speed";
}
