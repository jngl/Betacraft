// PathPlanningSystem + MobAISystem path following: plans when the target moves, shares
// a global node budget round-robin, forgets searches of destroyed mobs, and leaves mobs
// without a MobPathComp on straight-line steering.
#include "test_support/AiTestSupport.hpp"

#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace betacraft;
using namespace betacraft::test::ai;
using namespace betacraft::ecs;

namespace {

constexpr float DT = 0.05f;

EntityId spawnZombie(Registry& reg, const glm::vec3& feet, bool withPath = true) {
    const EntityId e = EntityFactory::spawnMob(reg, feet, gameplay::MobKind::Zombie);
    if (withPath) {
        reg.emplace<MobPathComp>(e);
    }
    return e;
}

EntityId spawnTarget(Registry& reg, const glm::vec3& pos) {
    const EntityId e = reg.create();
    reg.emplace<TransformComp>(e, TransformComp{pos, pos, 0.0f, 0.0f});
    return e;
}

void chase(Registry& reg, EntityId mob, EntityId target) {
    auto& ai = reg.get<MobAIComp>(mob);
    ai.state = MobAIState::Chase;
    ai.target = target;
}

// Moves the mob a little, as the physics would, so it never counts as stuck.
void nudge(Registry& reg, EntityId mob) {
    TransformComp& t = reg.get<TransformComp>(mob);
    t.prevPos = t.pos;
    t.pos.x += 0.001f;
    t.pos.z += 0.05f * ((reg.get<MobPathComp>(mob).repathCooldown & 1U) != 0 ? 1.0f : -1.0f);
}

} // namespace

TEST(AiPathPlanningSystem, RepathOnTargetMove) {
    const GridNavQuery grid = flatGrid(40, 5, 20);
    Registry reg;
    const EntityId mob = spawnZombie(reg, {5.5f, 1.0f, 10.5f});
    const EntityId target = spawnTarget(reg, {15.5f, 1.0f, 10.5f});
    chase(reg, mob, target);
    PathPlanningSystem planner;
    PathBudget budget{};

    planner.update(reg, grid, budget);
    const MobPathComp& path = reg.get<MobPathComp>(mob);
    EXPECT_EQ(path.goalCell, glm::ivec3(15, 1, 10));
    ASSERT_GT(path.count, 0);
    EXPECT_EQ(path.waypoints[0], glm::ivec3(6, 1, 10));
    EXPECT_EQ(path.repathCooldown, PathPlanningSystem::REPATH_COOLDOWN_TICKS);
    EXPECT_GT(budget.spent, 0);
    EXPECT_EQ(planner.activeSearches(), 0U); // short enough to finish within one tick

    // Target still: no new search, even once the cooldown is over.
    for (int i = 0; i < 30; ++i) {
        nudge(reg, mob);
        planner.update(reg, grid, budget);
        EXPECT_EQ(budget.spent, 0) << i;
    }
    // Moved by one block: not worth a new path.
    reg.get<TransformComp>(target).pos.x += 1.0f;
    nudge(reg, mob);
    planner.update(reg, grid, budget);
    EXPECT_EQ(budget.spent, 0);
    EXPECT_EQ(reg.get<MobPathComp>(mob).goalCell, glm::ivec3(15, 1, 10));
    // Moved by more than two blocks: planned again towards the new cell.
    reg.get<TransformComp>(target).pos.z += 4.0f;
    nudge(reg, mob);
    planner.update(reg, grid, budget);
    EXPECT_GT(budget.spent, 0);
    EXPECT_EQ(reg.get<MobPathComp>(mob).goalCell, glm::ivec3(16, 1, 14));
    EXPECT_EQ(reg.get<MobPathComp>(mob).index, 0);
    // ...and not again before REPATH_COOLDOWN_TICKS, however far it goes.
    reg.get<TransformComp>(target).pos.z -= 8.0f;
    for (int i = 1; i < PathPlanningSystem::REPATH_COOLDOWN_TICKS; ++i) {
        nudge(reg, mob);
        planner.update(reg, grid, budget);
        EXPECT_EQ(budget.spent, 0) << i;
    }
    nudge(reg, mob);
    planner.update(reg, grid, budget);
    EXPECT_GT(budget.spent, 0);
    EXPECT_EQ(reg.get<MobPathComp>(mob).goalCell, glm::ivec3(16, 1, 6));
}

TEST(AiPathPlanningSystem, GlobalBudgetRoundRobin) {
    GridNavQuery grid = flatGrid(64, 5, 64);
    // An unreachable goal: every search runs up to its node cap, over many ticks.
    grid.fill({49, 1, 49}, {51, 3, 51}, NavCell::Solid);
    grid.set({50, 1, 50}, NavCell::Passable);
    grid.set({50, 2, 50}, NavCell::Passable);
    Registry reg;
    const EntityId target = spawnTarget(reg, {50.5f, 1.0f, 50.5f});
    std::vector<EntityId> mobs;
    for (int i = 0; i < 20; ++i) {
        mobs.push_back(spawnZombie(reg, {2.5f + static_cast<float>(i), 1.0f, 2.5f}));
        chase(reg, mobs.back(), target);
    }
    PathPlanningSystem planner;
    const auto progressed = [&](int32_t atLeast) {
        int n = 0;
        for (const EntityId e : mobs) n += planner.searchProgress(e) >= atLeast ? 1 : 0;
        return n;
    };

    PathBudget budget{};
    planner.update(reg, grid, budget);
    EXPECT_EQ(budget.spent, 2000);
    EXPECT_EQ(planner.activeSearches(), 20U);
    EXPECT_EQ(progressed(1), 8); // 8 x 250
    EXPECT_EQ(progressed(251), 0);

    planner.update(reg, grid, budget);
    EXPECT_EQ(budget.spent, 2000);
    EXPECT_EQ(progressed(1), 16);
    EXPECT_EQ(progressed(251), 0);

    planner.update(reg, grid, budget);
    EXPECT_EQ(budget.spent, 2000);
    EXPECT_EQ(progressed(1), 20);  // the last 4, then the first 4 again
    EXPECT_EQ(progressed(251), 4);
    for (int i = 0; i < 4; ++i) EXPECT_EQ(planner.searchProgress(mobs[static_cast<size_t>(i)]), 500);

    // A smaller budget is honoured too, and nobody waits more than a few ticks.
    PathBudget small{.nodesPerTick = 600};
    planner.update(reg, grid, small);
    EXPECT_EQ(small.spent, 600);
}

TEST(AiPathPlanningSystem, DestroyedEntityDropsSearch) {
    const GridNavQuery grid = flatGrid(40, 5, 20);
    Registry reg;
    const EntityId target = spawnTarget(reg, {30.5f, 1.0f, 10.5f});
    const EntityId mob = spawnZombie(reg, {5.5f, 1.0f, 10.5f});
    const EntityId other = spawnZombie(reg, {5.5f, 1.0f, 5.5f});
    chase(reg, mob, target);
    chase(reg, other, target);
    PathPlanningSystem planner;
    PathBudget none{.nodesPerTick = 0};
    planner.update(reg, grid, none); // searches started, no node spent yet
    EXPECT_TRUE(planner.hasSearch(mob));
    EXPECT_TRUE(planner.hasSearch(other));

    ASSERT_TRUE(reg.destroy(mob));
    // The slot is recycled at once: the newcomer must not inherit the old search.
    const EntityId newcomer = spawnZombie(reg, {6.5f, 1.0f, 12.5f});
    planner.update(reg, grid, none);
    EXPECT_FALSE(planner.hasSearch(mob));
    EXPECT_TRUE(planner.hasSearch(other));
    EXPECT_FALSE(planner.hasSearch(newcomer)); // Idle: no search
    EXPECT_EQ(planner.activeSearches(), 1U);

    // A mob that stops chasing drops its search and its path.
    reg.get<MobAIComp>(other).state = MobAIState::Idle;
    planner.update(reg, grid, none);
    EXPECT_EQ(planner.activeSearches(), 0U);
    EXPECT_EQ(reg.get<MobPathComp>(other).count, 0);
}

TEST(AiPathPlanningSystem, FallbackStraightLineWithoutComp) {
    GridNavQuery grid = flatGrid(30, 5, 20);
    grid.fill({10, 1, 5}, {10, 3, 15}, NavCell::Solid); // wall between mob and target
    Registry reg;
    const EntityId target = spawnTarget(reg, {15.5f, 1.0f, 10.5f});
    const EntityId straight = spawnZombie(reg, {5.5f, 1.0f, 10.5f}, false);
    const EntityId planned = spawnZombie(reg, {5.5f, 1.0f, 10.5f});
    chase(reg, straight, target);
    chase(reg, planned, target);
    PathPlanningSystem planner;
    PathBudget budget{};
    planner.update(reg, grid, budget);
    EXPECT_FALSE(planner.hasSearch(straight));
    MobAISystem::update(reg, DT);

    // Without MobPathComp: straight at the target (+X), into the wall.
    const VelocityComp& vs = reg.get<VelocityComp>(straight);
    EXPECT_GT(vs.linear.x, 0.0f);
    EXPECT_NEAR(vs.linear.z, 0.0f, 1e-5f);

    // With a path: towards its first waypoint, which leaves the straight line to go around.
    const MobPathComp& path = reg.get<MobPathComp>(planned);
    ASSERT_GT(path.count, 0);
    const glm::ivec3 wp = path.waypoints[path.index];
    const glm::vec3 pos = reg.get<TransformComp>(planned).pos;
    const glm::vec2 expected = glm::normalize(glm::vec2(static_cast<float>(wp.x) + 0.5f - pos.x,
                                                        static_cast<float>(wp.z) + 0.5f - pos.z));
    const VelocityComp& vp = reg.get<VelocityComp>(planned);
    const glm::vec2 actual = glm::normalize(glm::vec2(vp.linear.x, vp.linear.z));
    EXPECT_NEAR(actual.x, expected.x, 1e-4f);
    EXPECT_NEAR(actual.y, expected.y, 1e-4f);
    bool leavesLine = false;
    for (uint8_t i = 0; i < path.count; ++i) leavesLine = leavesLine || path.waypoints[i].z != 10;
    EXPECT_TRUE(leavesLine);

    // A waypoint one block higher makes a grounded mob jump.
    MobPathComp& p = reg.get<MobPathComp>(planned);
    p.waypoints[0] = glm::ivec3(7, 2, 10);
    p.count = 1;
    p.index = 0;
    reg.get<VelocityComp>(planned).onGround = true;
    reg.get<VelocityComp>(planned).linear.y = 0.0f;
    MobAISystem::update(reg, DT);
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(planned).linear.y, MobAISystem::JUMP_SPEED);
}

// Each mob kind plans with its own box (Beta sizes) and nobody opens doors.
TEST(AiPathPlanningSystem, MobDefsGiveNavExtents) {
    const gameplay::MobDef& zombie = gameplay::mobDef(gameplay::MobKind::Zombie);
    const gameplay::MobDef& spider = gameplay::mobDef(gameplay::MobKind::Spider);
    EXPECT_EQ(zombie.navExtents, glm::vec3(0.6f, 1.8f, 0.6f));
    EXPECT_EQ(spider.navExtents, glm::vec3(1.4f, 0.9f, 1.4f));
    EXPECT_EQ(ai::navFootprint(zombie.navExtents), glm::ivec3(1, 2, 1));
    EXPECT_EQ(ai::navFootprint(spider.navExtents), glm::ivec3(2, 1, 2));
    for (int k = 0; k < static_cast<int>(gameplay::MobKind::Count); ++k) {
        const gameplay::MobDef& def = gameplay::mobDef(static_cast<gameplay::MobKind>(k));
        EXPECT_FALSE(def.canOpenDoors) << def.name;
        EXPECT_FLOAT_EQ(def.navExtents.x, def.width) << def.name;
        EXPECT_FLOAT_EQ(def.navExtents.y, def.height) << def.name;
    }
    Registry reg;
    const EntityId s = EntityFactory::spawnMob(reg, {0.0f, 1.0f, 0.0f}, gameplay::MobKind::Spider);
    const ai::NavAgent agent = PathPlanningSystem::agentOf(reg, s);
    EXPECT_EQ(agent.extents, spider.navExtents);
    EXPECT_TRUE(agent.climbsWalls);
    EXPECT_FALSE(agent.canOpenDoors);
    // A 2-wide body centred at x = 5.0 has its footprint corner at cell 4.
    EXPECT_EQ(PathPlanningSystem::cornerCell({5.0f, 1.0f, 5.0f}, {2, 1, 2}), glm::ivec3(4, 1, 4));
    EXPECT_EQ(PathPlanningSystem::cornerCell({5.3f, 1.0f, 5.7f}, {1, 2, 1}), glm::ivec3(5, 1, 5));
}
