#pragma once

#include "acoustics/AcousticOcclusion.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace betacraft::acoustics {

// Spec P4.2: occlusion rays are sampled at 10 Hz per active voice, then smoothed in the audio callback.
inline constexpr double OCCLUSION_UPDATE_INTERVAL_S = 0.1;

// One recomputed occlusion target, ready to be forwarded to the audio thread (e.g. as an
// `AudioCommand` through the MPSC queue). Trivially copyable on purpose.
struct OcclusionUpdate {
    uint32_t voiceHandle{0};
    float gain{1.0f};
    float cutoffHz{20000.0f};
    uint32_t solidVoxels{0};
    bool truncated{false};
    bool beyondAudibleRange{false}; // > R_max = 48 blocks: the integration may cull / not start it
};

// Rate limiter for occlusion raycasts, run on the game thread. Each source is re-evaluated at most
// once per interval (strict: consecutive evaluations of one source are >= interval apart), new
// sources are evaluated on the very next update so a fresh sound never plays unoccluded for 100 ms.
// Re-evaluations are load-spread: each update spends at most ceil(sources * dt / interval) + 1 rays
// on them (the average 10 Hz demand of the elapsed frame plus one), round-robin, so 32 sounds started
// in the same frame (an explosion, world load) fan out over a few frames and then stay staggered
// instead of costing a 32-ray burst every 100 ms. A long frame (dt >= interval) evaluates all of them.
// Time is injected (`nowSeconds`), which makes the whole class deterministic and testable.
// Fixed capacity, no allocation after construction.
class OcclusionScheduler {
public:
    static constexpr size_t CAPACITY = 32; // Matches the mixer's 32 voices

    explicit OcclusionScheduler(double intervalSeconds = OCCLUSION_UPDATE_INTERVAL_S,
                                const OcclusionParams& params = {}) noexcept;

    [[nodiscard]] double interval() const noexcept { return m_interval; }

    // False when full or when `handle` is already registered.
    [[nodiscard]] bool addSource(uint32_t handle, const glm::vec3& position) noexcept;
    bool removeSource(uint32_t handle) noexcept;
    // Position changes are picked up at the next due evaluation; they never bypass the rate limit.
    bool setSourcePosition(uint32_t handle, const glm::vec3& position) noexcept;
    void clear() noexcept;

    [[nodiscard]] size_t sourceCount() const noexcept { return m_count; }

    // Latest computed target, or nullopt if unknown handle / not evaluated yet.
    [[nodiscard]] std::optional<OcclusionTarget> target(uint32_t handle) const noexcept;

    // Target ramped from the value shown when the last evaluation landed to that evaluation's result,
    // over one interval: gain linearly, cutoff geometrically (perceived pitch is logarithmic). For
    // consumers that apply occlusion per frame/block instead of through a per-sample smoother; the
    // first evaluation snaps so a new sound never fades in from "unoccluded".
    [[nodiscard]] std::optional<OcclusionTarget> interpolatedTarget(uint32_t handle, double nowSeconds) const noexcept;

    // Re-evaluates every due source and returns the fresh results (valid until the next call).
    // A clock that jumps backwards only re-anchors the schedule; non-finite times are ignored.
    [[nodiscard]] std::span<const OcclusionUpdate> update(double nowSeconds, const glm::vec3& listener,
                                                          const VoxelSolidityQuery& query) noexcept;

private:
    struct Slot {
        uint32_t handle{0};
        glm::vec3 position{0.0f};
        double lastEvaluated{0.0};
        OcclusionTarget target{};
        OcclusionTarget rampStart{}; // Interpolated value at lastEvaluated (start of the current ramp)
        bool evaluated{false};
        bool active{false};
    };

    [[nodiscard]] Slot* find(uint32_t handle) noexcept;
    [[nodiscard]] const Slot* find(uint32_t handle) const noexcept;
    [[nodiscard]] OcclusionTarget interpolate(const Slot& slot, double nowSeconds) const noexcept;

    [[nodiscard]] size_t reevaluationBudget(double nowSeconds) const noexcept;

    std::array<Slot, CAPACITY> m_slots{};
    std::array<OcclusionUpdate, CAPACITY> m_updates{};
    OcclusionParams m_params;
    double m_interval;
    double m_lastUpdate{0.0};
    size_t m_count{0};
    size_t m_cursor{0}; // Round-robin start: sources deferred by the budget go first next time
    bool m_hasLastUpdate{false};
};

} // namespace betacraft::acoustics
