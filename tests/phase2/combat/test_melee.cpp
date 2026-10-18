#include "gameplay/MeleeCombat.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <random>

using namespace betacraft::gameplay;
using betacraft::physics::AABB;
using betacraft::world::BlockId;

namespace {
ItemStack tool(ToolType type, ToolMaterial mat) {
    return makeStack(toolItemId(type, mat), 1);
}

AttackerState attackerFacingPlusX() {
    AttackerState a{};
    a.eyePosition = {0.0f, 65.62f, 0.0f};
    a.look = {1.0f, 0.0f, 0.0f};
    a.verticalVelocity = 0.0f;
    return a;
}

TargetState zombieAt(float x) {
    TargetState t{};
    t.health = makeHealth(20);
    t.position = {x, 64.0f, 0.0f};
    t.velocity = {0.0f, 0.0f, 0.0f};
    return t;
}
} // namespace

// ---------------------------------------------------------------------------
// performMeleeAttack
// ---------------------------------------------------------------------------

TEST(MeleeAttack, FistHitDealsOneAndKnocksBack) {
    ItemStack hand{};
    TargetState target = zombieAt(2.0f);
    const MeleeResult r = performMeleeAttack(attackerFacingPlusX(), hand, target);
    EXPECT_EQ(r.damage, 1);
    EXPECT_EQ(r.hit.outcome, HitOutcome::Full);
    EXPECT_EQ(target.health.current, 19);
    EXPECT_EQ(r.weaponWear, WearResult::Unchanged);
    EXPECT_NEAR(r.impulse.x, 8.5f, 1e-4f);
    EXPECT_NEAR(target.velocity.x, 8.5f, 1e-4f);
    EXPECT_NEAR(target.velocity.y, 4.2f, 1e-4f);
}

TEST(MeleeAttack, DiamondSwordTwoHitsKillsZombieAcrossWindows) {
    ItemStack sword = tool(ToolType::Sword, ToolMaterial::Diamond);
    TargetState target = zombieAt(2.0f);
    const MeleeResult first = performMeleeAttack(attackerFacingPlusX(), sword, target);
    EXPECT_EQ(first.damage, 10);
    EXPECT_EQ(target.health.current, 10);
    EXPECT_EQ(sword.damage, 1);

    // Spam-click inside the resistance window: absorbed, but the sword still wears (Beta).
    const MeleeResult spam = performMeleeAttack(attackerFacingPlusX(), sword, target);
    EXPECT_EQ(spam.hit.outcome, HitOutcome::Absorbed);
    EXPECT_EQ(target.health.current, 10);
    EXPECT_EQ(sword.damage, 2);
    EXPECT_FLOAT_EQ(glm::length(spam.impulse), 0.0f);

    for (int i = 0; i < kHurtResistanceTicks; ++i) {
        tickHealth(target.health);
    }
    const MeleeResult second = performMeleeAttack(attackerFacingPlusX(), sword, target);
    EXPECT_TRUE(second.hit.killed);
    EXPECT_TRUE(target.health.isDead());
}

TEST(MeleeAttack, AbsorbedHitDoesNotChangeTargetVelocity) {
    ItemStack sword = tool(ToolType::Sword, ToolMaterial::Wood);
    TargetState target = zombieAt(2.0f);
    (void)performMeleeAttack(attackerFacingPlusX(), sword, target);
    target.velocity = {1.0f, -2.0f, 3.0f};
    (void)performMeleeAttack(attackerFacingPlusX(), sword, target);
    EXPECT_EQ(target.velocity, glm::vec3(1.0f, -2.0f, 3.0f));
}

TEST(MeleeAttack, PartialHitHasNoKnockback) {
    ItemStack hand{};
    TargetState target = zombieAt(2.0f);
    (void)performMeleeAttack(attackerFacingPlusX(), hand, target); // 1 damage, window opens
    target.velocity = glm::vec3{0.0f};
    ItemStack sword = tool(ToolType::Sword, ToolMaterial::Iron);
    const MeleeResult r = performMeleeAttack(attackerFacingPlusX(), sword, target);
    EXPECT_EQ(r.hit.outcome, HitOutcome::Partial);
    EXPECT_EQ(r.hit.applied, 7);
    EXPECT_EQ(target.velocity, glm::vec3{0.0f});
}

TEST(MeleeAttack, FallingAttackerDealsBonusDamage) {
    ItemStack sword = tool(ToolType::Sword, ToolMaterial::Stone);
    TargetState target = zombieAt(2.0f);
    AttackerState a = attackerFacingPlusX();
    a.verticalVelocity = -3.0f;
    const MeleeResult r = performMeleeAttack(a, sword, target);
    EXPECT_EQ(r.damage, 7);
    EXPECT_TRUE(r.critical);
    EXPECT_EQ(target.health.current, 13);
}

TEST(MeleeAttack, PickaxeAsWeaponWearsTwo) {
    ItemStack pick = tool(ToolType::Pickaxe, ToolMaterial::Iron);
    TargetState target = zombieAt(2.0f);
    const MeleeResult r = performMeleeAttack(attackerFacingPlusX(), pick, target);
    EXPECT_EQ(r.damage, 4);
    EXPECT_EQ(pick.damage, 2);
    EXPECT_EQ(r.weaponWear, WearResult::Worn);
}

TEST(MeleeAttack, WeaponBreaksMidFight) {
    ItemStack sword = tool(ToolType::Sword, ToolMaterial::Gold);
    sword.damage = sword.maxDamage; // one use left
    TargetState target = zombieAt(2.0f);
    const MeleeResult r = performMeleeAttack(attackerFacingPlusX(), sword, target);
    EXPECT_EQ(r.damage, 4) << "the breaking swing still uses the sword's damage";
    EXPECT_EQ(r.weaponWear, WearResult::Broken);
    EXPECT_TRUE(sword.empty());
}

TEST(MeleeAttack, HittingADyingTargetStillWearsTheWeapon) {
    // Beta: a mob stays targetable during its 20-tick death animation (canBeCollidedWith is
    // !isDead), attackEntityFrom returns false on health <= 0, but
    // attackTargetEntityWithCurrentItem calls itemstack.hitEntity regardless.
    ItemStack sword = tool(ToolType::Sword, ToolMaterial::Iron);
    TargetState target = zombieAt(2.0f);
    (void)applyDamage(target.health, 100);
    ASSERT_FALSE(isCorpseExpired(target.health));
    const MeleeResult r = performMeleeAttack(attackerFacingPlusX(), sword, target);
    EXPECT_EQ(r.hit.outcome, HitOutcome::Ignored);
    EXPECT_EQ(r.hit.applied, 0);
    EXPECT_FALSE(r.hit.killed);
    EXPECT_EQ(r.impulse, glm::vec3(0.0f)) << "no knockback on a corpse";
    EXPECT_EQ(target.velocity, glm::vec3(0.0f));
    EXPECT_EQ(r.weaponWear, WearResult::Worn);
    EXPECT_EQ(sword.damage, 1);
}

TEST(MeleeAttack, ExpiredCorpseIsNotAttackable) {
    // Once deathTime reaches 20 Beta sets isDead and the entity can no longer be targeted.
    ItemStack sword = tool(ToolType::Sword, ToolMaterial::Iron);
    TargetState target = zombieAt(2.0f);
    (void)applyDamage(target.health, 100);
    for (int i = 0; i < kDeathAnimationTicks; ++i) {
        tickHealth(target.health);
    }
    ASSERT_TRUE(isCorpseExpired(target.health));
    const MeleeResult r = performMeleeAttack(attackerFacingPlusX(), sword, target);
    EXPECT_EQ(r.hit.outcome, HitOutcome::Ignored);
    EXPECT_EQ(r.weaponWear, WearResult::Unchanged);
    EXPECT_EQ(sword.damage, 0);
}

TEST(MeleeAttack, KnockbackFollowsLookNotPosition) {
    // Roadmap formula: push along the attacker's look heading.
    AttackerState a = attackerFacingPlusX();
    a.look = glm::normalize(glm::vec3{0.0f, -0.3f, -1.0f});
    TargetState target = zombieAt(2.0f);
    ItemStack hand{};
    const MeleeResult r = performMeleeAttack(a, hand, target);
    EXPECT_NEAR(r.impulse.z, -8.5f, 1e-3f);
    EXPECT_NEAR(r.impulse.x, 0.0f, 1e-3f);
}

TEST(MeleeAttack, PropertyRandomFightsKeepInvariants) {
    std::mt19937 rng(424242u);
    std::uniform_int_distribution<int> pickType(0, 5);
    std::uniform_int_distribution<int> pickMat(0, 4);
    std::uniform_int_distribution<int> pickTicks(0, 15);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (int fight = 0; fight < 300; ++fight) {
        const int typeIdx = pickType(rng);
        ItemStack weapon = typeIdx == 0 ? ItemStack{}
                                        : tool(static_cast<ToolType>(typeIdx), static_cast<ToolMaterial>(pickMat(rng)));
        TargetState target = zombieAt(2.0f);
        int swings = 0;
        while (!target.health.isDead() && swings < 2000) {
            AttackerState a = attackerFacingPlusX();
            a.look = {u(rng), u(rng), u(rng)};
            a.verticalVelocity = u(rng);
            const int32_t before = target.health.current;
            const int32_t wearBefore = weapon.damage;
            const bool hadWeapon = !weapon.empty();
            const MeleeResult r = performMeleeAttack(a, weapon, target);
            ++swings;
            ASSERT_GE(r.damage, 1);
            ASSERT_LE(r.hit.applied, r.damage);
            ASSERT_EQ(before - target.health.current, r.hit.applied);
            ASSERT_TRUE(std::isfinite(target.velocity.x) && std::isfinite(target.velocity.y) &&
                        std::isfinite(target.velocity.z));
            ASSERT_LE(target.velocity.y, 4.2f + 1e-4f);
            if (hadWeapon && r.weaponWear == WearResult::Worn) {
                ASSERT_GT(weapon.damage, wearBefore);
            }
            for (int t = pickTicks(rng); t > 0; --t) {
                tickHealth(target.health);
            }
        }
        EXPECT_TRUE(target.health.isDead()) << "every weapon eventually kills (fight " << fight << ")";
    }
}

// ---------------------------------------------------------------------------
// Target picking: ray vs AABB within reach
// ---------------------------------------------------------------------------

TEST(RayAabb, HitsFrontFace) {
    const AABB box{{2.0f, 0.0f, -0.5f}, {3.0f, 2.0f, 0.5f}};
    const auto t = rayAabbDistance({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, box);
    ASSERT_TRUE(t.has_value());
    EXPECT_NEAR(*t, 2.0f, 1e-5f);
}

TEST(RayAabb, MissesAndBehind) {
    const AABB box{{2.0f, 0.0f, -0.5f}, {3.0f, 2.0f, 0.5f}};
    EXPECT_FALSE(rayAabbDistance({0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, box).has_value());
    EXPECT_FALSE(rayAabbDistance({0.0f, 5.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, box).has_value());
}

TEST(RayAabb, OriginInsideReturnsZero) {
    const AABB box{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
    const auto t = rayAabbDistance({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, box);
    ASSERT_TRUE(t.has_value());
    EXPECT_FLOAT_EQ(*t, 0.0f);
}

TEST(RayAabb, AxisParallelRayOutsideSlabMisses) {
    const AABB box{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    EXPECT_FALSE(rayAabbDistance({2.0f, 0.5f, -5.0f}, {0.0f, 0.0f, 1.0f}, box).has_value());
    EXPECT_TRUE(rayAabbDistance({0.5f, 0.5f, -5.0f}, {0.0f, 0.0f, 1.0f}, box).has_value());
}

TEST(RayAabb, DegenerateDirectionMisses) {
    const AABB box{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    EXPECT_FALSE(rayAabbDistance({-1.0f, 0.5f, 0.5f}, {0.0f, 0.0f, 0.0f}, box).has_value());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(rayAabbDistance({-1.0f, 0.5f, 0.5f}, {nan, 0.0f, 0.0f}, box).has_value());
}

TEST(PickTarget, NearestWithinReach) {
    const std::array<AABB, 3> boxes{{
        {{5.0f, 0.0f, -0.3f}, {5.6f, 1.8f, 0.3f}}, // too far
        {{2.0f, 0.0f, -0.3f}, {2.6f, 1.8f, 0.3f}}, // nearest
        {{2.5f, 0.0f, -0.3f}, {3.1f, 1.8f, 0.3f}}, // behind the nearest
    }};
    const auto hit = pickMeleeTarget({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, boxes);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->index, 1u);
    EXPECT_NEAR(hit->distance, 2.0f, 1e-5f);
}

TEST(PickTarget, RespectsReachBoundary) {
    const std::array<AABB, 1> boxes{{{{kDefaultMeleeReach, 0.0f, -0.3f}, {kDefaultMeleeReach + 0.6f, 1.8f, 0.3f}}}};
    EXPECT_TRUE(pickMeleeTarget({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, boxes).has_value()) << "inclusive";
    EXPECT_FALSE(pickMeleeTarget({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, boxes, kDefaultMeleeReach - 0.01f).has_value());
}

TEST(PickTarget, NonNormalisedLookStillMeasuresMetres) {
    const std::array<AABB, 1> boxes{{{{2.0f, 0.0f, -0.3f}, {2.6f, 1.8f, 0.3f}}}};
    const auto hit = pickMeleeTarget({0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, boxes);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 2.0f, 1e-5f);
}

TEST(PickTarget, NaNMaxDistanceRejectsEverything) {
    // e.g. min(reach, NaN distance from a broken block raycast): must not mean "unlimited".
    const std::array<AABB, 2> boxes{AABB{{1.0f, -0.5f, -0.5f}, {2.0f, 0.5f, 0.5f}},
                                    AABB{{500.0f, -0.5f, -0.5f}, {501.0f, 0.5f, 0.5f}}};
    EXPECT_FALSE(pickMeleeTarget({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, boxes,
                                 std::numeric_limits<float>::quiet_NaN())
                     .has_value());
}

TEST(PickTarget, NegativeMaxDistanceRejectsEverything) {
    const std::array<AABB, 1> boxes{AABB{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}};
    EXPECT_FALSE(pickMeleeTarget({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, boxes, -1.0f).has_value());
}

TEST(PickTarget, EmptyCandidates) {
    EXPECT_FALSE(pickMeleeTarget({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, std::span<const AABB>{}).has_value());
}

TEST(PickTarget, BlockInFrontOccludesEntity) {
    // The caller passes the distance of the first solid block hit; entities behind it are skipped.
    const std::array<AABB, 1> boxes{{{{2.0f, 0.0f, -0.3f}, {2.6f, 1.8f, 0.3f}}}};
    EXPECT_FALSE(pickMeleeTarget({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, boxes, 1.5f).has_value());
}

TEST(MeleeAttack, BetaDirectionModeIsForwardedToKnockback) {
    // Mob attacks (or Beta-faithful player hits) push away from the attacker's position.
    AttackerState a = attackerFacingPlusX();
    a.look = {0.0f, 0.0f, 1.0f}; // Looking sideways relative to the target
    TargetState target = zombieAt(2.0f);
    ItemStack hand{};
    KnockbackParams beta{};
    beta.direction = KnockbackDirection::AttackerToTarget;
    const MeleeResult r = performMeleeAttack(a, hand, target, beta);
    EXPECT_NEAR(r.impulse.x, 8.5f, 1e-3f);
    EXPECT_NEAR(r.impulse.z, 0.0f, 1e-3f);
    EXPECT_GT(target.velocity.x, 0.0f);
}

TEST(MeleeAttack, KillingBlowReportsDeathAndStillKnocksBack) {
    AttackerState a = attackerFacingPlusX();
    TargetState target = zombieAt(2.0f);
    target.health.current = 3;
    ItemStack sword = tool(ToolType::Sword, ToolMaterial::Iron);
    const MeleeResult r = performMeleeAttack(a, sword, target);
    EXPECT_TRUE(r.hit.killed);
    EXPECT_EQ(r.hit.applied, 3) << "overkill is clamped to the remaining health";
    EXPECT_TRUE(target.health.isDead());
    EXPECT_GT(r.impulse.x, 0.0f) << "Beta knocks the corpse back too (flag stays true)";
    EXPECT_EQ(sword.damage, 1);
}
