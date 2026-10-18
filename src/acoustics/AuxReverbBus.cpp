#include "acoustics/AuxReverbBus.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::acoustics {

AuxReverbBus::AuxReverbBus(float sampleRate)
    : m_sampleRate(sampleRate),
      m_sendLevel(DEFAULT_SEND_LEVEL, PARAM_SMOOTHING_SECONDS, sampleRate),
      m_feedback(FdnReverbProcessor::DEFAULT_FEEDBACK, PARAM_SMOOTHING_SECONDS, sampleRate),
      m_damping(FdnReverbProcessor::DEFAULT_DAMPING, PARAM_SMOOTHING_SECONDS, sampleRate) {}

size_t AuxReverbBus::beginBlock(size_t frames) noexcept {
    m_frames = std::min(frames, MAX_BLOCK_FRAMES);
    std::fill_n(m_send.begin(), m_frames, 0.0f);
    return m_frames;
}

void AuxReverbBus::accumulate(std::span<const float> voiceMono, float sendGain) noexcept {
    if (!std::isfinite(sendGain)) {
        return;
    }
    const size_t n = std::min(voiceMono.size(), m_frames);
    m_droppedSendFrames += voiceMono.size() - n;
    for (size_t i = 0; i < n; ++i) {
        m_send[i] += voiceMono[i] * sendGain;
    }
}

void AuxReverbBus::processAndMix(std::span<float> interleavedStereoOut, size_t frames) noexcept {
    const size_t n = std::min({frames, m_frames, interleavedStereoOut.size() / 2});
    m_frames = 0; // Each accumulated block is consumed exactly once

    m_feedback.advance(n);
    m_damping.advance(n);
    m_fdn.setParameters(m_feedback.current(), m_damping.current());

    for (size_t i = 0; i < n; ++i) {
        m_send[i] *= m_sendLevel.next();
    }
    (void)m_fdn.processBlock(std::span<const float>(m_send.data(), n), std::span<float>(m_wetLeft.data(), n),
                             std::span<float>(m_wetRight.data(), n));
    for (size_t i = 0; i < n; ++i) {
        interleavedStereoOut[2 * i] += m_wetLeft[i];
        interleavedStereoOut[2 * i + 1] += m_wetRight[i];
    }
}

void AuxReverbBus::setReverbParams(const ReverbParams& params, bool snap) noexcept {
    const float send = std::clamp(params.sendLevel, 0.0f, MAX_SEND_LEVEL);
    const bool fromDecay = params.decaySeconds > 0.0f && std::isfinite(params.decaySeconds);
    const float requested = fromDecay ? feedbackForDecay(params.decaySeconds, m_sampleRate) : params.feedback;
    const float feedback = std::clamp(requested, 0.0f, FdnReverbProcessor::MAX_FEEDBACK);
    const float damping = std::clamp(params.damping, 0.0f, FdnReverbProcessor::MAX_DAMPING);
    if (snap) {
        m_sendLevel.snap(send);
        m_feedback.snap(feedback);
        m_damping.snap(damping);
        m_fdn.setParameters(m_feedback.current(), m_damping.current());
    } else {
        m_sendLevel.setTarget(send);
        m_feedback.setTarget(feedback);
        m_damping.setTarget(damping);
    }
}

void AuxReverbBus::reset() noexcept {
    m_fdn.reset();
    m_send.fill(0.0f);
    m_frames = 0;
}

} // namespace betacraft::acoustics
