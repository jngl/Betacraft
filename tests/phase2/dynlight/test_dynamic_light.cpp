// P4.6 — DynamicLight struct, emitter lookup and CPU reference evaluation.
#include "lighting/DynamicLight.hpp"
#include "lighting/DynamicLightManager.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <random>

using namespace betacraft::lighting;
using betacraft::voxel::BlockId;

namespace {
DynamicLight makeLight(glm::vec3 p, float r, glm::vec3 c = glm::vec3(1.0f), float i = 1.0f) {
    DynamicLight l;
    l.position = p;
    l.radius = r;
    l.color = c;
    l.intensity = i;
    return l;
}
} // namespace

TEST(DynLightValidity, NominalLightIsValid) {
    EXPECT_TRUE(makeLight({1, 2, 3}, 14.0f).isValid());
}

TEST(DynLightValidity, DefaultLightIsInvalidBecauseZeroRadius) {
    EXPECT_FALSE(DynamicLight{}.isValid());
}

TEST(DynLightValidity, RejectsNonPositiveRadiusOrIntensity) {
    EXPECT_FALSE(makeLight({0, 0, 0}, -1.0f).isValid());
    EXPECT_FALSE(makeLight({0, 0, 0}, 5.0f, glm::vec3(1.0f), 0.0f).isValid());
    EXPECT_FALSE(makeLight({0, 0, 0}, 5.0f, glm::vec3(1.0f), -2.0f).isValid());
}

TEST(DynLightValidity, RejectsBlackColor) {
    EXPECT_FALSE(makeLight({0, 0, 0}, 5.0f, glm::vec3(0.0f)).isValid());
}

TEST(DynLightValidity, RejectsNonFiniteFields) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(makeLight({nan, 0, 0}, 5.0f).isValid());
    EXPECT_FALSE(makeLight({0, inf, 0}, 5.0f).isValid());
    EXPECT_FALSE(makeLight({0, 0, 0}, inf).isValid());
    EXPECT_FALSE(makeLight({0, 0, 0}, nan).isValid());
    EXPECT_FALSE(makeLight({0, 0, 0}, 5.0f, glm::vec3(nan, 1, 1)).isValid());
    EXPECT_FALSE(makeLight({0, 0, 0}, 5.0f, glm::vec3(1.0f), nan).isValid());
}

TEST(DynLightValidity, RejectsNegativeColorChannel) {
    EXPECT_FALSE(makeLight({0, 0, 0}, 5.0f, glm::vec3(1.0f, -0.1f, 1.0f)).isValid());
}

// --- Emitters ---------------------------------------------------------------

TEST(DynLightEmitters, EmissionReadFromBlockRegistry) {
    EXPECT_EQ(emissionOf(BlockId::Torch), 14);
    EXPECT_EQ(emissionOf(BlockId::Glowstone), 15);
    EXPECT_EQ(emissionOf(BlockId::Stone), 0);
    EXPECT_EQ(emissionOf(BlockId::Air), 0);
    EXPECT_EQ(emissionOf(BlockId::DiamondPickaxe), 0);
}

TEST(DynLightEmitters, HeldRedstoneOreDoesNotGlowLikeBeta) {
    // Beta 1.7.3: unlit redstone ore (id 73) emits 0; only the transient glowing ore block
    // (id 74, never an inventory item) emits light. The registry's static-world value of 4
    // must not turn a held or dropped ore into a light source.
    EXPECT_EQ(emissionOf(BlockId::RedstoneOre), 0);
    EXPECT_FALSE(makeEmitterLight(BlockId::RedstoneOre, {0, 0, 0}, LightSource::HeldItem).has_value());
    DynamicLightManager m;
    EXPECT_FALSE(m.submitHeldItem({0, 0, 0}, {0, 0, -1}, BlockId::RedstoneOre));
    EXPECT_FALSE(m.submitDroppedItem({0, 0, 0}, BlockId::RedstoneOre));
}

TEST(DynLightEmitters, EveryRegisteredIdMatchesBetaItemEmission) {
    // Only torches (14), glowstone (15) and the lit redstone torch (7, S8.5) glow as items
    // among this engine's ids.
    for (int raw = 0; raw < 256; ++raw) {
        const auto id = static_cast<BlockId>(raw);
        const uint8_t expected = id == BlockId::Torch             ? 14
                               : id == BlockId::Glowstone         ? 15
                               : id == BlockId::RedstoneTorchOn ? 7
                                                                  : 0;
        ASSERT_EQ(emissionOf(id), expected) << "block id " << raw;
    }
}

TEST(DynLightEmitters, TorchLightIsWarmAndUsesBlockLightRadius) {
    const auto light = makeEmitterLight(BlockId::Torch, {10, 64, -3}, LightSource::HeldItem);
    ASSERT_TRUE(light.has_value());
    EXPECT_TRUE(light->isValid());
    EXPECT_FLOAT_EQ(light->radius, 14.0f);
    EXPECT_EQ(light->position, glm::vec3(10, 64, -3));
    EXPECT_EQ(light->source, LightSource::HeldItem);
    // Warm: red >= green >= blue, same tint as the chunk shader's torch light.
    EXPECT_FLOAT_EQ(light->color.r, 1.0f);
    EXPECT_FLOAT_EQ(light->color.g, 0.94f);
    EXPECT_FLOAT_EQ(light->color.b, 0.82f);
    EXPECT_FLOAT_EQ(light->intensity, 1.0f);
}

TEST(DynLightEmitters, GlowstoneHasFullRadius) {
    const auto light = makeEmitterLight(BlockId::Glowstone, {0, 0, 0}, LightSource::DroppedItem);
    ASSERT_TRUE(light.has_value());
    EXPECT_FLOAT_EQ(light->radius, 15.0f);
    EXPECT_EQ(light->source, LightSource::DroppedItem);
    EXPECT_GT(light->color.r, light->color.b);
}

TEST(DynLightEmitters, NonGlowingItemProducesNoLight) {
    EXPECT_FALSE(makeEmitterLight(BlockId::Stone, {0, 0, 0}, LightSource::HeldItem).has_value());
    EXPECT_FALSE(makeEmitterLight(BlockId::Air, {0, 0, 0}, LightSource::HeldItem).has_value());
    EXPECT_FALSE(makeEmitterLight(BlockId::Stick, {0, 0, 0}, LightSource::HeldItem).has_value());
}

TEST(DynLightEmitters, ColorsAreInUnitRange) {
    for (int i = 0; i < static_cast<int>(BlockId::Count); ++i) {
        const glm::vec3 c = emitterColor(static_cast<BlockId>(i));
        for (int k = 0; k < 3; ++k) {
            ASSERT_GE(c[k], 0.0f);
            ASSERT_LE(c[k], 1.0f);
        }
    }
}

// --- CPU reference evaluation --------------------------------------------------

TEST(DynLightEvaluate, NoLightsGivesBlack) {
    EXPECT_EQ(evaluateDynamicLight({0, 0, 0}, {}), glm::vec3(0.0f));
}

TEST(DynLightEvaluate, SingleLightFollowsSpecFormula) {
    const std::array lights{makeLight({0, 0, 0}, 10.0f, {1.0f, 0.5f, 0.25f}, 2.0f)};
    const glm::vec3 v = evaluateDynamicLight({5, 0, 0}, lights);
    // 2 * (1 - 0.5)^2 = 0.5
    EXPECT_FLOAT_EQ(v.r, 0.5f);
    EXPECT_FLOAT_EQ(v.g, 0.25f);
    EXPECT_FLOAT_EQ(v.b, 0.125f);
}

TEST(DynLightEvaluate, UsesEuclideanDistance) {
    const std::array lights{makeLight({1, 2, 3}, 10.0f)};
    // distance from (1,2,3) to (4,6,3) = 5
    EXPECT_FLOAT_EQ(evaluateDynamicLight({4, 6, 3}, lights).r, 0.25f);
}

TEST(DynLightEvaluate, ContributionsAreAdditive) {
    const std::array lights{makeLight({0, 0, 0}, 10.0f), makeLight({10, 0, 0}, 10.0f)};
    EXPECT_FLOAT_EQ(evaluateDynamicLight({5, 0, 0}, lights).g, 0.5f);
}

TEST(DynLightEvaluate, MaskSelectsLights) {
    const std::array lights{makeLight({0, 0, 0}, 10.0f, {1, 0, 0}), makeLight({0, 0, 0}, 10.0f, {0, 1, 0})};
    EXPECT_EQ(evaluateDynamicLight({0, 0, 0}, lights, 0b01u), glm::vec3(1, 0, 0));
    EXPECT_EQ(evaluateDynamicLight({0, 0, 0}, lights, 0b10u), glm::vec3(0, 1, 0));
    EXPECT_EQ(evaluateDynamicLight({0, 0, 0}, lights, 0u), glm::vec3(0.0f));
}

TEST(DynLightEvaluate, OnlyFirstSixteenLightsAreAddressable) {
    std::array<DynamicLight, kMaxDynamicLights + 4> lights{};
    for (auto& l : lights) {
        l = makeLight({0, 0, 0}, 4.0f, {0.01f, 0.01f, 0.01f});
    }
    const glm::vec3 v = evaluateDynamicLight({0, 0, 0}, lights);
    EXPECT_NEAR(v.r, 0.01f * kMaxDynamicLights, 1e-6f);
}

TEST(DynLightEvaluate, PropertyOutsideEveryRadiusIsBlack) {
    std::mt19937 rng(42u);
    std::uniform_real_distribution<float> pos(-50.0f, 50.0f);
    std::uniform_real_distribution<float> rad(1.0f, 15.0f);
    for (int iter = 0; iter < 200; ++iter) {
        std::array<DynamicLight, 8> lights{};
        for (auto& l : lights) {
            l = makeLight({pos(rng), pos(rng), pos(rng)}, rad(rng));
        }
        const glm::vec3 p{pos(rng), pos(rng), pos(rng)};
        bool inside = false;
        for (const auto& l : lights) {
            inside |= glm::distance(p, l.position) < l.radius;
        }
        const glm::vec3 v = evaluateDynamicLight(p, lights);
        if (!inside) {
            ASSERT_EQ(v, glm::vec3(0.0f));
        } else {
            ASSERT_GT(v.r, 0.0f);
        }
    }
}

// --- Per-light block-light-equivalent level (hand shading, gameplay queries) ----------------

TEST(DynLightLevelOf, EmitterLightMatchesEmissionMapping) {
    const auto torch = makeEmitterLight(BlockId::Torch, {0, 0, 0}, LightSource::HeldItem);
    ASSERT_TRUE(torch.has_value());
    for (float d = 0.0f; d <= 16.0f; d += 0.5f) {
        ASSERT_EQ(blockLightLevelOf(*torch, {d, 0, 0}), blockLightLevelAt(d, 14)) << d;
    }
}

TEST(DynLightLevelOf, UsesTheRealRadiusBeyondFifteen) {
    // Radius 30 at 20 blocks: the GPU still adds (1 - 20/30)^2 = 0.11, so the level is not 0.
    const DynamicLight big = makeLight({0, 0, 0}, 30.0f);
    ASSERT_GT(evaluateDynamicLight({20, 0, 0}, std::span(&big, 1)).r, 0.0f);
    EXPECT_GT(blockLightLevelOf(big, {20, 0, 0}), 0);
    EXPECT_EQ(blockLightLevelOf(big, {0, 0, 0}), kMaxBlockLight);
    EXPECT_EQ(blockLightLevelOf(big, {30, 0, 0}), 0);
}

TEST(DynLightLevelOf, IntensityScalesTheLevel) {
    const DynamicLight dim = makeLight({0, 0, 0}, 14.0f, glm::vec3(1.0f), 0.1f);
    const DynamicLight normal = makeLight({0, 0, 0}, 14.0f);
    const DynamicLight bright = makeLight({0, 0, 0}, 14.0f, glm::vec3(1.0f), 2.0f);
    EXPECT_EQ(blockLightLevelOf(dim, {0, 0, 0}), 1);
    EXPECT_EQ(blockLightLevelOf(normal, {0, 0, 0}), 14);
    EXPECT_EQ(blockLightLevelOf(bright, {0, 0, 0}), kMaxBlockLight);
    for (float d = 0.0f; d < 14.0f; d += 0.25f) {
        const glm::vec3 p{0, d, 0};
        ASSERT_LE(blockLightLevelOf(dim, p), blockLightLevelOf(normal, p)) << d;
        ASSERT_LE(blockLightLevelOf(normal, p), blockLightLevelOf(bright, p)) << d;
    }
    EXPECT_GT(blockLightLevelOf(bright, {0, 7, 0}), blockLightLevelOf(normal, {0, 7, 0}));
}

TEST(DynLightLevelOf, TinyRadiusStillLitAtTheSource) {
    const DynamicLight tiny = makeLight({0, 0, 0}, 0.4f);
    EXPECT_EQ(blockLightLevelOf(tiny, {0, 0, 0}), 1);
    EXPECT_EQ(blockLightLevelOf(tiny, {0.5f, 0, 0}), 0);
}

TEST(DynLightLevelOf, InvalidLightGivesZero) {
    EXPECT_EQ(blockLightLevelOf(DynamicLight{}, {0, 0, 0}), 0);
    DynamicLight nanLight = makeLight({0, 0, 0}, 5.0f);
    nanLight.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(blockLightLevelOf(nanLight, {0, 0, 0}), 0);
}

TEST(DynLightLevelOf, PropertyAgreesWithGpuAndIsMonotonic) {
    std::mt19937 rng(0x5EEDu);
    std::uniform_real_distribution<float> radius(0.2f, 40.0f);
    std::uniform_real_distribution<float> intensity(0.05f, 4.0f);
    std::uniform_real_distribution<float> unit(0.0f, 1.3f);
    for (int i = 0; i < 3000; ++i) {
        const DynamicLight l = makeLight({0, 0, 0}, radius(rng), glm::vec3(1.0f), intensity(rng));
        const float d0 = unit(rng) * l.radius;
        const float d1 = d0 + unit(rng) * l.radius;
        const uint8_t a = blockLightLevelOf(l, {d0, 0, 0});
        const uint8_t b = blockLightLevelOf(l, {d1, 0, 0});
        ASSERT_LE(a, kMaxBlockLight);
        ASSERT_GE(a, b) << "r=" << l.radius << " d0=" << d0 << " d1=" << d1;
        ASSERT_EQ(a > 0, evaluateDynamicLight({d0, 0, 0}, std::span(&l, 1)).r > 0.0f) << "r=" << l.radius << " d=" << d0;
    }
}
