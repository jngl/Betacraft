#pragma once

#include "acoustics/EnclosureDetector.hpp"
#include "acoustics/OcclusionScheduler.hpp"

#include <optional>

namespace betacraft::acoustics {

// A reverb-environment change to forward to the audio thread (AcousticCommand::environment()).
struct EnvironmentUpdate {
    ReverbParams params{};
    bool snap{false}; // Jump instead of ramping: first sample, after reset() or a teleport
};

// Game-thread driver of the EnclosureDetector: re-probes the listener's surroundings at most once per
// interval (10 Hz like occlusion, injected clock) and only emits when the reverb parameters actually
// changed, so a player standing still in a cave costs 26 short rays per 100 ms and zero queue traffic.
// Fixed size, no allocation.
class EnclosureMonitor {
public:
    // Farther than this between two samples is a teleport / respawn, not walking (> 160 blocks/s).
    static constexpr float TELEPORT_DISTANCE = 16.0f;
    // Changes below this on every parameter are inaudible and not worth a queue message.
    static constexpr float MIN_PARAM_CHANGE = 1.0e-3f;

    explicit EnclosureMonitor(double intervalSeconds = OCCLUSION_UPDATE_INTERVAL_S,
                              const EnclosureParams& params = {}) noexcept;

    [[nodiscard]] double interval() const noexcept { return m_interval; }
    [[nodiscard]] const EnclosureDetector& detector() const noexcept { return m_detector; }

    // nullopt when not due, unchanged, or given a non-finite time / listener position.
    [[nodiscard]] std::optional<EnvironmentUpdate> update(double nowSeconds, const glm::vec3& listener,
                                                          const VoxelSolidityQuery& query) noexcept;

    // Forget history (world change): the next update() probes immediately and snaps.
    void reset() noexcept;

private:
    EnclosureDetector m_detector;
    double m_interval;
    double m_lastProbe{0.0};
    glm::vec3 m_lastListener{0.0f};
    ReverbParams m_lastSent{};
    bool m_hasSample{false};
};

} // namespace betacraft::acoustics
