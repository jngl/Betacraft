#pragma once

#include <array>
#include <cstddef>
#include <numeric>
#include <span>
#include <vector>

namespace betacraft::acoustics {

// 8-line Feedback Delay Network (Jot / Householder) for the ONE shared auxiliary reverb bus.
// All 32 voices send into a single instance, so the whole delay memory
// (16 310 floats = 65 240 bytes) stays resident in L2 instead of 32 x 65 KB per-voice copies.
//
// Per sample: read the 8 delay outputs, mix them through the orthogonal Householder matrix
// H = I - (2/N) 11^T (O(N) instead of O(N^2)), low-pass each line (damping: highs die first, like rock
// absorbing treble), scale by the loop gain g < 1 and write back with the mono send added. Because H
// is orthogonal and every per-line filter has |H(f)| <= 1, the loop gain is bounded by g and the
// network is unconditionally stable for g <= MAX_FEEDBACK < 1.
//
// Real-time contract: processBlock() never allocates, locks or throws; the buffer is allocated once
// in the constructor.
class FdnReverbProcessor {
public:
    static constexpr size_t NUM_DELAY_LINES = 8;
    // Mutually prime lengths at 44.1 kHz (24.6 ms .. 72.3 ms): no common period, so echoes never
    // stack into audible metallic resonances.
    static constexpr std::array<size_t, NUM_DELAY_LINES> DELAY_LENGTHS = {1087, 1283, 1523, 1823,
                                                                          2137, 2467, 2803, 3187};
    static constexpr size_t TOTAL_DELAY_SAMPLES = std::accumulate(DELAY_LENGTHS.begin(), DELAY_LENGTHS.end(), size_t{0});
    static constexpr float MAX_FEEDBACK = 0.98f;
    static constexpr float MAX_DAMPING = 0.85f;
    static constexpr float DEFAULT_FEEDBACK = 0.84f;
    static constexpr float DEFAULT_DAMPING = 0.30f;

    FdnReverbProcessor();

    // feedback = loop gain (decay length), damping = per-line one-pole coefficient (0 = bright).
    // Values are clamped to the stable range; NaN keeps the previous value.
    void setParameters(float feedback, float damping) noexcept;
    [[nodiscard]] float feedbackGain() const noexcept { return m_feedbackGain; }
    [[nodiscard]] float damping() const noexcept { return m_dampingCoeff; }

    // Consumes the mono aux send and OVERWRITES the stereo wet outputs. Processes
    // min(input, left, right) frames and returns that count.
    size_t processBlock(std::span<const float> monoAuxInput, std::span<float> wetOutLeft,
                        std::span<float> wetOutRight) noexcept;

    void reset() noexcept;

    [[nodiscard]] size_t allocatedSampleCount() const noexcept { return m_storage.size(); }
    [[nodiscard]] size_t memoryBytes() const noexcept { return m_storage.size() * sizeof(float); }

    // Sum of squares of every internal state (delay memory + damping filters); for diagnostics/tests.
    [[nodiscard]] double energy() const noexcept;
    [[nodiscard]] bool hasSubnormalState() const noexcept;

    // In-place y = (I - (2/N) 11^T) x.
    static void applyHouseholder(std::array<float, NUM_DELAY_LINES>& v) noexcept;

private:
    std::vector<float> m_storage; // All 8 lines back to back: one allocation, contiguous for the cache
    std::array<size_t, NUM_DELAY_LINES> m_offsets{};
    std::array<size_t, NUM_DELAY_LINES> m_cursors{};
    std::array<float, NUM_DELAY_LINES> m_filterState{};
    float m_feedbackGain{DEFAULT_FEEDBACK};
    float m_dampingCoeff{DEFAULT_DAMPING};
};

} // namespace betacraft::acoustics
