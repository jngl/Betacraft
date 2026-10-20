#include "math/Frustum.hpp"
#include "math/PerlinNoise.hpp"

#include <cmath>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <glm/gtc/matrix_transform.hpp>

using namespace betacraft::math;
using namespace testing;

TEST(PerlinNoiseTest, Determinism) {
    PerlinNoise p1(173173ULL);
    PerlinNoise p2(173173ULL);

    for (int i = -30; i <= 30; ++i) {
        const double x = i * 0.17;
        const double y = i * 0.23;
        const double z = i * 0.31;
        EXPECT_DOUBLE_EQ(p1.noise2D(x, y), p2.noise2D(x, y));
        EXPECT_DOUBLE_EQ(p1.noise3D(x, y, z), p2.noise3D(x, y, z));
        EXPECT_DOUBLE_EQ(p1.fbm2D(x, y, 4), p2.fbm2D(x, y, 4));
        EXPECT_DOUBLE_EQ(p1.fbm3D(x, y, z, 4), p2.fbm3D(x, y, z, 4));
    }
}

TEST(PerlinNoiseTest, OutputRangeAndBounds) {
    PerlinNoise p(42173ULL);

    for (int i = -50; i <= 50; ++i) {
        const double x = i * 0.15;
        const double y = i * -0.27;
        const double z = i * 0.41;

        const double n2 = p.noise2D(x, y);
        EXPECT_GE(n2, -1.0);
        EXPECT_LE(n2, 1.0);

        const double n3 = p.noise3D(x, y, z);
        EXPECT_GE(n3, -1.0);
        EXPECT_LE(n3, 1.0);

        const double fbm2 = p.fbm2D(x, y, 5, 2.0, 0.5);
        EXPECT_GE(fbm2, -1.0);
        EXPECT_LE(fbm2, 1.0);

        const double fbm3 = p.fbm3D(x, y, z, 5, 2.0, 0.5);
        EXPECT_GE(fbm3, -1.0);
        EXPECT_LE(fbm3, 1.0);
    }
}

TEST(PerlinNoiseTest, ReseedModifiesOutput) {
    PerlinNoise p1(100ULL);
    PerlinNoise p2(200ULL);

    bool anyDifference = false;
    for (int i = 1; i <= 20; ++i) {
        if (std::abs(p1.noise2D(i * 0.19, i * 0.29) - p2.noise2D(i * 0.19, i * 0.29)) > 1e-4) {
            anyDifference = true;
            break;
        }
    }
    EXPECT_TRUE(anyDifference);

    p1.reseed(200ULL);
    EXPECT_DOUBLE_EQ(p1.noise2D(0.5, 0.5), p2.noise2D(0.5, 0.5));
}

TEST(PerlinNoiseTest, HashCoordsConsistency) {
    const uint32_t h1 = PerlinNoise::hashCoords(10, -5, 42, 173ULL);
    const uint32_t h2 = PerlinNoise::hashCoords(10, -5, 42, 173ULL);
    const uint32_t h3 = PerlinNoise::hashCoords(11, -5, 42, 173ULL);

    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, h3);
}

TEST(FrustumTest, PlaneExtractionAndAABBIntersection) {
    Frustum frustum;
    const glm::mat4 proj = glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    frustum.update(proj * view);

    // Box right in front of camera at (0, 0, 0)
    EXPECT_TRUE(frustum.intersectsAABB(glm::vec3(-1.0f, -1.0f, -1.0f), glm::vec3(1.0f, 1.0f, 1.0f)));

    // Box behind camera at Z = +20
    EXPECT_FALSE(frustum.intersectsAABB(glm::vec3(-1.0f, -1.0f, 15.0f), glm::vec3(1.0f, 1.0f, 25.0f)));

    // Box way off to the right (+X = 200)
    EXPECT_FALSE(frustum.intersectsAABB(glm::vec3(190.0f, -1.0f, 0.0f), glm::vec3(210.0f, 1.0f, 2.0f)));

    // Box beyond far plane (Z < -150)
    EXPECT_FALSE(frustum.intersectsAABB(glm::vec3(-1.0f, -1.0f, -200.0f), glm::vec3(1.0f, 1.0f, -180.0f)));

    // Huge bounding box encompassing the entire view
    EXPECT_TRUE(frustum.intersectsAABB(glm::vec3(-500.0f), glm::vec3(500.0f)));
}
