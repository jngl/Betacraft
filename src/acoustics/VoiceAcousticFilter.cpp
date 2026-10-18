#include "acoustics/VoiceAcousticFilter.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::acoustics {

VoiceAcousticFilter::VoiceAcousticFilter(float sampleRate, float smoothingSeconds) noexcept
    : m_sampleRate(sampleRate),
      m_lpf(sampleRate),
      m_gain(1.0f, smoothingSeconds, sampleRate),
      m_coeff(onePoleCoefficient(OcclusionParams{}.maxCutoffHz, sampleRate), smoothingSeconds, sampleRate),
      m_send(DEFAULT_REVERB_SEND, smoothingSeconds, sampleRate) {
    m_lpf.setCoefficient(m_coeff.current());
}

void VoiceAcousticFilter::setOcclusionTarget(const OcclusionTarget& target) noexcept {
    if (std::isfinite(target.gain)) {
        m_gain.setTarget(std::clamp(target.gain, 0.0f, 1.0f));
    }
    if (!std::isnan(target.cutoffHz)) {
        m_coeff.setTarget(onePoleCoefficient(target.cutoffHz, m_sampleRate));
    }
}

void VoiceAcousticFilter::setReverbSend(float send) noexcept {
    if (std::isfinite(send)) {
        m_send.setTarget(std::clamp(send, 0.0f, 1.0f));
    }
}

void VoiceAcousticFilter::snapToTargets() noexcept {
    m_gain.snap(m_gain.target());
    m_coeff.snap(m_coeff.target());
    m_send.snap(m_send.target());
    m_lpf.setCoefficient(m_coeff.current());
}

} // namespace betacraft::acoustics
