#include "acoustics/FdnReverb.hpp"

#include "acoustics/OnePoleFilter.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::acoustics {

FdnReverbProcessor::FdnReverbProcessor() : m_storage(TOTAL_DELAY_SAMPLES, 0.0f) {
    size_t offset = 0;
    for (size_t i = 0; i < NUM_DELAY_LINES; ++i) {
        m_offsets[i] = offset;
        offset += DELAY_LENGTHS[i];
    }
}

void FdnReverbProcessor::setParameters(float feedback, float damping) noexcept {
    if (!std::isnan(feedback)) {
        m_feedbackGain = std::clamp(feedback, 0.0f, MAX_FEEDBACK);
    }
    if (!std::isnan(damping)) {
        m_dampingCoeff = std::clamp(damping, 0.0f, MAX_DAMPING);
    }
}

void FdnReverbProcessor::applyHouseholder(std::array<float, NUM_DELAY_LINES>& v) noexcept {
    float sum = 0.0f;
    for (const float x : v) {
        sum += x;
    }
    const float projection = sum * (2.0f / static_cast<float>(NUM_DELAY_LINES));
    for (float& x : v) {
        x -= projection;
    }
}

size_t FdnReverbProcessor::processBlock(std::span<const float> monoAuxInput, std::span<float> wetOutLeft,
                                        std::span<float> wetOutRight) noexcept {
    const size_t frames = std::min({monoAuxInput.size(), wetOutLeft.size(), wetOutRight.size()});
    float* const base = m_storage.data();
    const float g = m_feedbackGain;
    const float d = m_dampingCoeff;

    for (size_t f = 0; f < frames; ++f) {
        float in = monoAuxInput[f];
        if (!std::isfinite(in)) {
            in = 0.0f; // A NaN would otherwise circulate in the loop forever
        }

        std::array<float, NUM_DELAY_LINES> taps{};
        for (size_t i = 0; i < NUM_DELAY_LINES; ++i) {
            taps[i] = base[m_offsets[i] + m_cursors[i]];
        }
        // Wet stereo: even lines left, odd lines right (decorrelated, equal energy).
        wetOutLeft[f] = (taps[0] + taps[2] + taps[4] + taps[6]) * 0.25f;
        wetOutRight[f] = (taps[1] + taps[3] + taps[5] + taps[7]) * 0.25f;

        applyHouseholder(taps);
        for (size_t i = 0; i < NUM_DELAY_LINES; ++i) {
            m_filterState[i] = flushDenormal(taps[i] * (1.0f - d) + m_filterState[i] * d);
            base[m_offsets[i] + m_cursors[i]] = in + m_filterState[i] * g;
            if (++m_cursors[i] == DELAY_LENGTHS[i]) {
                m_cursors[i] = 0; // Branch instead of '%': no integer division in the hot loop
            }
        }
    }
    return frames;
}

void FdnReverbProcessor::reset() noexcept {
    std::fill(m_storage.begin(), m_storage.end(), 0.0f);
    m_filterState.fill(0.0f);
    m_cursors.fill(0);
}

double FdnReverbProcessor::energy() const noexcept {
    double e = 0.0;
    for (const float x : m_storage) {
        e += static_cast<double>(x) * x;
    }
    for (const float x : m_filterState) {
        e += static_cast<double>(x) * x;
    }
    return e;
}

bool FdnReverbProcessor::hasSubnormalState() const noexcept {
    const auto subnormal = [](float x) { return std::fpclassify(x) == FP_SUBNORMAL; };
    return std::any_of(m_storage.begin(), m_storage.end(), subnormal) ||
           std::any_of(m_filterState.begin(), m_filterState.end(), subnormal);
}

} // namespace betacraft::acoustics
