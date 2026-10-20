// S6.2 arrows (game/Projectiles.hpp, Beta EntityArrow): heading and spread, gravity and drag,
// sticking in blocks and the 1200-tick lifetime, hits on mobs and on the player's proxy, the
// shooter's grace ticks and the bounce of an absorbed hit.
#include "tests/test_helpers.hpp"

#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"
#include "game/MobBehaviour.hpp"
#include "game/Projectiles.hpp"
#include "gameplay/MobDefs.hpp"
#include "voxel/Block.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

using namespace betacraft;
using namespace betacraft::ecs;
using game::ArrowPlayerHit;

namespace {

constexpr float DT = 0.05f;
constexpr int32_t kY = 120; // Open sky above the generated terrain

class ProjectilesTest : public ::testing::Test {
protected:
    void SetUp() override {
        voxel::BlockRegistry::initialize();
        m_world = std::make_unique<world::World>(173ULL, m_dir.path());
        for (int cx = -1; cx <= 1; ++cx)
            for (int cz = -1; cz <= 1; ++cz) m_world->ensureChunkLoaded({cx, cz});
    }

    // One arrow tick as EntitySimulation runs it: movement, then the arrows.
    game::ArrowTickResult step(EntityId proxy = NULL_ENTITY, const game::ArrowPlayerHitFn& hit = {}) {
        MovementPhysicsSystem::update(m_reg, DT);
        return game::tickArrows(m_reg, *m_world, proxy, hit);
    }

    test::TempDir m_dir{"betacraft_projectiles_"};
    std::unique_ptr<world::World> m_world;
    Registry m_reg;
};

} // namespace

TEST(ArrowHeading, WithoutSpreadItFliesStraightAtTheGivenSpeed) {
    gameplay::SplitMix64 rng(1);
    const glm::vec3 v = game::arrowHeading({3.0f, 0.0f, 4.0f}, 12.0f, 0.0f, rng);
    EXPECT_NEAR(glm::length(v), 12.0f, 1e-4f);
    EXPECT_NEAR(v.x, 12.0f * 0.6f, 1e-4f);
    EXPECT_NEAR(v.z, 12.0f * 0.8f, 1e-4f);
}

TEST(ArrowHeading, SpreadTwelveScattersByAboutPointZeroNine) {
    // Beta setArrowHeading: a gaussian of 0.0075 * 12 = 0.09 per axis of the unit heading.
    gameplay::SplitMix64 rng(7);
    constexpr int kN = 4000;
    double sum = 0.0;
    double sumSq = 0.0;
    for (int i = 0; i < kN; ++i) {
        const glm::vec3 v = game::arrowHeading({1.0f, 0.0f, 0.0f}, 1.0f, 12.0f, rng);
        sum += v.y;
        sumSq += static_cast<double>(v.y) * v.y;
    }
    const double mean = sum / kN;
    const double sigma = std::sqrt(sumSq / kN - mean * mean);
    EXPECT_NEAR(mean, 0.0, 0.01);
    EXPECT_NEAR(sigma, 0.09, 0.01);
}

TEST_F(ProjectilesTest, ArrowsFallWithBetaGravityAndDrag) {
    const EntityId arrow = game::spawnArrow(m_reg, {0.5f, kY + 5.0f, 0.5f}, {12.0f, 0.0f, 0.0f}, NULL_ENTITY);
    ASSERT_NE(arrow, NULL_ENTITY);
    EXPECT_FALSE(m_reg.has<AABBColliderComp>(arrow)); // The voxel collision system leaves it alone
    (void)step();
    const VelocityComp& v = m_reg.get<VelocityComp>(arrow);
    EXPECT_NEAR(m_reg.get<TransformComp>(arrow).pos.x, 0.5f + 0.6f, 1e-4f); // 12 b/s for one tick
    EXPECT_NEAR(v.linear.x, 12.0f * game::kArrowDrag, 1e-4f);
    EXPECT_NEAR(v.linear.y, -game::kArrowGravityPerTick, 1e-4f); // 0.05 b/tick^2 = 1 b/s per tick
    (void)step();
    EXPECT_NEAR(v.linear.x, 12.0f * game::kArrowDrag * game::kArrowDrag, 1e-4f);
    EXPECT_NEAR(v.linear.y, -game::kArrowGravityPerTick * (game::kArrowDrag + 1.0f), 1e-4f);
    // It faces along its flight, nose down a little.
    EXPECT_NEAR(m_reg.get<TransformComp>(arrow).yaw, 0.0f, 1e-3f);
    EXPECT_LT(m_reg.get<TransformComp>(arrow).pitch, 0.0f);
}

TEST_F(ProjectilesTest, ArrowsStickInBlocksThenVanishAfter1200Ticks) {
    ASSERT_TRUE(m_world->setBlock(4, kY, 0, voxel::BlockId::Stone));
    ASSERT_TRUE(m_world->setBlock(4, kY - 1, 0, voxel::BlockId::Stone));
    const EntityId arrow = game::spawnArrow(m_reg, {0.5f, kY + 0.5f, 0.5f}, {12.0f, 0.0f, 0.0f}, NULL_ENTITY);
    int32_t stuck = 0;
    for (int i = 0; i < 20 && stuck == 0; ++i) stuck += step().stuck;
    ASSERT_EQ(stuck, 1);
    const ArrowComp& comp = m_reg.get<ArrowComp>(arrow);
    EXPECT_TRUE(comp.inGround);
    EXPECT_EQ(comp.stuckIn.x, 4);
    EXPECT_LT(m_reg.get<TransformComp>(arrow).pos.x, 4.0f);
    EXPECT_GT(m_reg.get<TransformComp>(arrow).pos.x, 3.9f);
    const glm::vec3 restingAt = m_reg.get<TransformComp>(arrow).pos;
    for (int i = 0; i < game::kArrowGroundLifetimeTicks - 2; ++i) (void)step();
    ASSERT_TRUE(m_reg.valid(arrow));
    EXPECT_EQ(m_reg.get<TransformComp>(arrow).pos, restingAt); // Never moves while stuck
    (void)step();
    (void)step();
    EXPECT_FALSE(m_reg.valid(arrow));
}

TEST_F(ProjectilesTest, AStuckArrowFallsWhenItsBlockGoes) {
    ASSERT_TRUE(m_world->setBlock(2, kY, 0, voxel::BlockId::Stone));
    const EntityId arrow = game::spawnArrow(m_reg, {0.5f, kY + 0.5f, 0.5f}, {12.0f, 0.0f, 0.0f}, NULL_ENTITY);
    for (int i = 0; i < 20; ++i) (void)step();
    ASSERT_TRUE(m_reg.get<ArrowComp>(arrow).inGround);
    const float stuckY = m_reg.get<TransformComp>(arrow).pos.y;
    ASSERT_TRUE(m_world->setBlock(2, kY, 0, voxel::BlockId::Air));
    for (int i = 0; i < 5; ++i) (void)step();
    EXPECT_FALSE(m_reg.get<ArrowComp>(arrow).inGround);
    EXPECT_LT(m_reg.get<TransformComp>(arrow).pos.y, stuckY - 0.2f);
}

TEST_F(ProjectilesTest, ArrowsPassThroughTallGrassAndFlowers) {
    ASSERT_TRUE(m_world->setBlock(2, kY, 0, voxel::BlockId::TallGrass));
    const EntityId arrow = game::spawnArrow(m_reg, {0.5f, kY + 0.5f, 0.5f}, {12.0f, 0.0f, 0.0f}, NULL_ENTITY);
    for (int i = 0; i < 2; ++i) (void)step();
    EXPECT_FALSE(m_reg.get<ArrowComp>(arrow).inGround);
    EXPECT_GT(m_reg.get<TransformComp>(arrow).pos.x, 1.6f);
}

TEST_F(ProjectilesTest, ArrowsHurtMobsForFourAndPushThem) {
    const EntityId zombie = EntityFactory::spawnMob(m_reg, {3.5f, kY + 0.0f, 0.5f}, gameplay::MobKind::Zombie);
    const EntityId arrow = game::spawnArrow(m_reg, {0.5f, kY + 1.0f, 0.5f}, {12.0f, 0.0f, 0.0f}, NULL_ENTITY);
    std::vector<EntityId> hurt;
    for (int i = 0; i < 20 && hurt.empty(); ++i) {
        // The mob stays put: only the arrow moves (no gravity on the zombie for this test).
        m_reg.get<TransformComp>(zombie).pos = {3.5f, kY + 0.0f, 0.5f};
        m_reg.get<VelocityComp>(zombie).linear = glm::vec3(0.0f);
        hurt = step().mobsHurt;
    }
    ASSERT_EQ(hurt.size(), 1U);
    EXPECT_EQ(hurt[0], zombie);
    EXPECT_EQ(m_reg.get<HealthComp>(zombie).current, 20 - 4);
    EXPECT_FALSE(m_reg.valid(arrow)); // Spent
    EXPECT_GT(m_reg.get<VelocityComp>(zombie).linear.x, 0.0f); // Pushed along the flight
    EXPECT_GT(m_reg.get<VelocityComp>(zombie).linear.y, 0.0f);
}

TEST_F(ProjectilesTest, TheShooterIsSafeForFiveTicks) {
    // An arrow shot straight up from inside its shooter's box, then falling back on it.
    const EntityId skeleton = EntityFactory::spawnMob(m_reg, {0.5f, kY + 0.0f, 0.5f}, gameplay::MobKind::Skeleton);
    const EntityId arrow = game::spawnArrow(m_reg, {0.5f, kY + 1.4f, 0.5f}, {0.0f, 0.5f, 0.0f}, skeleton);
    std::vector<EntityId> hurt;
    for (int i = 0; i < game::kArrowShooterGraceTicks - 1; ++i) {
        m_reg.get<TransformComp>(skeleton).pos = {0.5f, kY + 0.0f, 0.5f};
        m_reg.get<VelocityComp>(skeleton).linear = glm::vec3(0.0f);
        const auto r = step();
        hurt.insert(hurt.end(), r.mobsHurt.begin(), r.mobsHurt.end());
    }
    EXPECT_TRUE(hurt.empty());
    EXPECT_EQ(m_reg.get<HealthComp>(skeleton).current, 20);
    // Past the grace ticks the same arrow can hit it.
    for (int i = 0; i < 40 && hurt.empty() && m_reg.valid(arrow); ++i) {
        m_reg.get<TransformComp>(skeleton).pos = {0.5f, kY + 0.0f, 0.5f};
        m_reg.get<VelocityComp>(skeleton).linear = glm::vec3(0.0f);
        const auto r = step();
        hurt.insert(hurt.end(), r.mobsHurt.begin(), r.mobsHurt.end());
    }
    ASSERT_EQ(hurt.size(), 1U);
    EXPECT_EQ(hurt[0], skeleton);
}

TEST_F(ProjectilesTest, TheProxyIsHitThroughTheCallback) {
    const EntityId shooter = EntityFactory::spawnMob(m_reg, {0.5f, kY + 0.0f, 0.5f}, gameplay::MobKind::Skeleton);
    const EntityId proxy = EntityFactory::spawnPlayerProxy(m_reg, {4.5f, kY + 0.0f, 0.5f});
    const EntityId arrow = game::spawnArrow(m_reg, {1.5f, kY + 1.5f, 0.5f}, {12.0f, 0.0f, 0.0f}, shooter);
    std::vector<ArrowPlayerHit> hits;
    int32_t landed = 0;
    for (int i = 0; i < 20 && hits.empty(); ++i) {
        m_reg.get<TransformComp>(shooter).pos = {0.5f, kY + 0.0f, 0.5f};
        landed += step(proxy, [&](const ArrowPlayerHit& hit) {
                      hits.push_back(hit);
                      return true;
                  }).playerHits;
    }
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(landed, 1);
    EXPECT_EQ(hits[0].arrow, arrow);
    EXPECT_EQ(hits[0].damage, 4);
    EXPECT_NEAR(hits[0].sourcePos.x, 0.5f, 1e-4f); // The shooter
    EXPECT_FALSE(m_reg.valid(arrow));
}

TEST_F(ProjectilesTest, AnAbsorbedHitBouncesTheArrowBack) {
    const EntityId proxy = EntityFactory::spawnPlayerProxy(m_reg, {4.5f, kY + 0.0f, 0.5f});
    const EntityId arrow = game::spawnArrow(m_reg, {1.5f, kY + 1.5f, 0.5f}, {12.0f, 0.0f, 0.0f}, NULL_ENTITY);
    int calls = 0;
    for (int i = 0; i < 20 && calls == 0; ++i) {
        (void)step(proxy, [&](const ArrowPlayerHit&) {
            ++calls;
            return false; // Hurt window: nothing landed
        });
    }
    ASSERT_EQ(calls, 1);
    ASSERT_TRUE(m_reg.valid(arrow));
    EXPECT_LT(m_reg.get<VelocityComp>(arrow).linear.x, 0.0f);
    EXPECT_GT(m_reg.get<VelocityComp>(arrow).linear.x, -2.0f); // A tenth of its speed
}

TEST_F(ProjectilesTest, DeadMobsAreNotHit) {
    const EntityId zombie = EntityFactory::spawnMob(m_reg, {3.5f, kY + 0.0f, 0.5f}, gameplay::MobKind::Zombie);
    m_reg.get<HealthComp>(zombie).current = 0;
    const EntityId arrow = game::spawnArrow(m_reg, {0.5f, kY + 1.0f, 0.5f}, {12.0f, 0.0f, 0.0f}, NULL_ENTITY);
    for (int i = 0; i < 8; ++i) {
        m_reg.get<TransformComp>(zombie).pos = {3.5f, kY + 0.0f, 0.5f};
        EXPECT_TRUE(step().mobsHurt.empty());
    }
    EXPECT_TRUE(m_reg.valid(arrow));
    EXPECT_GT(m_reg.get<TransformComp>(arrow).pos.x, 4.5f); // Flew through the corpse
}
