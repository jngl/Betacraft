#include "lighting/DynamicLightManager.hpp"

#include <algorithm>

namespace betacraft::lighting {

namespace {

constexpr float kDegenerateEpsilon = 1e-6f;

struct Candidate {
    float distSq;
    uint32_t index;
};

// Strict total order: nearest first, then submission order.
[[nodiscard]] bool closer(const Candidate& a, const Candidate& b) noexcept {
    if (a.distSq != b.distSq) {
        return a.distSq < b.distSq;
    }
    return a.index < b.index;
}

} // namespace

DynamicLightManager::DynamicLightManager() {
    // Reserve up front so typical frames never allocate on the game thread.
    m_lights.reserve(64);
}

bool DynamicLightManager::submit(const DynamicLight& light) {
    const size_t cap =
        light.source == LightSource::HeldItem ? kMaxPendingLights : kMaxPendingLights - kReservedHeldSlots;
    if (!light.isValid() || m_lights.size() >= cap) {
        return false;
    }
    m_lights.push_back(light);
    return true;
}

glm::vec3 DynamicLightManager::heldLightPosition(const glm::vec3& eyePos, const glm::vec3& lookDir) noexcept {
    glm::vec3 forward = lookDir;
    const float len = glm::length(forward);
    forward = len > kDegenerateEpsilon ? forward / len : glm::vec3(0.0f, 0.0f, -1.0f);

    // Right vector from the horizontal plane; looking straight up/down has no defined yaw, so
    // fall back to +X rather than producing NaN.
    glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
    const float rightLen = glm::length(right);
    right = rightLen > kDegenerateEpsilon ? right / rightLen : glm::vec3(1.0f, 0.0f, 0.0f);

    return eyePos + forward * kHeldForward + right * kHeldRight - glm::vec3(0.0f, kHeldDown, 0.0f);
}

bool DynamicLightManager::submitHeldItem(const glm::vec3& eyePos, const glm::vec3& lookDir,
                                         voxel::BlockId heldItem) {
    const auto light = makeEmitterLight(heldItem, heldLightPosition(eyePos, lookDir), LightSource::HeldItem);
    return light.has_value() && submit(*light);
}

bool DynamicLightManager::submitDroppedItem(const glm::vec3& position, voxel::BlockId item) {
    const auto light = makeEmitterLight(item, position, LightSource::DroppedItem);
    return light.has_value() && submit(*light);
}

LightSelection DynamicLightManager::select(const glm::vec3& cameraPos, const math::Frustum* frustum,
                                           size_t capacity) const noexcept {
    const size_t cap = std::min<size_t>(capacity, kMaxDynamicLights);
    LightSelection result;
    if (cap == 0) {
        return result;
    }

    // Bounded insertion into a sorted top-k array: O(n * k) with k <= 16, no allocation.
    std::array<Candidate, kMaxDynamicLights> best{};
    size_t bestCount = 0;
    for (size_t i = 0; i < m_lights.size(); ++i) {
        const DynamicLight& l = m_lights[i];
        if (frustum != nullptr) {
            const glm::vec3 extent(l.radius);
            if (!frustum->intersectsAABB(l.position - extent, l.position + extent)) {
                continue;
            }
        }
        const glm::vec3 delta = l.position - cameraPos;
        const Candidate c{glm::dot(delta, delta), static_cast<uint32_t>(i)};
        if (bestCount == cap && !closer(c, best[cap - 1])) {
            continue;
        }
        size_t slot = bestCount < cap ? bestCount++ : cap - 1;
        while (slot > 0 && closer(c, best[slot - 1])) {
            best[slot] = best[slot - 1];
            --slot;
        }
        best[slot] = c;
    }

    for (size_t i = 0; i < bestCount; ++i) {
        result.lights[i] = m_lights[best[i].index];
    }
    result.count = static_cast<uint32_t>(bestCount);
    return result;
}

} // namespace betacraft::lighting
