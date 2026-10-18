// P4.6 — DynamicLightManager: per-frame collection, capped to the UBO capacity by distance.
#include "lighting/DynamicLightManager.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

using namespace betacraft::lighting;
using betacraft::world::BlockId;

namespace {

DynamicLight lightAt(glm::vec3 p, float r = 5.0f) {
    DynamicLight l;
    l.position = p;
    l.radius = r;
    return l;
}

} // namespace

TEST(DynLightManager, StartsEmpty) {
    DynamicLightManager m;
    EXPECT_EQ(m.size(), 0u);
    EXPECT_TRUE(m.lights().empty());
    EXPECT_EQ(m.select({0, 0, 0}).count, 0u);
}

TEST(DynLightManager, SubmitAndClear) {
    DynamicLightManager m;
    EXPECT_TRUE(m.submit(lightAt({1, 2, 3})));
    EXPECT_TRUE(m.submit(lightAt({4, 5, 6})));
    EXPECT_EQ(m.size(), 2u);
    EXPECT_EQ(m.lights()[1].position, glm::vec3(4, 5, 6));
    m.clear();
    EXPECT_EQ(m.size(), 0u);
}

TEST(DynLightManager, RejectsInvalidLights) {
    DynamicLightManager m;
    EXPECT_FALSE(m.submit(DynamicLight{}));
    EXPECT_FALSE(m.submit(lightAt({0, 0, 0}, -1.0f)));
    EXPECT_EQ(m.size(), 0u);
}

TEST(DynLightManager, PendingStorageIsBounded) {
    DynamicLightManager m;
    const size_t worldCap = DynamicLightManager::kMaxPendingLights - DynamicLightManager::kReservedHeldSlots;
    for (size_t i = 0; i < worldCap; ++i) {
        ASSERT_TRUE(m.submit(lightAt({static_cast<float>(i), 0, 0})));
    }
    EXPECT_FALSE(m.submit(lightAt({0, 0, 0})));
    EXPECT_EQ(m.size(), worldCap);
}

TEST(DynLightManager, HeldTorchSurvivesAFullPendingList) {
    // Item-drop farm: thousands of glowing drops are submitted before the player's hand. The
    // pending cap must not drop the nearest, most important light before select() ranks them.
    DynamicLightManager m;
    size_t accepted = 0;
    for (size_t i = 0; i < 3 * DynamicLightManager::kMaxPendingLights; ++i) {
        accepted += m.submitDroppedItem({static_cast<float>(i % 64), 0.0f, 20.0f + static_cast<float>(i / 64)},
                                        BlockId::Glowstone)
                        ? 1u
                        : 0u;
    }
    EXPECT_LT(accepted, 3 * DynamicLightManager::kMaxPendingLights);
    const glm::vec3 eye{0.0f, 0.0f, 0.0f};
    ASSERT_TRUE(m.submitHeldItem(eye, {0, 0, -1}, BlockId::Torch));
    EXPECT_LE(m.size(), DynamicLightManager::kMaxPendingLights);
    const LightSelection sel = m.select(eye);
    ASSERT_EQ(sel.count, kMaxDynamicLights);
    EXPECT_EQ(sel.lights[0].source, LightSource::HeldItem);
}

TEST(DynLightManager, HeldReserveIsBoundedToo) {
    // The reserve only protects held lights; it never lets the list grow without bound.
    DynamicLightManager m;
    for (size_t i = 0; i < 2 * DynamicLightManager::kMaxPendingLights; ++i) {
        (void)m.submitHeldItem({0, 0, 0}, {0, 0, -1}, BlockId::Torch);
        (void)m.submitDroppedItem({1, 0, 0}, BlockId::Torch);
    }
    EXPECT_EQ(m.size(), DynamicLightManager::kMaxPendingLights);
}

TEST(DynLightManager, NonHeldLightsLeaveTheReserveFree) {
    DynamicLightManager m;
    size_t accepted = 0;
    while (m.submit(lightAt({0, 0, 0}))) {
        ++accepted;
        ASSERT_LE(accepted, DynamicLightManager::kMaxPendingLights);
    }
    EXPECT_EQ(accepted, DynamicLightManager::kMaxPendingLights - DynamicLightManager::kReservedHeldSlots);
    for (size_t i = 0; i < DynamicLightManager::kReservedHeldSlots; ++i) {
        ASSERT_TRUE(m.submitHeldItem({0, 0, 0}, {0, 0, -1}, BlockId::Torch)) << i;
    }
    EXPECT_FALSE(m.submitHeldItem({0, 0, 0}, {0, 0, -1}, BlockId::Torch));
}

// --- Held item ------------------------------------------------------------------

TEST(DynLightManager, HeldTorchEmitsNearTheRightHand) {
    DynamicLightManager m;
    const glm::vec3 eye{0.0f, 65.62f, 0.0f};
    ASSERT_TRUE(m.submitHeldItem(eye, {0.0f, 0.0f, -1.0f}, BlockId::Torch));
    ASSERT_EQ(m.size(), 1u);
    const DynamicLight& l = m.lights()[0];
    EXPECT_EQ(l.source, LightSource::HeldItem);
    EXPECT_FLOAT_EQ(l.radius, 14.0f);
    const float d = glm::distance(l.position, eye);
    EXPECT_GT(d, 0.1f);
    EXPECT_LT(d, 1.0f);
    EXPECT_LT(l.position.z, eye.z); // In front (looking -Z)
    EXPECT_GT(l.position.x, eye.x); // Right hand (+X when looking -Z)
    EXPECT_LT(l.position.y, eye.y); // Below the eyes
}

TEST(DynLightManager, HeldLightPositionIsWhereTheHeldLightIsSubmitted) {
    // The renderer shades the first-person hand at this point, so it must be the light's own.
    const glm::vec3 eye{3.0f, 70.0f, -2.0f};
    for (const glm::vec3 look : {glm::vec3(0, 0, -1), glm::vec3(1, 0.5f, 0), glm::vec3(0, 1, 0), glm::vec3(0)}) {
        DynamicLightManager m;
        ASSERT_TRUE(m.submitHeldItem(eye, look, BlockId::Torch));
        const glm::vec3 expected = DynamicLightManager::heldLightPosition(eye, look);
        EXPECT_EQ(m.lights()[0].position, expected);
    }
    const glm::vec3 p = DynamicLightManager::heldLightPosition(eye, {0, 0, -1});
    EXPECT_FLOAT_EQ(p.x, eye.x + DynamicLightManager::kHeldRight);
    EXPECT_FLOAT_EQ(p.y, eye.y - DynamicLightManager::kHeldDown);
    EXPECT_FLOAT_EQ(p.z, eye.z - DynamicLightManager::kHeldForward);
}

TEST(DynLightManager, HeldGlowstoneEmitsFullLevel) {
    DynamicLightManager m;
    ASSERT_TRUE(m.submitHeldItem({0, 0, 0}, {1, 0, 0}, BlockId::Glowstone));
    EXPECT_FLOAT_EQ(m.lights()[0].radius, 15.0f);
}

TEST(DynLightManager, HeldNonEmitterAddsNothing) {
    DynamicLightManager m;
    EXPECT_FALSE(m.submitHeldItem({0, 0, 0}, {0, 0, -1}, BlockId::Stone));
    EXPECT_FALSE(m.submitHeldItem({0, 0, 0}, {0, 0, -1}, BlockId::Air));
    EXPECT_EQ(m.size(), 0u);
}

TEST(DynLightManager, HeldLightStableWhenLookingStraightUpOrDown) {
    DynamicLightManager m;
    ASSERT_TRUE(m.submitHeldItem({0, 0, 0}, {0, 1, 0}, BlockId::Torch));
    ASSERT_TRUE(m.submitHeldItem({0, 0, 0}, {0, -1, 0}, BlockId::Torch));
    ASSERT_TRUE(m.submitHeldItem({0, 0, 0}, {0, 0, 0}, BlockId::Torch)); // degenerate direction
    for (const auto& l : m.lights()) {
        EXPECT_TRUE(l.isValid());
        EXPECT_LT(glm::length(l.position), 1.0f);
    }
}

TEST(DynLightManager, HeldLightUnnormalizedLookDirectionGivesSameOffset) {
    DynamicLightManager a;
    DynamicLightManager b;
    ASSERT_TRUE(a.submitHeldItem({0, 0, 0}, {0, 0, -1}, BlockId::Torch));
    ASSERT_TRUE(b.submitHeldItem({0, 0, 0}, {0, 0, -10}, BlockId::Torch));
    EXPECT_NEAR(glm::distance(a.lights()[0].position, b.lights()[0].position), 0.0f, 1e-5f);
}

// --- Dropped item ------------------------------------------------------------------

TEST(DynLightManager, DroppedGlowingItemEmits) {
    DynamicLightManager m;
    ASSERT_TRUE(m.submitDroppedItem({3.5f, 70.0f, -2.5f}, BlockId::Glowstone));
    EXPECT_EQ(m.lights()[0].source, LightSource::DroppedItem);
    EXPECT_EQ(m.lights()[0].position, glm::vec3(3.5f, 70.0f, -2.5f));
    EXPECT_FALSE(m.submitDroppedItem({0, 0, 0}, BlockId::Dirt));
}

TEST(DynLightManager, DroppedItemNonFinitePositionRejected) {
    DynamicLightManager m;
    EXPECT_FALSE(m.submitDroppedItem({std::numeric_limits<float>::quiet_NaN(), 0, 0}, BlockId::Torch));
}

// --- Selection ---------------------------------------------------------------------

TEST(DynLightSelect, SortsNearestFirst) {
    DynamicLightManager m;
    (void)m.submit(lightAt({10, 0, 0}));
    (void)m.submit(lightAt({2, 0, 0}));
    (void)m.submit(lightAt({0, 0, -5}));
    const LightSelection sel = m.select({0, 0, 0});
    ASSERT_EQ(sel.count, 3u);
    EXPECT_EQ(sel.lights[0].position, glm::vec3(2, 0, 0));
    EXPECT_EQ(sel.lights[1].position, glm::vec3(0, 0, -5));
    EXPECT_EQ(sel.lights[2].position, glm::vec3(10, 0, 0));
    EXPECT_EQ(sel.view().size(), 3u);
}

TEST(DynLightSelect, CapsToUboCapacityKeepingNearest) {
    DynamicLightManager m;
    for (int i = 40; i > 0; --i) {
        (void)m.submit(lightAt({static_cast<float>(i), 0, 0}));
    }
    const LightSelection sel = m.select({0, 0, 0});
    ASSERT_EQ(sel.count, kMaxDynamicLights);
    for (uint32_t i = 0; i < sel.count; ++i) {
        EXPECT_FLOAT_EQ(sel.lights[i].position.x, static_cast<float>(i + 1));
    }
}

TEST(DynLightSelect, HeldTorchAlwaysWinsAgainstDistantDroppedItems) {
    DynamicLightManager m;
    for (int i = 0; i < 30; ++i) {
        (void)m.submitDroppedItem({static_cast<float>(i % 6) * 2.0f + 3.0f, 64.0f, static_cast<float>(i / 6) * 2.0f + 3.0f},
                                  BlockId::Torch);
    }
    const glm::vec3 eye{0, 65.6f, 0};
    (void)m.submitHeldItem(eye, {0, 0, -1}, BlockId::Torch);
    const LightSelection sel = m.select(eye);
    ASSERT_EQ(sel.count, kMaxDynamicLights);
    EXPECT_EQ(sel.lights[0].source, LightSource::HeldItem);
}

TEST(DynLightSelect, CustomCapacityRespectedAndClamped) {
    DynamicLightManager m;
    for (int i = 0; i < 20; ++i) {
        (void)m.submit(lightAt({static_cast<float>(i), 0, 0}));
    }
    EXPECT_EQ(m.select({0, 0, 0}, nullptr, 4).count, 4u);
    EXPECT_EQ(m.select({0, 0, 0}, nullptr, 0).count, 0u);
    EXPECT_EQ(m.select({0, 0, 0}, nullptr, 1000).count, kMaxDynamicLights);
}

TEST(DynLightSelect, EqualDistanceTieBrokenBySubmissionOrder) {
    DynamicLightManager m;
    (void)m.submit(lightAt({5, 0, 0}, 3.0f));
    (void)m.submit(lightAt({-5, 0, 0}, 4.0f));
    (void)m.submit(lightAt({0, 5, 0}, 5.0f));
    const LightSelection sel = m.select({0, 0, 0}, nullptr, 2);
    ASSERT_EQ(sel.count, 2u);
    EXPECT_FLOAT_EQ(sel.lights[0].radius, 3.0f);
    EXPECT_FLOAT_EQ(sel.lights[1].radius, 4.0f);
}

TEST(DynLightSelect, FrustumRejectsLightsThatCannotTouchTheView) {
    // Camera at origin looking down -Z.
    const glm::mat4 proj = glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.05f, 256.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    betacraft::math::Frustum frustum;
    frustum.update(proj * view);

    DynamicLightManager m;
    (void)m.submit(lightAt({0, 0, -10}, 5.0f)); // in view
    (void)m.submit(lightAt({0, 0, 20}, 5.0f));  // behind, sphere does not reach the camera
    (void)m.submit(lightAt({0, 0, 3}, 5.0f));   // behind but its sphere covers the camera
    const LightSelection sel = m.select({0, 0, 0}, &frustum);
    ASSERT_EQ(sel.count, 2u);
    EXPECT_FLOAT_EQ(sel.lights[0].position.z, 3.0f);
    EXPECT_FLOAT_EQ(sel.lights[1].position.z, -10.0f);
}

TEST(DynLightSelect, SelectionDoesNotConsumeSubmittedLights) {
    DynamicLightManager m;
    (void)m.submit(lightAt({1, 0, 0}));
    (void)m.select({0, 0, 0});
    EXPECT_EQ(m.size(), 1u);
    EXPECT_EQ(m.select({0, 0, 0}).count, 1u);
}

TEST(DynLightSelect, PropertyMatchesBruteForceReference) {
    std::mt19937 rng(1234u);
    std::uniform_real_distribution<float> pos(-64.0f, 64.0f);
    std::uniform_int_distribution<int> countDist(0, 80);
    for (int iter = 0; iter < 200; ++iter) {
        DynamicLightManager m;
        std::vector<DynamicLight> ref;
        const int n = countDist(rng);
        for (int i = 0; i < n; ++i) {
            DynamicLight l = lightAt({pos(rng), pos(rng), pos(rng)}, 1.0f + static_cast<float>(i));
            ASSERT_TRUE(m.submit(l));
            ref.push_back(l);
        }
        const glm::vec3 cam{pos(rng), pos(rng), pos(rng)};
        std::vector<size_t> idx(ref.size());
        std::iota(idx.begin(), idx.end(), size_t{0});
        std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            const glm::vec3 da = ref[a].position - cam;
            const glm::vec3 db = ref[b].position - cam;
            return glm::dot(da, da) < glm::dot(db, db);
        });
        const LightSelection sel = m.select(cam);
        ASSERT_EQ(sel.count, std::min<size_t>(ref.size(), kMaxDynamicLights));
        for (uint32_t i = 0; i < sel.count; ++i) {
            ASSERT_FLOAT_EQ(sel.lights[i].radius, ref[idx[i]].radius) << "iter " << iter << " slot " << i;
        }
    }
}
