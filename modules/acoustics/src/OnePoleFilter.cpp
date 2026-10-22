#include "acoustics/OnePoleFilter.hpp"

#include <algorithm>
#include <numbers>

namespace betacraft::acoustics {

float onePoleCoefficient(float cutoffHz, float sampleRate) noexcept {
    if (!(sampleRate > 0.0f) || !std::isfinite(sampleRate)) {
        return 1.0f;
    }
    if (std::isnan(cutoffHz) || cutoffHz >= BYPASS_CUTOFF_HZ || cutoffHz >= sampleRate * 0.5f) {
        return 1.0f;
    }
    // Double precision: k ~ 5e-8 at 10 Hz, where float cos() alone would be off by tens of percent.
    const double fc = std::max(cutoffHz, MIN_FILTER_CUTOFF_HZ);
    const double halfW = std::numbers::pi * fc / static_cast<double>(sampleRate);
    const double s = std::sin(halfW);
    const double k = 2.0 * s * s;
    return static_cast<float>(std::sqrt(k * (k + 2.0)) - k);
}

ParameterSmoother::ParameterSmoother(float initial, float timeConstantSeconds, float sampleRate) noexcept
    : m_current(std::isfinite(initial) ? initial : 0.0f), m_target(m_current), m_alpha(1.0f) {
    const float samples = timeConstantSeconds * sampleRate;
    if (samples > 1.0e-3f && std::isfinite(samples)) {
        m_alpha = 1.0f - std::exp(-1.0f / samples);
    }
}

void ParameterSmoother::advance(size_t frames) noexcept {
    if (frames == 0) {
        return;
    }
    const float decay = std::pow(1.0f - m_alpha, static_cast<float>(frames));
    m_current = m_target + (m_current - m_target) * decay;
    settle();
}

} // namespace betacraft::acoustics
