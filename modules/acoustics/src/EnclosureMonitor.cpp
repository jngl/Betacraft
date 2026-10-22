#include "acoustics/EnclosureMonitor.hpp"

#include <cmath>

namespace betacraft::acoustics {

namespace {

constexpr double TIME_EPSILON = 1.0e-9; // Frame timestamps are sums of 1/fps: absorb rounding

[[nodiscard]] bool finite(const glm::vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] bool audiblyDifferent(const ReverbParams& a, const ReverbParams& b) noexcept {
    return std::fabs(a.sendLevel - b.sendLevel) >= EnclosureMonitor::MIN_PARAM_CHANGE ||
           std::fabs(a.feedback - b.feedback) >= EnclosureMonitor::MIN_PARAM_CHANGE ||
           std::fabs(a.damping - b.damping) >= EnclosureMonitor::MIN_PARAM_CHANGE;
}

} // namespace

EnclosureMonitor::EnclosureMonitor(double intervalSeconds, const EnclosureParams& params) noexcept
    : m_detector(params),
      m_interval((std::isfinite(intervalSeconds) && intervalSeconds > 0.0) ? intervalSeconds
                                                                           : OCCLUSION_UPDATE_INTERVAL_S) {}

std::optional<EnvironmentUpdate> EnclosureMonitor::update(double nowSeconds, const glm::vec3& listener,
                                                          const VoxelSolidityQuery& query) noexcept {
    if (!std::isfinite(nowSeconds) || !finite(listener)) {
        return std::nullopt;
    }
    if (m_hasSample) {
        if (nowSeconds < m_lastProbe) {
            m_lastProbe = nowSeconds; // Clock went backwards: re-anchor without a burst
            return std::nullopt;
        }
        if (nowSeconds - m_lastProbe < m_interval - TIME_EPSILON) {
            return std::nullopt;
        }
    }

    const glm::vec3 moved = listener - m_lastListener;
    const bool snap = !m_hasSample || glm::dot(moved, moved) > TELEPORT_DISTANCE * TELEPORT_DISTANCE;
    const ReverbParams params = m_detector.analyze(listener, query);
    const bool changed = !m_hasSample || audiblyDifferent(params, m_lastSent);

    m_lastProbe = nowSeconds;
    m_lastListener = listener;
    m_hasSample = true;
    if (!changed) {
        return std::nullopt;
    }
    m_lastSent = params;
    return EnvironmentUpdate{params, snap};
}

void EnclosureMonitor::reset() noexcept {
    m_hasSample = false;
    m_lastProbe = 0.0;
    m_lastSent = ReverbParams{};
}

} // namespace betacraft::acoustics
