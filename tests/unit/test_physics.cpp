#include "math/AABB.hpp"
#include "physics/Player.hpp"
#include "physics/Raycaster.hpp"
#include "tests/test_helpers.hpp"
#include "voxel/Block.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace betacraft;
using namespace betacraft::physics;
using namespace betacraft::math;
using namespace testing;

TEST(AABBTest, IntersectionAndTransformation) {
    const AABB box1{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    const AABB box2{{0.5f, 0.5f, 0.5f}, {1.5f, 1.5f, 1.5f}};
    const AABB box3{{2.0f, 2.0f, 2.0f}, {3.0f, 3.0f, 3.0f}};

    EXPECT_TRUE(box1.intersects(box2));
    EXPECT_TRUE(box2.intersects(box1));
    EXPECT_FALSE(box1.intersects(box3));

    // Touching on edge: strict inequalities return false
    const AABB boxTouching{{1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f}};
    EXPECT_FALSE(box1.intersects(boxTouching));

    const AABB trans = box1.translated({2.0f, 3.0f, 4.0f});
    EXPECT_FLOAT_EQ(trans.min.x, 2.0f);
    EXPECT_FLOAT_EQ(trans.min.y, 3.0f);
    EXPECT_FLOAT_EQ(trans.min.z, 4.0f);
    EXPECT_FLOAT_EQ(trans.max.x, 3.0f);
    EXPECT_FLOAT_EQ(trans.max.y, 4.0f);
    EXPECT_FLOAT_EQ(trans.max.z, 5.0f);

    const AABB exp = box1.expanded({0.25f, 0.5f, 0.1f});
    EXPECT_FLOAT_EQ(exp.min.x, -0.25f);
    EXPECT_FLOAT_EQ(exp.min.y, -0.5f);
    EXPECT_FLOAT_EQ(exp.min.z, -0.1f);
    EXPECT_FLOAT_EQ(exp.max.x, 1.25f);
    EXPECT_FLOAT_EQ(exp.max.y, 1.5f);
    EXPECT_FLOAT_EQ(exp.max.z, 1.1f);
}

TEST(AABBTest, FromBlockShapes) {
    world::BlockRegistry::initialize();

    const auto cube = AABB::fromBlock(5, 10, 15, world::BlockId::Stone);
    EXPECT_FLOAT_EQ(cube.min.x, 5.0f);
    EXPECT_FLOAT_EQ(cube.min.y, 10.0f);
    EXPECT_FLOAT_EQ(cube.min.z, 15.0f);
    EXPECT_FLOAT_EQ(cube.max.x, 6.0f);
    EXPECT_FLOAT_EQ(cube.max.y, 11.0f);
    EXPECT_FLOAT_EQ(cube.max.z, 16.0f);

    const auto slab = AABB::fromBlock(5, 10, 15, world::BlockId::CobbleSlab);
    EXPECT_FLOAT_EQ(slab.max.y, 10.5f);

    const auto cactus = AABB::fromBlock(0, 0, 0, world::BlockId::Cactus);
    EXPECT_GT(cactus.min.x, 0.0f);
    EXPECT_LT(cactus.max.x, 1.0f);

    const auto torch = AABB::fromBlock(0, 0, 0, world::BlockId::Torch);
    EXPECT_NEAR(torch.min.x, 0.375f, 1e-4f);
    EXPECT_NEAR(torch.max.x, 0.625f, 1e-4f);
}

TEST(RaycasterTest, HitsAndMisses) {
    test::TempDir tempDir;
    world::World w(173ULL, tempDir.path());
    w.ensureChunkLoaded({0, 0});

    // Clear test area in sky at Y=105 to avoid pre-generated terrain
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            for (int y = 100; y <= 110; ++y) {
                w.setBlock(x, y, z, world::BlockId::Air);
            }
        }
    }

    w.setBlock(5, 105, 5, world::BlockId::Stone);

    // Cast from (5.5, 105.5, 2.0) looking directly +Z toward (5, 105, 5)
    const auto hitZ = Raycaster::cast(w, {5.5f, 105.5f, 2.0f}, {0.0f, 0.0f, 1.0f}, 5.0f);
    EXPECT_TRUE(hitZ.hit);
    EXPECT_EQ(hitZ.blockPos, (core::BlockPos{5, 105, 5}));
    EXPECT_EQ(hitZ.adjacentPos, (core::BlockPos{5, 105, 4}));
    EXPECT_EQ(hitZ.face, core::FaceDir::NegZ);
    EXPECT_EQ(hitZ.blockId, world::BlockId::Stone);
    EXPECT_NEAR(hitZ.distance, 3.0f, 0.1f);

    // Cast in opposite direction (-Z) should miss
    const auto missZ = Raycaster::cast(w, {5.5f, 105.5f, 2.0f}, {0.0f, 0.0f, -1.0f}, 5.0f);
    EXPECT_FALSE(missZ.hit);

    // Cast with too short maxDistance should miss
    const auto shortRay = Raycaster::cast(w, {5.5f, 105.5f, 2.0f}, {0.0f, 0.0f, 1.0f}, 1.5f);
    EXPECT_FALSE(shortRay.hit);
}

TEST(PlayerTest, RotationAndPitchClamping) {
    Player p;
    p.setRotation(45.0f, 30.0f);
    EXPECT_FLOAT_EQ(p.getYaw(), 45.0f);
    EXPECT_FLOAT_EQ(p.getPitch(), 30.0f);

    // Pitch must be clamped below 90 degrees to avoid gimbal singularity
    p.setRotation(0.0f, 95.0f);
    EXPECT_LE(p.getPitch(), 89.5f);

    p.setRotation(0.0f, -95.0f);
    EXPECT_GE(p.getPitch(), -89.5f);

    p.rotate(360.0f, 0.0f);
    EXPECT_NEAR(p.getYaw(), 0.0f, 1e-4f);
}

TEST(PlayerTest, InventoryOperations) {
    Player p;

    // Hotbar scroll wrapping
    p.setSelectedSlot(0);
    EXPECT_EQ(p.getSelectedSlot(), 0);
    p.scrollSelectedSlot(-1);
    EXPECT_EQ(p.getSelectedSlot(), 8);
    p.scrollSelectedSlot(1);
    EXPECT_EQ(p.getSelectedSlot(), 0);

    // Custom inventory manipulation
    p.setInventorySlot(30, {world::BlockId::GoldOre, 10});
    EXPECT_EQ(p.getInventorySlot(30).id, world::BlockId::GoldOre);
    EXPECT_EQ(p.getInventorySlot(30).count, 10);
    EXPECT_EQ(p.countItem(world::BlockId::GoldOre), 10);

    // Consume item
    EXPECT_TRUE(p.consumeItem(world::BlockId::GoldOre, 4));
    EXPECT_EQ(p.countItem(world::BlockId::GoldOre), 6);
    EXPECT_FALSE(p.consumeItem(world::BlockId::GoldOre, 20)); // Not enough
    EXPECT_EQ(p.countItem(world::BlockId::GoldOre), 6);

    // Add item
    EXPECT_TRUE(p.addItem(world::BlockId::GoldOre, 14));
    EXPECT_EQ(p.countItem(world::BlockId::GoldOre), 20);

    // Stack limit for tools is 1
    EXPECT_TRUE(p.addItem(world::BlockId::DiamondPickaxe, 1));
}

TEST(PlayerTest, FlyingAndHealth) {
    Player p;
    p.setGameMode(core::GameMode::Creative); // Flying is a Creative cheat (S1.1)
    EXPECT_FALSE(p.isFlying());
    p.toggleFlyMode();
    EXPECT_TRUE(p.isFlying());
    p.toggleFlyMode();
    EXPECT_FALSE(p.isFlying());

    p.setHealth(20);
    EXPECT_EQ(p.getHealth(), 20);
    p.setHealth(25);
    EXPECT_EQ(p.getHealth(), 20);
    p.setHealth(-5);
    EXPECT_EQ(p.getHealth(), 0);
}

// S1.2: inventories start empty; game::applyStarterKit fills a new Creative world's.
TEST(PlayerTest, DefaultPlayerHasEmptyInventory) {
    const Player p;
    EXPECT_EQ(p.gameMode(), core::GameMode::Survival);
    EXPECT_FALSE(p.isCreative());
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) {
        EXPECT_TRUE(p.getInventorySlot(i).isEmpty()) << "slot " << i;
    }
    EXPECT_EQ(p.getHealth(), 20);
    EXPECT_FLOAT_EQ(p.getAirRatio(), 1.0f);
}

// S1.1: flying is a Creative cheat; switching to Survival lands the player.
TEST(PlayerTest, SurvivalCannotToggleFly) {
    Player p;
    EXPECT_FALSE(p.toggleFlyMode());
    EXPECT_FALSE(p.isFlying());

    p.setGameMode(core::GameMode::Creative);
    EXPECT_TRUE(p.toggleFlyMode());
    EXPECT_TRUE(p.isFlying());

    p.setGameMode(core::GameMode::Survival);
    EXPECT_FALSE(p.isFlying());
    EXPECT_FALSE(p.toggleFlyMode());
    EXPECT_FALSE(p.isFlying());

    EXPECT_EQ(core::parseGameMode("survival"), core::GameMode::Survival);
    EXPECT_EQ(core::parseGameMode("Creative"), core::GameMode::Creative);
    EXPECT_EQ(core::parseGameMode("hardcore"), std::nullopt);
}

// S1.5: physics records the fall; the 20 TPS game tick (EntitySimulation) turns it into damage.
TEST(PlayerTest, LandingRecordsPendingFallWithoutDamage) {
    test::TempDir tempDir;
    world::World w(173ULL, tempDir.path());
    w.ensureChunkLoaded({0, 0});
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t x = 0; x < 16; ++x) {
            w.setBlock(x, 100, z, world::BlockId::Stone);
            for (int32_t y = 101; y < 128; ++y) {
                w.setBlock(x, y, z, world::BlockId::Air);
            }
        }
    }

    Player p;
    p.setPosition({8.5f, 111.0f, 8.5f}); // Feet 10 blocks above the stone top (y = 101)
    for (int i = 0; i < 180 && !p.isOnGround(); ++i) {
        p.fixedUpdate(1.0f / 60.0f, {}, w);
    }
    ASSERT_TRUE(p.isOnGround());
    EXPECT_EQ(p.getHealth(), 20) << "landing no longer hurts by itself";
    const float fall = p.takePendingFall();
    EXPECT_GT(fall, 9.0f);
    EXPECT_LE(fall, 10.0f);
    EXPECT_FLOAT_EQ(p.takePendingFall(), 0.0f) << "read once, then cleared";

    // The selected-slot pick block of Survival only selects what the hotbar already holds.
    p.setInventorySlot(4, {world::BlockId::Stone, 3});
    EXPECT_FALSE(p.selectHotbarSlotHolding(gameplay::toItemId(world::BlockId::Dirt)));
    EXPECT_EQ(p.getSelectedSlot(), 0);
    EXPECT_TRUE(p.selectHotbarSlotHolding(gameplay::toItemId(world::BlockId::Stone)));
    EXPECT_EQ(p.getSelectedSlot(), 4);
    EXPECT_EQ(p.getHeldItem().count, 3);
}

namespace {

// Stone floor at y = 100 over the whole chunk, air above.
void flatStoneChunk(world::World& w) {
    w.ensureChunkLoaded({0, 0});
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t x = 0; x < 16; ++x) {
            w.setBlock(x, 100, z, world::BlockId::Stone);
            for (int32_t y = 101; y < 128; ++y) {
                w.setBlock(x, y, z, world::BlockId::Air);
            }
        }
    }
}

void settle(Player& p, const world::World& w, const PlayerInput& input = {}, int frames = 30) {
    for (int i = 0; i < frames; ++i) {
        p.fixedUpdate(1.0f / 60.0f, input, w);
    }
}

} // namespace

// S3.1: the cactus collision box is inset by 1/16, so walking into it overlaps its cell.
TEST(PlayerTest, TouchingCactusFlag) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;
    world::World w(173ULL, tempDir.path());
    flatStoneChunk(w);

    Player p;
    p.setPosition({5.5f, 101.0f, 8.5f});
    settle(p, w);
    EXPECT_FALSE(p.isTouchingCactus());

    w.setBlock(8, 101, 8, world::BlockId::Cactus);
    w.setBlock(8, 102, 8, world::BlockId::Cactus);
    settle(p, w, {}, 2);
    EXPECT_FALSE(p.isTouchingCactus()) << "two blocks away";

    // Walk +X (yaw 0) into the cactus: physics stops the player flush with the inset box.
    p.setRotation(0.0f, 0.0f);
    PlayerInput forward{};
    forward.forward = true;
    settle(p, w, forward, 90);
    EXPECT_LT(p.getAABB().max.x, 8.0f + 1.0f / 16.0f + 1e-4f) << "blocked by the inset box";
    EXPECT_GT(p.getAABB().max.x, 8.001f);
    EXPECT_TRUE(p.isTouchingCactus());

    // Merely standing next to the cell (box edge on the cell boundary) is not contact.
    p.setPosition({8.0f - 0.3f, 101.0f, 8.5f});
    settle(p, w, {}, 1);
    EXPECT_FALSE(p.isTouchingCactus());

    // Standing on top: the collision box is 1/16 short, so the feet sink into the cell.
    p.setPosition({8.5f, 104.0f, 8.5f});
    settle(p, w, {}, 60);
    EXPECT_TRUE(p.isOnGround());
    EXPECT_NEAR(p.getPosition().y, 103.0f - 1.0f / 16.0f, 0.01f);
    EXPECT_TRUE(p.isTouchingCactus()) << "standing on a cactus is contact";
}

TEST(PlayerTest, InLavaFlag) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;
    world::World w(173ULL, tempDir.path());
    flatStoneChunk(w);

    Player p;
    p.setPosition({4.5f, 101.0f, 4.5f});
    settle(p, w, {}, 1);
    EXPECT_FALSE(p.isInLava());

    w.setBlock(4, 101, 4, world::BlockId::Lava);
    settle(p, w, {}, 1);
    EXPECT_TRUE(p.isInLava());
    EXPECT_TRUE(p.isInWater()) << "lava is swum through";
    EXPECT_FALSE(p.isInWaterMaterial()) << "but does not put fire out";

    w.setBlock(4, 101, 4, world::BlockId::Water);
    settle(p, w, {}, 1);
    EXPECT_FALSE(p.isInLava());
    EXPECT_TRUE(p.isInWaterMaterial());
}

// S3.2: Beta hurtCameraEffect, -sin(f^4 * pi) * 14 with f = (hurtTime - alpha) / 10.
TEST(PlayerTest, HurtCameraRollPeaksAndDecays) {
    Player p;
    EXPECT_FLOAT_EQ(p.hurtCameraRollDeg(0.0f), 0.0f);
    EXPECT_EQ(p.hurtTime(), 0);

    p.onHurt();
    EXPECT_EQ(p.hurtTime(), Player::HURT_TIME_TICKS);
    EXPECT_NEAR(p.hurtCameraRollDeg(0.0f), 0.0f, 1e-4f) << "f = 1: sin(pi) = 0";

    // Sample the whole 10-tick curve at fine alpha steps.
    float peak = 0.0f;
    float peakT = -1.0f;
    for (int tick = 0; tick < Player::HURT_TIME_TICKS; ++tick) {
        for (int s = 0; s < 10; ++s) {
            const float alpha = static_cast<float>(s) / 10.0f;
            const float roll = p.hurtCameraRollDeg(alpha);
            EXPECT_LE(roll, 1e-5f) << "the roll is always to the same side";
            EXPECT_GE(roll, -14.0f - 1e-4f);
            if (roll < peak) {
                peak = roll;
                peakT = static_cast<float>(p.hurtTime()) - alpha;
            }
        }
        p.tickHurtTime();
    }
    EXPECT_NEAR(peak, -14.0f, 0.05f);
    // Peak where f^4 = 1/2, i.e. hurtTime - alpha = 10 * 0.5^0.25 ~ 8.41.
    EXPECT_NEAR(peakT, 8.4f, 0.11f);
    EXPECT_EQ(p.hurtTime(), 0);
    EXPECT_FLOAT_EQ(p.hurtCameraRollDeg(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(p.hurtCameraRollDeg(0.5f), 0.0f) << "negative f clamps to no roll";
    p.tickHurtTime();
    EXPECT_EQ(p.hurtTime(), 0) << "never negative";

    // Late in the window the roll decays towards 0.
    p.onHurt();
    for (int i = 0; i < 9; ++i) p.tickHurtTime();
    EXPECT_GT(p.hurtCameraRollDeg(0.0f), -0.01f);

    // The roll tilts the view matrix around the line of sight: the eye stays put.
    Player q;
    q.setTickAlpha(0.0f);
    const glm::mat4 still = q.getInterpolatedViewMatrix(1.0f);
    q.onHurt();
    q.tickHurtTime(); // hurtTime 9: f = 0.9, a clear roll
    const glm::mat4 rolled = q.getInterpolatedViewMatrix(1.0f);
    const glm::vec3 eye = q.getCameraEyePosition(1.0f);
    const glm::vec4 eyeStill = still * glm::vec4(eye, 1.0f);
    const glm::vec4 eyeRolled = rolled * glm::vec4(eye, 1.0f);
    EXPECT_NEAR(glm::length(glm::vec3(eyeRolled)), 0.0f, 1e-3f);
    EXPECT_NEAR(glm::length(glm::vec3(eyeStill)), 0.0f, 1e-3f);
    const glm::vec4 ahead = glm::vec4(eye + q.getLookDirection(), 1.0f);
    EXPECT_NEAR(glm::distance(glm::vec3(still * ahead), glm::vec3(rolled * ahead)), 0.0f, 1e-3f);
    const glm::vec4 up = glm::vec4(eye + glm::vec3(0.0f, 1.0f, 0.0f), 1.0f);
    EXPECT_GT(glm::distance(glm::vec3(still * up), glm::vec3(rolled * up)), 0.1f);
}
