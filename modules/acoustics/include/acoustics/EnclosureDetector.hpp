#pragma once

#include "acoustics/AcousticOcclusion.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace betacraft::acoustics {

// Tunables of the enclosure -> reverb mapping (the roadmap gives no numbers; these are chosen so that
// open terrain stays nearly dry and a large cave gets a multi-second tail).
struct EnclosureParams {
    float probeRange{24.0f};      // Blocks; reaches the corners of a ~25-block-wide cavern
    // Bus send multipliers (AuxReverbBus: 1 = the spec's neutral per-voice send of 0.15 unchanged).
    float minSend{0.25f};         // Outdoors: ground reflections only (0.15 x 0.25 ~ 0.04 effective)
    float maxSend{1.2f};          // Fully enclosed cave (0.15 x 1.2 = 0.18 effective)
    float minDecaySeconds{0.4f};  // RT60 outdoors / tiny pocket
    float maxDecaySeconds{4.5f};  // RT60 of a huge cave
    float openDamping{0.6f};      // Dull, short tails in small or open spaces
    float caveDamping{0.25f};     // Large stone halls keep more highs
    // Wetness kept when the straight-up probe escapes (skylight, ravine, open pit): energy leaks
    // out of the ceiling hole, so the room rings less than a sealed one with the same hit fraction.
    float skyOpenWetScale{0.6f};
};

// Geometry seen from the listener by NUM_PROBES first-hit rays.
struct EnclosureSample {
    float enclosure{0.0f};      // Fraction of probes that hit a solid voxel within range, [0, 1]
    float meanFreePath{0.0f};   // Mean hit distance over all probes (misses count as probeRange)
    float minHitDistance{0.0f}; // Closest hit (probeRange if none)
    uint32_t hits{0};
    bool skyOpen{true};         // The straight-up probe escaped (scales wetness by skyOpenWetScale)
    bool valid{false};
};

// Parameters for the shared aux bus: sendLevel multiplies every voice's send (1 = neutral),
// feedback/damping go to FdnReverbProcessor::setParameters().
struct ReverbParams {
    float sendLevel{0.0f};
    float feedback{0.0f};
    float damping{0.0f};
    float decaySeconds{0.0f};
};

// FDN loop gain giving the requested RT60 on the shared FDN: a sample loses 60 dB after
// RT60 * fs / meanDelay passes, so g = 10^(-3 * meanDelay / (RT60 * fs)). Clamped to the FDN's stable
// range; non-positive / NaN decay yields 0.
[[nodiscard]] float feedbackForDecay(float rt60Seconds, float sampleRate = 44100.0f) noexcept;

// Cave detection by casting 26 rays (every direction of the 3x3x3 neighbourhood) from the listener.
// Bounded cost: <= 26 * 3 * probeRange solidity queries, no allocation. Meant to run at the same
// 10 Hz cadence as the occlusion scheduler (the result is smoothed on the audio side).
class EnclosureDetector {
public:
    static constexpr size_t NUM_PROBES = 26;

    explicit EnclosureDetector(const EnclosureParams& params = {}) noexcept;

    [[nodiscard]] static const std::array<glm::vec3, NUM_PROBES>& probeDirections() noexcept;
    [[nodiscard]] const EnclosureParams& params() const noexcept { return m_params; }

    // The listener's own voxel is ignored (head clipping into a block is not "being in a cave").
    [[nodiscard]] EnclosureSample probe(const glm::vec3& listener, const VoxelSolidityQuery& query) const noexcept;
    [[nodiscard]] ReverbParams reverbFor(const EnclosureSample& sample) const noexcept;
    [[nodiscard]] ReverbParams analyze(const glm::vec3& listener, const VoxelSolidityQuery& query) const noexcept {
        return reverbFor(probe(listener, query));
    }

private:
    EnclosureParams m_params;
};

} // namespace betacraft::acoustics
