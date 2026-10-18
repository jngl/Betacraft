#pragma once

#include "lighting/DynamicLight.hpp"
#include "math/Frustum.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace betacraft::lighting {

// The (at most kMaxDynamicLights) lights chosen for upload this frame, nearest first.
struct LightSelection {
    std::array<DynamicLight, kMaxDynamicLights> lights{};
    uint32_t count{0};

    [[nodiscard]] std::span<const DynamicLight> view() const noexcept { return {lights.data(), count}; }
};

// Collects every dynamic light source of the frame (held item, dropped glowing items, burning
// entities...) and picks the ones that fit in the UBO. Usage per frame (game thread only):
//   clear() -> submit*() from each system -> select(camera) -> pack/upload.
class DynamicLightManager {
public:
    // Hard bound so a pathological item-drop farm cannot grow the per-frame list without limit.
    static constexpr size_t kMaxPendingLights = 1024;
    // The last slots of that bound are reserved for LightSource::HeldItem: world lights are
    // refused first, so a farm submitted before the hand can never evict the held torch before
    // select() gets to rank by distance.
    static constexpr size_t kReservedHeldSlots = kMaxDynamicLights;
    static_assert(kReservedHeldSlots < kMaxPendingLights);

    // Held light offset from the eyes, in blocks: roughly where the first-person hand is drawn.
    static constexpr float kHeldForward = 0.5f;
    static constexpr float kHeldRight = 0.35f;
    static constexpr float kHeldDown = 0.3f;

    DynamicLightManager();

    void clear() noexcept { m_lights.clear(); }

    // Returns false (and drops the light) when it is invalid or the pending list is full
    // (kMaxPendingLights - kReservedHeldSlots for world lights, kMaxPendingLights for held ones).
    bool submit(const DynamicLight& light);

    // Where a held light sits for these eyes: forward, right and down by the kHeld* offsets.
    // Degenerate or vertical look directions fall back to -Z forward / +X right, never NaN.
    [[nodiscard]] static glm::vec3 heldLightPosition(const glm::vec3& eyePos, const glm::vec3& lookDir) noexcept;

    // Light from the item in the player's hand; false when the item does not glow.
    bool submitHeldItem(const glm::vec3& eyePos, const glm::vec3& lookDir, world::BlockId heldItem);

    // Light from a glowing item entity lying in the world; false when it does not glow.
    bool submitDroppedItem(const glm::vec3& position, world::BlockId item);

    [[nodiscard]] size_t size() const noexcept { return m_lights.size(); }
    [[nodiscard]] std::span<const DynamicLight> lights() const noexcept { return m_lights; }

    // Keeps the `capacity` (clamped to kMaxDynamicLights) lights nearest to `cameraPos`, with
    // ties broken by submission order so the result is fully deterministic. When `frustum` is
    // given, lights whose sphere cannot touch the view volume are skipped first so they never
    // steal a slot from a visible one.
    [[nodiscard]] LightSelection select(const glm::vec3& cameraPos, const math::Frustum* frustum = nullptr,
                                        size_t capacity = kMaxDynamicLights) const noexcept;

private:
    std::vector<DynamicLight> m_lights;
};

} // namespace betacraft::lighting
