// P4.6 — Forward+ screen-tile light culling on the CPU.
#include "lighting/TileLightCuller.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <random>
#include <vector>

using namespace betacraft::lighting;

namespace {

constexpr glm::vec2 kViewport{1600.0f, 900.0f}; // 100 x 100 px tiles with the 16x9 grid

DynamicLight lightAt(glm::vec3 p, float r) {
    DynamicLight l;
    l.position = p;
    l.radius = r;
    return l;
}

uint32_t countTilesWithBit(const TileLightGrid& g, uint32_t bit) {
    uint32_t n = 0;
    for (uint32_t m : g.masks()) {
        n += (m >> bit) & 1u;
    }
    return n;
}

glm::mat4 cameraViewProj() {
    const glm::mat4 proj = glm::perspective(glm::radians(70.0f), kViewport.x / kViewport.y, 0.05f, 256.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    return proj * view;
}

} // namespace

// --- Sphere projection -----------------------------------------------------------

TEST(DynLightProject, OrthoIdentityMapsSphereBoundsDirectly) {
    const auto r = projectSphereToNdc({0.25f, -0.5f, 0.0f}, 0.1f, glm::mat4(1.0f));
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->min.x, 0.15f, 1e-6f);
    EXPECT_NEAR(r->max.x, 0.35f, 1e-6f);
    EXPECT_NEAR(r->min.y, -0.6f, 1e-6f);
    EXPECT_NEAR(r->max.y, -0.4f, 1e-6f);
}

TEST(DynLightProject, ClampedToScreen) {
    const auto r = projectSphereToNdc({0.9f, 0.0f, 0.0f}, 0.5f, glm::mat4(1.0f));
    ASSERT_TRUE(r.has_value());
    EXPECT_FLOAT_EQ(r->max.x, 1.0f);
    EXPECT_NEAR(r->min.x, 0.4f, 1e-6f);
}

TEST(DynLightProject, OffscreenSphereRejected) {
    EXPECT_FALSE(projectSphereToNdc({3.0f, 0.0f, 0.0f}, 0.5f, glm::mat4(1.0f)).has_value());
    EXPECT_FALSE(projectSphereToNdc({0.0f, 0.0f, 5.0f}, 0.5f, glm::mat4(1.0f)).has_value()); // beyond far
}

TEST(DynLightProject, BehindPerspectiveCameraRejected) {
    EXPECT_FALSE(projectSphereToNdc({0, 0, 10}, 1.0f, cameraViewProj()).has_value());
}

TEST(DynLightProject, CameraInsideSphereCoversWholeScreen) {
    const auto r = projectSphereToNdc({0, 0, 0.5f}, 2.0f, cameraViewProj());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->min, glm::vec2(-1.0f));
    EXPECT_EQ(r->max, glm::vec2(1.0f));
}

TEST(DynLightProject, AheadOfPerspectiveCameraIsCentered) {
    const auto r = projectSphereToNdc({0, 0, -10}, 1.0f, cameraViewProj());
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->min.x, -r->max.x, 1e-5f);
    EXPECT_NEAR(r->min.y, -r->max.y, 1e-5f);
    EXPECT_LT(r->max.x, 0.25f);
    EXPECT_GT(r->max.x, 0.0f);
}

TEST(DynLightProject, NonPositiveRadiusRejected) {
    EXPECT_FALSE(projectSphereToNdc({0, 0, 0}, 0.0f, glm::mat4(1.0f)).has_value());
}

// --- Tile grid ------------------------------------------------------------------

TEST(DynLightTiles, TileSizeFollowsViewport) {
    TileLightGrid g;
    g.build({}, glm::mat4(1.0f), kViewport);
    EXPECT_EQ(g.tileSizePx(), glm::vec2(100.0f, 100.0f));
    EXPECT_EQ(g.masks().size(), kTileCount);
    for (uint32_t m : g.masks()) {
        EXPECT_EQ(m, 0u);
    }
}

TEST(DynLightTiles, DegenerateViewportIsClampedNotDividedByZero) {
    TileLightGrid g;
    g.build(std::vector{lightAt({0, 0, 0}, 0.1f)}, glm::mat4(1.0f), {0.0f, -5.0f});
    EXPECT_GT(g.tileSizePx().x, 0.0f);
    EXPECT_GT(g.tileSizePx().y, 0.0f);
}

TEST(DynLightTiles, SmallCenteredLightHitsOnlyCenterTiles) {
    TileLightGrid g;
    g.build(std::vector{lightAt({0, 0, 0}, 0.05f)}, glm::mat4(1.0f), kViewport);
    // x: 760..840 px -> cols 7,8 ; y: 427.5..472.5 px -> row 4
    EXPECT_EQ(g.mask(7, 4), 1u);
    EXPECT_EQ(g.mask(8, 4), 1u);
    EXPECT_EQ(countTilesWithBit(g, 0), 2u);
}

TEST(DynLightTiles, RowZeroIsBottomLikeGlFragCoord) {
    TileLightGrid g;
    g.build(std::vector{lightAt({-0.99f, -0.99f, 0}, 0.005f), lightAt({0.99f, 0.99f, 0}, 0.005f)}, glm::mat4(1.0f),
            kViewport);
    EXPECT_EQ(g.mask(0, 0), 0b01u);
    EXPECT_EQ(g.mask(kTileCols - 1, kTileRows - 1), 0b10u);
    EXPECT_EQ(countTilesWithBit(g, 0), 1u);
    EXPECT_EQ(countTilesWithBit(g, 1), 1u);
}

TEST(DynLightTiles, HugeLightCoversAllTiles) {
    TileLightGrid g;
    g.build(std::vector{lightAt({0, 0, 0}, 0.9f)}, glm::mat4(1.0f), kViewport);
    EXPECT_EQ(countTilesWithBit(g, 0), kTileCount);
}

TEST(DynLightTiles, OffscreenLightTouchesNoTile) {
    TileLightGrid g;
    g.build(std::vector{lightAt({5, 0, 0}, 0.5f)}, glm::mat4(1.0f), kViewport);
    EXPECT_EQ(countTilesWithBit(g, 0), 0u);
}

TEST(DynLightTiles, InvalidLightsSkipped) {
    TileLightGrid g;
    g.build(std::vector{lightAt({0, 0, 0}, 0.0f)}, glm::mat4(1.0f), kViewport);
    EXPECT_EQ(countTilesWithBit(g, 0), 0u);
}

TEST(DynLightTiles, OnlyFirstSixteenLightsAreCulled) {
    std::vector<DynamicLight> lights(kMaxDynamicLights + 3, lightAt({0, 0, 0}, 0.9f));
    TileLightGrid g;
    g.build(lights, glm::mat4(1.0f), kViewport);
    EXPECT_EQ(g.mask(0, 0), 0xFFFFu);
}

TEST(DynLightTiles, IndexListsAreAscendingAndMatchMask) {
    TileLightGrid g;
    g.build(std::vector{lightAt({0.5f, 0.5f, 0}, 0.05f), lightAt({0, 0, 0}, 0.9f), lightAt({0.5f, 0.5f, 0}, 0.05f)},
            glm::mat4(1.0f), kViewport);
    // NDC (0.5, 0.5) -> px (1200, 675) -> tile (12, 6)
    const auto idx = g.lightIndices(12, 6);
    ASSERT_EQ(idx.size(), 3u);
    EXPECT_EQ(idx[0], 0u);
    EXPECT_EQ(idx[1], 1u);
    EXPECT_EQ(idx[2], 2u);
    const auto corner = g.lightIndices(0, 0);
    ASSERT_EQ(corner.size(), 1u);
    EXPECT_EQ(corner[0], 1u);
    for (uint32_t row = 0; row < kTileRows; ++row) {
        for (uint32_t col = 0; col < kTileCols; ++col) {
            uint32_t rebuilt = 0;
            for (uint8_t i : g.lightIndices(col, row)) {
                rebuilt |= 1u << i;
            }
            ASSERT_EQ(rebuilt, g.mask(col, row));
        }
    }
}

TEST(DynLightTiles, OutOfRangeTileQueriesAreSafe) {
    TileLightGrid g;
    g.build(std::vector{lightAt({0, 0, 0}, 0.9f)}, glm::mat4(1.0f), kViewport);
    EXPECT_EQ(g.mask(kTileCols, 0), 0u);
    EXPECT_EQ(g.mask(0, kTileRows), 0u);
    EXPECT_TRUE(g.lightIndices(kTileCols, kTileRows).empty());
}

TEST(DynLightTiles, RebuildResetsPreviousFrame) {
    TileLightGrid g;
    g.build(std::vector{lightAt({0, 0, 0}, 0.9f)}, glm::mat4(1.0f), kViewport);
    g.build({}, glm::mat4(1.0f), kViewport);
    EXPECT_EQ(countTilesWithBit(g, 0), 0u);
    EXPECT_TRUE(g.lightIndices(0, 0).empty());
}

TEST(DynLightTiles, FragCoordToTileMirrorsShader) {
    TileLightGrid g;
    g.build({}, glm::mat4(1.0f), kViewport);
    EXPECT_EQ(g.tileIndexForFragCoord({0.5f, 0.5f}), 0u);
    EXPECT_EQ(g.tileIndexForFragCoord({150.5f, 0.5f}), 1u);
    EXPECT_EQ(g.tileIndexForFragCoord({0.5f, 150.5f}), kTileCols);
    EXPECT_EQ(g.tileIndexForFragCoord({1599.5f, 899.5f}), kTileCount - 1);
    EXPECT_EQ(g.tileIndexForFragCoord({5000.0f, 5000.0f}), kTileCount - 1); // clamped
    EXPECT_EQ(g.tileIndexForFragCoord({-3.0f, -3.0f}), 0u);
}

TEST(DynLightTiles, PerspectiveLightAheadLitsCenterNotCorners) {
    TileLightGrid g;
    g.build(std::vector{lightAt({0, 0, -10}, 1.0f)}, cameraViewProj(), kViewport);
    EXPECT_NE(g.mask(7, 4) | g.mask(8, 4), 0u);
    EXPECT_EQ(g.mask(0, 0), 0u);
    EXPECT_EQ(g.mask(kTileCols - 1, kTileRows - 1), 0u);
}

TEST(DynLightTiles, PerspectiveLightBehindCameraLitsNothing) {
    TileLightGrid g;
    g.build(std::vector{lightAt({0, 0, 10}, 1.0f)}, cameraViewProj(), kViewport);
    EXPECT_EQ(countTilesWithBit(g, 0), 0u);
}

// Core Forward+ invariant: culling may be conservative but must never miss a lit pixel.
TEST(DynLightTiles, PropertyConservativeNeverMissesALitFragment) {
    std::mt19937 rng(0xF0F0u);
    std::uniform_real_distribution<float> pos(-20.0f, 20.0f);
    std::uniform_real_distribution<float> rad(0.5f, 8.0f);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    const glm::mat4 vp = cameraViewProj();
    size_t checked = 0;
    for (int iter = 0; iter < 200; ++iter) {
        std::vector<DynamicLight> lights;
        for (uint32_t i = 0; i < kMaxDynamicLights; ++i) {
            lights.push_back(lightAt({pos(rng), pos(rng), pos(rng) - 15.0f}, rad(rng)));
        }
        TileLightGrid g;
        g.build(lights, vp, kViewport);
        for (uint32_t i = 0; i < kMaxDynamicLights; ++i) {
            for (int s = 0; s < 20; ++s) {
                glm::vec3 offset{unit(rng), unit(rng), unit(rng)};
                if (glm::length(offset) > 1.0f) {
                    continue;
                }
                const glm::vec3 p = lights[i].position + offset * lights[i].radius * 0.999f;
                const glm::vec4 clip = vp * glm::vec4(p, 1.0f);
                if (clip.w <= 0.0f) {
                    continue;
                }
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                if (glm::any(glm::greaterThan(glm::abs(ndc), glm::vec3(1.0f)))) {
                    continue;
                }
                const glm::vec2 frag = (glm::vec2(ndc) * 0.5f + 0.5f) * kViewport;
                const uint32_t tile = g.tileIndexForFragCoord(frag);
                ASSERT_NE(g.masks()[tile] & (1u << i), 0u) << "iter " << iter << " light " << i;
                ++checked;
            }
        }
    }
    EXPECT_GT(checked, 1000u); // Make sure the property actually exercised many fragments
}

TEST(DynLightTiles, TilesNeverShrinkBelowOnePixel) {
    // An 8 px wide viewport would give 0.5 px tiles; the shader divides by
    // max(u_dynTileSizePx, 1.0), so the CPU must use the same clamped size for its own lookups
    // and masks, otherwise a light lit on the CPU grid lands in another tile on the GPU.
    TileLightGrid g;
    g.build(std::vector{lightAt({0.5f, 0, 0}, 0.2f)}, glm::mat4(1.0f), {8.0f, 4.0f});
    EXPECT_EQ(g.tileSizePx(), glm::vec2(1.0f, 1.0f));
    EXPECT_EQ(g.tileIndexForFragCoord({5.5f, 0.5f}), 5u);
    EXPECT_EQ(g.tileIndexForFragCoord({5.5f, 3.5f}), 3u * kTileCols + 5u);
    // NDC x in [0.3, 0.7] -> px [5.2, 6.8] on the 8 px viewport -> columns 5..6 only.
    EXPECT_NE(g.mask(5, 2) & 1u, 0u);
    EXPECT_NE(g.mask(6, 2) & 1u, 0u);
    EXPECT_EQ(g.mask(10, 2) & 1u, 0u);
    EXPECT_EQ(g.mask(13, 2) & 1u, 0u);
}
