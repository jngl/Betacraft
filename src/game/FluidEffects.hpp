#pragma once

#include "fluids/FluidSimulator.hpp"

#include <glm/glm.hpp>

#include <optional>
#include <vector>

namespace betacraft::game {

// Beta's sound attenuation distance: a sound at volume 1 fades out linearly over 16 blocks.
inline constexpr float kFizzAudibleRange = 16.0f;

// The one fizz Application plays for a game tick's fluid events: the nearest lava mix wins, so a
// settling cave that hardens hundreds of cells does not flood the audio queue. It plays at the
// contact voxel's centre (occlusion and reverb come from there) with a gain falling off linearly
// with distance, since the mixer has no distance attenuation of its own.
struct FizzSound {
    glm::vec3 position{0.0f};
    float gain{0.0f}; // (0, 1]
};

[[nodiscard]] inline std::optional<FizzSound> loudestFizzSound(const std::vector<fluids::FluidEvent>& events,
                                                               const glm::vec3& listener) noexcept {
    std::optional<FizzSound> loudest;
    for (const fluids::FluidEvent& ev : events) {
        if (ev.kind != fluids::FluidEventKind::Fizz) {
            continue;
        }
        const glm::vec3 centre = glm::vec3(static_cast<float>(ev.pos.x), static_cast<float>(ev.pos.y),
                                           static_cast<float>(ev.pos.z)) + glm::vec3(0.5f);
        const float gain = 1.0f - glm::length(centre - listener) / kFizzAudibleRange;
        if (gain > 0.0f && (!loudest || gain > loudest->gain)) {
            loudest = FizzSound{centre, gain};
        }
    }
    return loudest;
}

// Gain (0..1) of loudestFizzSound, 0 when nothing fizzes within range.
[[nodiscard]] inline float loudestFizz(const std::vector<fluids::FluidEvent>& events, const glm::vec3& listener) noexcept {
    const std::optional<FizzSound> fizz = loudestFizzSound(events, listener);
    return fizz ? fizz->gain : 0.0f;
}

} // namespace betacraft::game
