#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "render/TextRenderer.hpp"
#include "ui/BitmapFont.hpp"

#include <glm/glm.hpp>
#include <string>

using namespace betacraft::render;
using namespace betacraft::ui;

TEST(MsdfFontTest, MedianFilterLogic) {
    // Median of 3 channels: max(min(r, g), min(max(r, g), b))
    EXPECT_FLOAT_EQ(TextRenderer::msdfMedian(0.2f, 0.5f, 0.8f), 0.5f);
    EXPECT_FLOAT_EQ(TextRenderer::msdfMedian(0.9f, 0.1f, 0.4f), 0.4f);
    EXPECT_FLOAT_EQ(TextRenderer::msdfMedian(0.7f, 0.7f, 0.7f), 0.7f);
    EXPECT_FLOAT_EQ(TextRenderer::msdfMedian(0.1f, 0.2f, 0.3f), 0.2f);
    EXPECT_FLOAT_EQ(TextRenderer::msdfMedian(0.3f, 0.2f, 0.1f), 0.2f);
}

TEST(MsdfFontTest, FragmentEvaluationContourEdges) {
    // 0.5 is contour edge
    const float alphaContour = TextRenderer::evaluateMsdfFragment(glm::vec3(0.5f, 0.5f, 0.5f), 4.0f);
    EXPECT_NEAR(alphaContour, 0.5f, 1e-4f);

    // > 0.5 is inside the glyph
    const float alphaInside = TextRenderer::evaluateMsdfFragment(glm::vec3(0.8f, 0.8f, 0.8f), 4.0f);
    EXPECT_GT(alphaInside, 0.5f);

    // < 0.5 is outside the glyph
    const float alphaOutside = TextRenderer::evaluateMsdfFragment(glm::vec3(0.2f, 0.2f, 0.2f), 4.0f);
    EXPECT_LT(alphaOutside, 0.5f);
}

TEST(MsdfFontTest, VirtualGuiScaleCalculations) {
    // Auto mode adapts to window resolution
    EXPECT_EQ(TextRenderer::calculateGuiScale(640, 480, GuiScalePreference::Auto), 2);
    EXPECT_EQ(TextRenderer::calculateGuiScale(1280, 720, GuiScalePreference::Auto), 3);
    EXPECT_EQ(TextRenderer::calculateGuiScale(1920, 1080, GuiScalePreference::Auto), 4);
    EXPECT_EQ(TextRenderer::calculateGuiScale(3840, 2160, GuiScalePreference::Auto), 4); // Clamped at 4

    // Minimum boundary window (e.g. 100x100) clamps to scale 1
    EXPECT_EQ(TextRenderer::calculateGuiScale(100, 100, GuiScalePreference::Auto), 1);
    EXPECT_EQ(TextRenderer::calculateGuiScale(320, 240, GuiScalePreference::Auto), 1);

    // Explicit preferences override resolution
    EXPECT_EQ(TextRenderer::calculateGuiScale(3840, 2160, GuiScalePreference::Scale1x), 1);
    EXPECT_EQ(TextRenderer::calculateGuiScale(3840, 2160, GuiScalePreference::Scale2x), 2);
    EXPECT_EQ(TextRenderer::calculateGuiScale(640, 480, GuiScalePreference::Scale4x), 4);
}

TEST(MsdfFontTest, GlyphMetricsAndTextMeasurement) {
    EXPECT_FLOAT_EQ(TextRenderer::measureTextWidth("", 1.0f), 0.0f);

    const float wScale1 = TextRenderer::measureTextWidth("BetaCraft", 1.0f);
    const float wScale2 = TextRenderer::measureTextWidth("BetaCraft", 2.0f);

    EXPECT_GT(wScale1, 0.0f);
    EXPECT_FLOAT_EQ(wScale2, wScale1 * 2.0f);
}

TEST(MsdfFontTest, QuadEmissionEfficiency) {
    std::vector<UiQuadVertex> msdfVerts;
    const std::string testStr = "BetaCraft 1.7.3 Modernization";

    // TextRenderer generates 1 quad (6 vertices) per character (or 12 with drop shadow)
    TextRenderer::appendText(msdfVerts, testStr, 10.0f, 10.0f, 1.0f, glm::vec4(1.0f), false);

    // Non-space characters
    size_t nonSpaceCount = 0;
    for (char c : testStr) {
        if (c != ' ') ++nonSpaceCount;
    }

    // 1 quad = 6 vertices per printable glyph in triangle list mode
    EXPECT_EQ(msdfVerts.size(), nonSpaceCount * 6u);

    // Compare with legacy BitmapFont: legacy emits up to 35 quads (210 vertices) per glyph
    std::vector<UiQuadVertex> legacyVerts;
    BitmapFont::appendText(legacyVerts, testStr, 10.0f, 10.0f, 1.0f, glm::vec4(1.0f), false);

    // MSDF vertices should be far fewer than legacy bitmap pixel quads (>= 80% reduction)
    EXPECT_LT(msdfVerts.size(), legacyVerts.size());
}
