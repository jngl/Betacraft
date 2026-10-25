#include "gameplay/Knockback.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <random>

using namespace betacraft::gameplay;

namespace {
float horizontalLength(const glm::vec3& v) {
    return std::sqrt(v.x * v.x + v.z * v.z);
}
bool isFinite(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
const glm::vec3 kOrigin{0.0f, 64.0f, 0.0f};
} // namespace

TEST(Knockback, DefaultParams) {
    const KnockbackParams p{};
    EXPECT_FLOAT_EQ(p.horizontalSpeed, 8.5f);
    EXPECT_FLOAT_EQ(p.verticalSpeed, 4.2f);
    EXPECT_FLOAT_EQ(p.velocityRetention, 0.5f);
}

TEST(Knockback, AlongLookDirection) {
    const glm::vec3 impulse = computeKnockbackImpulse({1.0f, 0.0f, 0.0f}, kOrigin, kOrigin + glm::vec3{2, 0, 0});
    EXPECT_NEAR(impulse.x, 8.5f, 1e-5f);
    EXPECT_NEAR(impulse.y, 4.2f, 1e-5f);
    EXPECT_NEAR(impulse.z, 0.0f, 1e-5f);
}

TEST(Knockback, PitchDoesNotChangeTheImpulse) {
    // Only look.xz is used; looking down at a short mob must not reduce or redirect lift.
    const glm::vec3 a = computeKnockbackImpulse(glm::normalize(glm::vec3{0.6f, -0.5f, 0.6f}), kOrigin, kOrigin);
    const glm::vec3 b = computeKnockbackImpulse(glm::normalize(glm::vec3{0.6f, 0.7f, 0.6f}), kOrigin, kOrigin);
    EXPECT_NEAR(a.x, 8.5f / std::sqrt(2.0f), 1e-4f);
    EXPECT_NEAR(a.z, 8.5f / std::sqrt(2.0f), 1e-4f);
    EXPECT_NEAR(a.y, 4.2f, 1e-5f);
    EXPECT_NEAR(glm::length(a - b), 0.0f, 1e-5f);
}

TEST(Knockback, VerticalLookFallsBackToAttackerToTarget) {
    const glm::vec3 target = kOrigin + glm::vec3{0.0f, -1.0f, -3.0f};
    const glm::vec3 impulse = computeKnockbackImpulse({0.0f, -1.0f, 0.0f}, kOrigin, target);
    EXPECT_NEAR(impulse.x, 0.0f, 1e-5f);
    EXPECT_NEAR(impulse.z, -8.5f, 1e-4f) << "pushed away from the attacker";
    EXPECT_NEAR(impulse.y, 4.2f, 1e-5f);
}

TEST(Knockback, FullyDegenerateUsesDeterministicFallback) {
    const glm::vec3 a = computeKnockbackImpulse({0.0f, 1.0f, 0.0f}, kOrigin, kOrigin);
    const glm::vec3 b = computeKnockbackImpulse({0.0f, 1.0f, 0.0f}, kOrigin, kOrigin);
    EXPECT_TRUE(isFinite(a));
    EXPECT_NEAR(horizontalLength(a), 8.5f, 1e-4f);
    EXPECT_EQ(a, b);
}

TEST(Knockback, NonFiniteInputsNeverProduceNaN) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const glm::vec3 a = computeKnockbackImpulse({nan, 0.0f, nan}, kOrigin, kOrigin + glm::vec3{1, 0, 0});
    EXPECT_TRUE(isFinite(a));
    EXPECT_NEAR(a.x, 8.5f, 1e-4f) << "NaN look falls back to the positional direction";
    const glm::vec3 b = computeKnockbackImpulse({inf, 0.0f, 1.0f}, {nan, nan, nan}, kOrigin);
    EXPECT_TRUE(isFinite(b));
    EXPECT_NEAR(horizontalLength(b), 8.5f, 1e-4f);
}

TEST(Knockback, CustomParams) {
    const KnockbackParams weak{.horizontalSpeed = 2.0f, .verticalSpeed = 1.0f, .velocityRetention = 0.0f};
    const glm::vec3 impulse = computeKnockbackImpulse({0.0f, 0.0f, 1.0f}, kOrigin, kOrigin, weak);
    EXPECT_NEAR(impulse.z, 2.0f, 1e-5f);
    EXPECT_NEAR(impulse.y, 1.0f, 1e-5f);
}

TEST(Knockback, PropertyHorizontalMagnitudeIsConstant) {
    std::mt19937 rng(7u);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (int i = 0; i < 2000; ++i) {
        const glm::vec3 look{u(rng), u(rng), u(rng)};
        const glm::vec3 attacker{u(rng) * 100.0f, 64.0f, u(rng) * 100.0f};
        const glm::vec3 target = attacker + glm::vec3{u(rng) * 3.0f, u(rng), u(rng) * 3.0f};
        const glm::vec3 impulse = computeKnockbackImpulse(look, attacker, target);
        ASSERT_TRUE(isFinite(impulse));
        ASSERT_NEAR(horizontalLength(impulse), 8.5f, 1e-3f);
        ASSERT_NEAR(impulse.y, 4.2f, 1e-5f);
        const float lookXZ = std::sqrt(look.x * look.x + look.z * look.z);
        if (lookXZ > 1e-3f) {
            // Same heading as the attacker's look.
            ASSERT_GT(impulse.x * look.x + impulse.z * look.z, 0.0f);
        }
    }
}

TEST(KnockbackApply, HalvesExistingVelocityThenAdds) {
    // Beta knockBack: motion /= 2 then add the impulse.
    const glm::vec3 v = applyKnockback({2.0f, -10.0f, 4.0f}, {8.5f, 4.2f, 0.0f});
    EXPECT_NEAR(v.x, 9.5f, 1e-5f);
    EXPECT_NEAR(v.y, -0.8f, 1e-5f);
    EXPECT_NEAR(v.z, 2.0f, 1e-5f);
}

TEST(KnockbackApply, VerticalSpeedIsCapped) {
    // Beta caps motionY at the lift value so repeated knockback cannot launch an entity.
    const glm::vec3 v = applyKnockback({0.0f, 10.0f, 0.0f}, {8.5f, 4.2f, 0.0f});
    EXPECT_NEAR(v.y, 4.2f, 1e-5f);
}

TEST(KnockbackApply, RepeatedKnockbackConverges) {
    glm::vec3 v{0.0f};
    const glm::vec3 impulse{8.5f, 4.2f, 0.0f};
    for (int i = 0; i < 64; ++i) {
        v = applyKnockback(v, impulse);
        ASSERT_LE(v.y, 4.2f + 1e-5f);
        ASSERT_LE(v.x, 17.0f + 1e-4f) << "geometric series bounded by impulse / (1 - 0.5)";
    }
    EXPECT_NEAR(v.x, 17.0f, 1e-3f);
}

TEST(KnockbackApply, NonFiniteVelocityIsSanitised) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const glm::vec3 v = applyKnockback({nan, nan, nan}, {8.5f, 4.2f, 0.0f});
    EXPECT_TRUE(isFinite(v));
    EXPECT_NEAR(v.x, 8.5f, 1e-5f);
}

TEST(Knockback, AttackerToTargetModeIgnoresLook) {
    KnockbackParams beta{};
    beta.direction = KnockbackDirection::AttackerToTarget;
    // Attacker looks along +X but the target stands at -Z: Beta pushes it further along -Z.
    const glm::vec3 impulse =
        computeKnockbackImpulse({1.0f, 0.0f, 0.0f}, kOrigin, kOrigin + glm::vec3{0.0f, 0.5f, -2.0f}, beta);
    EXPECT_NEAR(impulse.x, 0.0f, 1e-5f);
    EXPECT_NEAR(impulse.z, -8.5f, 1e-4f);
    EXPECT_NEAR(impulse.y, 4.2f, 1e-5f);
}

TEST(Knockback, AttackerToTargetModeFallsBackToLookWhenStacked) {
    KnockbackParams beta{};
    beta.direction = KnockbackDirection::AttackerToTarget;
    // Same column: no positional heading, so the look direction is the next best guess.
    const glm::vec3 impulse = computeKnockbackImpulse({0.0f, 0.0f, -1.0f}, kOrigin, kOrigin + glm::vec3{0, 3, 0}, beta);
    EXPECT_NEAR(impulse.z, -8.5f, 1e-4f);
    const glm::vec3 degenerate = computeKnockbackImpulse({0.0f, 1.0f, 0.0f}, kOrigin, kOrigin, beta);
    EXPECT_TRUE(isFinite(degenerate));
    EXPECT_NEAR(horizontalLength(degenerate), 8.5f, 1e-4f);
}

TEST(Knockback, PropertyAttackerToTargetAlwaysIncreasesSeparation) {
    std::mt19937 rng(1337u);
    std::uniform_real_distribution<float> d(-20.0f, 20.0f);
    KnockbackParams beta{};
    beta.direction = KnockbackDirection::AttackerToTarget;
    for (int i = 0; i < 2000; ++i) {
        const glm::vec3 attacker{d(rng), 64.0f + d(rng), d(rng)};
        const glm::vec3 target = attacker + glm::vec3{d(rng), d(rng), d(rng)};
        const glm::vec3 look{d(rng), d(rng), d(rng)};
        const glm::vec3 impulse = computeKnockbackImpulse(look, attacker, target, beta);
        const glm::vec2 sep{target.x - attacker.x, target.z - attacker.z};
        if (glm::length(sep) < 1e-3f) {
            continue;
        }
        EXPECT_GT(glm::dot(glm::vec2{impulse.x, impulse.z}, sep), 0.0f) << i;
        EXPECT_NEAR(horizontalLength(impulse), 8.5f, 1e-3f) << i;
    }
}
