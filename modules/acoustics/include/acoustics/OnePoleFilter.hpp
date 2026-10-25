#pragma once

#include <cmath>
#include <cstddef>
#include <span>

namespace betacraft::acoustics {

inline constexpr float DEFAULT_SAMPLE_RATE = 44100.0f;
// Lowest cutoff the occlusion LPF may be driven to; keeps the coefficient strictly positive so the
// filter can never freeze on a stale value.
inline constexpr float MIN_FILTER_CUTOFF_HZ = 10.0f;
// The spec's "unoccluded" cutoff (clamp max). At and above it the LPF is an exact bypass (a = 1):
// an unoccluded voice must lose nothing, not ~1-3 dB in its top octave.
inline constexpr float BYPASS_CUTOFF_HZ = 20000.0f;
// Below this magnitude recursive states are flushed to 0: subnormal floats are up to 100x slower on
// x86 and would otherwise appear in every long decay tail.
inline constexpr float DENORMAL_FLUSH_THRESHOLD = 1.0e-20f;

[[nodiscard]] inline float flushDenormal(float v) noexcept {
    return std::fabs(v) < DENORMAL_FLUSH_THRESHOLD ? 0.0f : v;
}

// Coefficient of the one-pole EMA y += a * (x - y) whose -3 dB point is EXACTLY fc, so the spec's
// cutoffs (13 kHz for one block, 8.45 kHz for two, ...) mean what they say. Solving |H(e^jw)|^2 = 1/2
// gives a = sqrt(k (k + 2)) - k with k = 1 - cos(w) = 2 sin^2(w / 2), w = 2 pi fc / fs (the impulse-
// invariant 1 - exp(-w) is only -1 dB at 20 kHz and -1.9 dB at 13 kHz). Cutoffs below
// MIN_FILTER_CUTOFF_HZ are raised to it; cutoffs >= BYPASS_CUTOFF_HZ or >= fs/2 and NaN give 1
// (bypass); an invalid sample rate yields 1 so a misconfigured device degrades to "unfiltered"
// rather than "silent".
[[nodiscard]] float onePoleCoefficient(float cutoffHz, float sampleRate) noexcept;

// 1-pole low-pass ("EMA") used per voice for occlusion muffling (spec: LPF 1-pole EMA). Real-time
// safe: no allocation, no branches beyond input sanitising and denormal flushing.
class OnePoleLowPass {
public:
    explicit OnePoleLowPass(float sampleRate = DEFAULT_SAMPLE_RATE) noexcept
        : m_sampleRate(sampleRate), m_coeff(1.0f) {} // Transparent until a cutoff is set

    void setCutoff(float cutoffHz) noexcept { m_coeff = onePoleCoefficient(cutoffHz, m_sampleRate); }
    // Lets the voice smooth the coefficient itself (cheaper than smoothing Hz and calling exp per sample).
    void setCoefficient(float coeff) noexcept { m_coeff = coeff > 1.0f ? 1.0f : (coeff > 0.0f ? coeff : m_coeff); }
    [[nodiscard]] float coefficient() const noexcept { return m_coeff; }

    [[nodiscard]] float process(float x) noexcept {
        if (!std::isfinite(x)) {
            x = 0.0f; // One bad sample must not latch NaN into the recursion forever
        }
        m_state = flushDenormal(m_state + m_coeff * (x - m_state));
        return m_state;
    }

    void processBlock(std::span<float> inOut) noexcept {
        for (float& s : inOut) {
            s = process(s);
        }
    }

    void reset(float value = 0.0f) noexcept { m_state = value; }
    [[nodiscard]] float state() const noexcept { return m_state; }

private:
    float m_sampleRate;
    float m_coeff;
    float m_state{0.0f};
};

// Per-sample exponential smoother for control parameters (gain, LPF coefficient, reverb send) that
// arrive at 10 Hz: converts step changes into ~time-constant ramps so they never produce zipper noise.
class ParameterSmoother {
public:
    ParameterSmoother(float initial, float timeConstantSeconds, float sampleRate) noexcept;

    void setTarget(float target) noexcept {
        if (std::isfinite(target)) {
            m_target = target;
        }
    }
    void snap(float value) noexcept {
        if (std::isfinite(value)) {
            m_current = value;
            m_target = value;
        }
    }

    [[nodiscard]] float next() noexcept {
        const float previous = m_current;
        m_current += m_alpha * (m_target - m_current);
        // Float rounding can stall the EMA a few ulps short of the target; land on it instead.
        if (m_current == previous) {
            m_current = m_target;
        }
        settle();
        return m_current;
    }

    // Closed-form equivalent of `frames` calls to next(), for block-rate parameters.
    void advance(size_t frames) noexcept;

    [[nodiscard]] float current() const noexcept { return m_current; }
    [[nodiscard]] float target() const noexcept { return m_target; }
    [[nodiscard]] float alpha() const noexcept { return m_alpha; }

private:
    // Lands exactly on the target once inaudibly close, so steady-state voices stop drifting.
    void settle() noexcept {
        if (std::fabs(m_target - m_current) < 1.0e-6f) {
            m_current = m_target;
        }
    }

    float m_current;
    float m_target;
    float m_alpha;
};

} // namespace betacraft::acoustics
