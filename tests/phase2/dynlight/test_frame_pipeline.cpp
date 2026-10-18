// P4.6 — End-to-end CPU pipeline: manager -> selection -> tile culling -> std140 bytes,
// plus the shader include splicer used to inject dynamic_lights.glsl into CHUNK_FS.
#include "lighting/DynamicLightFrame.hpp"
#include "lighting/ShaderInclude.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>

using namespace betacraft::lighting;
using betacraft::world::BlockId;

namespace {

constexpr glm::vec2 kViewport{1600.0f, 900.0f};

glm::mat4 viewProjAt(glm::vec3 eye, glm::vec3 dir) {
    const glm::mat4 proj = glm::perspective(glm::radians(70.0f), kViewport.x / kViewport.y, 0.05f, 256.0f);
    return proj * glm::lookAt(eye, eye + dir, glm::vec3(0, 1, 0));
}

int32_t uboCount(const UboBuffer& b) {
    int32_t v;
    std::memcpy(&v, b.data() + ubo::kLightCountOffset, sizeof v);
    return v;
}

uint32_t uboMask(const UboBuffer& b, uint32_t tile) {
    uint32_t v;
    std::memcpy(&v, b.data() + ubo::tileMaskOffset(tile), sizeof v);
    return v;
}

} // namespace

// --- Shader include -------------------------------------------------------------

TEST(DynLightShaderInclude, InsertedRightAfterVersionLine) {
    const std::string out = injectShaderInclude("#version 330 core\nvoid main() {}\n", "#define X 1\n");
    EXPECT_EQ(out, "#version 330 core\n#define X 1\nvoid main() {}\n");
}

TEST(DynLightShaderInclude, VersionAfterLeadingCommentsIsFound) {
    const std::string out = injectShaderInclude("// header\n#version 330 core\nint a;\n", "int b;");
    EXPECT_EQ(out, "// header\n#version 330 core\nint b;\nint a;\n");
}

TEST(DynLightShaderInclude, MissingTrailingNewlineInIncludeIsAdded) {
    const std::string out = injectShaderInclude("#version 330 core\nint a;", "int b;");
    EXPECT_EQ(out, "#version 330 core\nint b;\nint a;");
}

TEST(DynLightShaderInclude, VersionOnLastLineWithoutNewline) {
    const std::string out = injectShaderInclude("#version 330 core", "int b;");
    EXPECT_EQ(out, "#version 330 core\nint b;\n");
}

TEST(DynLightShaderInclude, NoVersionPrependsInclude) {
    EXPECT_EQ(injectShaderInclude("int a;\n", "int b;\n"), "int b;\nint a;\n");
}

TEST(DynLightShaderInclude, EmptyIncludeLeavesSourceUntouched) {
    EXPECT_EQ(injectShaderInclude("#version 330 core\nint a;\n", ""), "#version 330 core\nint a;\n");
}

TEST(DynLightShaderInclude, CrLfLineEndingsPreserved) {
    EXPECT_EQ(injectShaderInclude("#version 330 core\r\nint a;\r\n", "int b;\n"), "#version 330 core\r\nint b;\nint a;\r\n");
}

TEST(DynLightShaderInclude, InsertedAfterExtensionDirectives) {
    // #extension must precede every non-preprocessor token, and the include declares a
    // struct and a uniform block: it has to go after the last #extension.
    const std::string src = "#version 330 core\n#extension GL_ARB_a : enable\n\n// c\n#extension GL_ARB_b : require\n"
                            "in vec2 v;\n";
    EXPECT_EQ(injectShaderInclude(src, "int b;\n"),
              "#version 330 core\n#extension GL_ARB_a : enable\n\n// c\n#extension GL_ARB_b : require\nint b;\n"
              "in vec2 v;\n");
}

TEST(DynLightShaderInclude, ExtensionLaterInTheCodeIsNotAnAnchor) {
    // Only directives before the first code line count (a later one is invalid GLSL anyway).
    const std::string src = "#version 330 core\nin vec2 v;\n#extension GL_ARB_a : enable\n";
    EXPECT_EQ(injectShaderInclude(src, "int b;\n"), "#version 330 core\nint b;\nin vec2 v;\n#extension GL_ARB_a : enable\n");
}

TEST(DynLightShaderInclude, VersionInsideBlockCommentIsIgnored) {
    const std::string src = "/*\n#version 100\n*/\n#version 330 core\nint a;\n";
    EXPECT_EQ(injectShaderInclude(src, "int b;\n"), "/*\n#version 100\n*/\n#version 330 core\nint b;\nint a;\n");
}

TEST(DynLightShaderInclude, DirectiveNameMustBeExactlyVersion) {
    EXPECT_EQ(injectShaderInclude("#versioned 1\nint a;\n", "int b;\n"), "int b;\n#versioned 1\nint a;\n");
    // `# version` (blank after '#') is a valid GLSL directive.
    EXPECT_EQ(injectShaderInclude("# version 330 core\nint a;\n", "int b;\n"), "# version 330 core\nint b;\nint a;\n");
}

// --- Frame pipeline ---------------------------------------------------------------

TEST(DynLightFrame, EmptyManagerUploadsNothing) {
    DynamicLightManager m;
    DynamicLightFrame f;
    f.build(m, {0, 0, 0}, viewProjAt({0, 0, 0}, {0, 0, -1}), kViewport);
    EXPECT_EQ(f.selection().count, 0u);
    EXPECT_EQ(uboCount(f.uboBytes()), 0);
    for (uint32_t t = 0; t < kTileCount; ++t) {
        ASSERT_EQ(uboMask(f.uboBytes(), t), 0u);
    }
}

TEST(DynLightFrame, HeldTorchLightsTheCaveWallInFront) {
    const glm::vec3 eye{8.5f, 30.6f, 8.5f};
    const glm::vec3 look{0, 0, -1};
    DynamicLightManager m;
    ASSERT_TRUE(m.submitHeldItem(eye, look, BlockId::Torch));
    DynamicLightFrame f;
    f.build(m, eye, viewProjAt(eye, look), kViewport);

    ASSERT_EQ(uboCount(f.uboBytes()), 1);
    // A wall fragment 4 blocks ahead at the screen centre is lit with a warm tint...
    const glm::vec2 centre = kViewport * 0.5f;
    const glm::vec3 lit = f.shade(eye + look * 4.0f, centre);
    EXPECT_GT(lit.r, 0.1f);
    EXPECT_GE(lit.r, lit.g);
    EXPECT_GE(lit.g, lit.b);
    // ...and the centre tile in the uploaded bytes references light 0.
    const uint32_t tile = f.grid().tileIndexForFragCoord(centre);
    EXPECT_EQ(uboMask(f.uboBytes(), tile) & 1u, 1u);
    // A wall 20 blocks away stays dark (no light beyond radius 14).
    EXPECT_EQ(f.shade(eye + look * 20.0f, centre), glm::vec3(0.0f));
}

TEST(DynLightFrame, ShadeMatchesCpuReferenceWithTileMask) {
    std::mt19937 rng(77u);
    std::uniform_real_distribution<float> pos(-12.0f, 12.0f);
    const glm::vec3 eye{0, 0, 0};
    const glm::vec3 look{0, 0, -1};
    const glm::mat4 vp = viewProjAt(eye, look);
    DynamicLightManager m;
    for (int i = 0; i < 24; ++i) {
        (void)m.submitDroppedItem({pos(rng), pos(rng), pos(rng) - 12.0f}, i % 2 ? BlockId::Torch : BlockId::Glowstone);
    }
    DynamicLightFrame f;
    f.build(m, eye, vp, kViewport);
    ASSERT_EQ(f.selection().count, kMaxDynamicLights);
    for (int s = 0; s < 500; ++s) {
        const glm::vec3 p{pos(rng), pos(rng), pos(rng) - 12.0f};
        const glm::vec4 clip = vp * glm::vec4(p, 1.0f);
        if (clip.w <= 0.0f) {
            continue;
        }
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        if (glm::any(glm::greaterThan(glm::abs(ndc), glm::vec2(1.0f)))) {
            continue;
        }
        const glm::vec2 frag = (ndc * 0.5f + 0.5f) * kViewport;
        // Tile culling is conservative: culled shading equals unculled shading.
        const glm::vec3 culled = f.shade(p, frag);
        const glm::vec3 full = evaluateDynamicLight(p, f.selection().view());
        ASSERT_NEAR(culled.r, full.r, 1e-5f);
        ASSERT_NEAR(culled.g, full.g, 1e-5f);
        ASSERT_NEAR(culled.b, full.b, 1e-5f);
    }
}

TEST(DynLightFrame, LightsOutsideFrustumDoNotStealSlots) {
    const glm::vec3 eye{0, 0, 0};
    DynamicLightManager m;
    // 20 near torches behind the camera (sphere does not reach it) + 1 farther in front.
    for (int i = 0; i < 20; ++i) {
        (void)m.submitDroppedItem({static_cast<float>(i) - 10.0f, 0.0f, 16.0f}, BlockId::Torch);
    }
    (void)m.submitDroppedItem({0, 0, -30}, BlockId::Glowstone);
    DynamicLightFrame f;
    f.build(m, eye, viewProjAt(eye, {0, 0, -1}), kViewport);
    ASSERT_EQ(f.selection().count, 1u);
    EXPECT_FLOAT_EQ(f.selection().lights[0].position.z, -30.0f);
}

TEST(DynLightFrame, BlockLightLevelForHeldHandShading) {
    // Diagnostic §5.1.6: the hand used to read only the static light of the player's block.
    const glm::vec3 eye{0, 0, 0};
    DynamicLightManager m;
    ASSERT_TRUE(m.submitHeldItem(eye, {0, 0, -1}, BlockId::Torch));
    DynamicLightFrame f;
    f.build(m, eye, viewProjAt(eye, {0, 0, -1}), kViewport);
    const glm::vec3 hand = m.lights()[0].position;
    EXPECT_EQ(f.blockLightLevelAt(hand), 14);
    // Same levels as a placed torch: one level lost per block.
    EXPECT_EQ(f.blockLightLevelAt(hand + glm::vec3(0, 0, -7)), 7);
    EXPECT_EQ(f.blockLightLevelAt(hand + glm::vec3(0, 0, -10)), 4);
    EXPECT_EQ(f.blockLightLevelAt(eye + glm::vec3(0, 0, -30)), 0);
}

TEST(DynLightFrame, BlockLightLevelHonoursCustomRadiusAndIntensity) {
    const glm::vec3 eye{0, 0, 0};
    DynamicLightManager m;
    DynamicLight lantern;
    lantern.position = {0, 0, -2};
    lantern.radius = 30.0f;
    lantern.source = LightSource::Entity;
    ASSERT_TRUE(m.submit(lantern));
    DynamicLight faint;
    faint.position = {3, 0, -2};
    faint.radius = 14.0f;
    faint.intensity = 0.1f;
    ASSERT_TRUE(m.submit(faint));
    DynamicLightFrame f;
    f.build(m, eye, viewProjAt(eye, {0, 0, -1}), kViewport);
    ASSERT_EQ(f.selection().count, 2u);
    // 20 blocks from the radius-30 light: lit on the GPU, so lit for the hand too.
    EXPECT_GT(f.blockLightLevelAt({0, 0, -22}), 0);
    EXPECT_EQ(f.blockLightLevelAt({0, 0, -22}), blockLightLevelOf(lantern, {0, 0, -22}));
    // The faint light never dominates: the level is the max over the selected lights.
    EXPECT_EQ(f.blockLightLevelAt({3, 0, -2}), std::max(blockLightLevelOf(lantern, {3, 0, -2}),
                                                        blockLightLevelOf(faint, {3, 0, -2})));
}

TEST(DynLightFrame, RebuildIsDeterministic) {
    DynamicLightManager m;
    for (int i = 0; i < 40; ++i) {
        (void)m.submitDroppedItem({static_cast<float>(i % 7), 0.0f, -static_cast<float>(i)}, BlockId::Torch);
    }
    DynamicLightFrame a;
    DynamicLightFrame b;
    const glm::mat4 vp = viewProjAt({0, 0, 0}, {0, 0, -1});
    a.build(m, {0, 0, 0}, vp, kViewport);
    b.build(m, {0, 0, 0}, vp, kViewport);
    b.build(m, {0, 0, 0}, vp, kViewport);
    EXPECT_EQ(a.uboBytes(), b.uboBytes());
}

TEST(DynLightFrame, FullPendingListBuildsTheNearestSixteen) {
    // Worst case per frame (pending list at its cap). Timing is only RECORDED, never asserted:
    // a wall-clock bound would be flaky under sanitizers, valgrind or a loaded CI machine.
    DynamicLightManager m;
    for (size_t i = 0; i < DynamicLightManager::kMaxPendingLights; ++i) {
        (void)m.submitDroppedItem({static_cast<float>(i % 32), 0.0f, -static_cast<float>(i / 32)}, BlockId::Torch);
    }
    ASSERT_EQ(m.size(), DynamicLightManager::kMaxPendingLights - DynamicLightManager::kReservedHeldSlots);
    DynamicLightFrame f;
    const glm::mat4 vp = viewProjAt({0, 0, 0}, {0, 0, -1});
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) {
        f.build(m, {0, 0, 0}, vp, kViewport);
    }
    const auto us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    ::testing::Test::RecordProperty("avg_build_us", static_cast<int>(us / 20.0));

    ASSERT_EQ(f.selection().count, kMaxDynamicLights);
    // Brute-force reference: the 16 nearest visible lights, nearest first.
    const auto ref = m.select({0, 0, 0});
    float prev = 0.0f;
    for (uint32_t i = 0; i < f.selection().count; ++i) {
        const float d = glm::length(f.selection().lights[i].position);
        EXPECT_GE(d, prev);
        EXPECT_FLOAT_EQ(d, glm::length(ref.lights[i].position)) << i;
        prev = d;
    }
}
