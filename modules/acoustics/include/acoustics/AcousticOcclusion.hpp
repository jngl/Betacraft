#pragma once

#include "acoustics/VoxelDda.hpp"
#include "core/Types.hpp"

#include <cstdint>

namespace betacraft::acoustics {

// Abstract solidity oracle so the acoustic code never depends on World/Chunk internals (and can be
// unit-tested against a sparse fake grid). Implementations must be safe to call from the thread that
// runs the 10 Hz occlusion scheduler; the World-backed ones (WorldSolidity.hpp) are game-thread only.
class VoxelSolidityQuery {
public:
    VoxelSolidityQuery() = default;
    virtual ~VoxelSolidityQuery() = default;
    VoxelSolidityQuery(const VoxelSolidityQuery&) = default;
    VoxelSolidityQuery& operator=(const VoxelSolidityQuery&) = default;

    [[nodiscard]] virtual bool isSolid(const core::BlockPos& pos) const noexcept = 0;
};

// Raw result of one listener -> source ray.
struct OcclusionSample {
    uint32_t solidVoxels{0};    // Solid voxels strictly between listener and source voxels
    float solidThickness{0.0f}; // Path length (blocks) travelled inside those solid voxels
    float pathLength{0.0f};     // Euclidean listener -> source distance
    bool truncated{false};      // Ray exceeded the 144-step DDA budget (implies beyondAudibleRange)
    // Euclidean distance > MAX_AUDIBLE_RANGE (R_max = 48): the mixer should cull the voice. Distinct
    // from `truncated`, which only fires past 144 Manhattan steps (~83 blocks on a diagonal).
    bool beyondAudibleRange{false};
    bool valid{false};          // False for non-finite / out-of-range input
};

// Tunables of the occlusion model. Cutoff values are the spec's; the gain curve is not
// specified, so a gentle per-block attenuation with a floor keeps occluded sounds faintly audible.
struct OcclusionParams {
    float maxCutoffHz{20000.0f};
    float minCutoffHz{400.0f};
    float cutoffFactorPerBlock{0.65f};
    float gainFactorPerBlock{0.8f}; // ~ -1.9 dB per block of solid material
    float minGain{0.12f};           // ~ -18 dB floor
};

// What the audio thread should converge to for one voice.
struct OcclusionTarget {
    float gain{1.0f};
    float cutoffHz{20000.0f};
};

// Visits shorter than this (blocks of path length) are exact edge / corner ties: the ray only touches
// the voxel. Far above float noise for rays of <= 144 blocks, far below any audible thickness.
inline constexpr float TIE_EPSILON_BLOCKS = 1.0e-4f;

// Marches the voxel grid from listener to source (O(MAX_DDA_STEPS) time, O(1) space, no allocation).
// The listener and source voxels themselves are ignored: a head clipping into a block or a sound
// emitted by the block being broken must not muffle itself. Voxels only grazed at an exact edge or
// corner tie are not counted individually; the tie junction counts as ONE solid block iff every way
// through it is blocked (a diagonal seam of blocks is a wall, a lone grazed block is not). The result
// is therefore independent of the ray direction. Worst case (every step a 3-axis tie) costs
// 7/3 * MAX_DDA_STEPS solidity queries, still O(R_max).
[[nodiscard]] OcclusionSample measureOcclusion(const VoxelSolidityQuery& query, const glm::vec3& listener,
                                               const glm::vec3& source, uint32_t maxSteps = MAX_DDA_STEPS) noexcept;

// Spec: f_cutoff = clamp(20000 * 0.65^N, 400, 20000) Hz. Negative / NaN counts map to "unoccluded".
[[nodiscard]] float occlusionCutoffHz(float solidVoxels, const OcclusionParams& params = {}) noexcept;

// gain = max(minGain, gainFactorPerBlock^thickness). Negative / NaN thickness maps to 1.
[[nodiscard]] float occlusionGain(float solidThickness, const OcclusionParams& params = {}) noexcept;

// Cutoff follows the spec's per-block count; gain follows the actual thickness so that a ray grazing
// a block edge is attenuated less than one crossing it squarely.
[[nodiscard]] OcclusionTarget occlusionTargetFor(const OcclusionSample& sample,
                                                 const OcclusionParams& params = {}) noexcept;

// Facade (SpatialAudioEngine::computeVoxelOcclusion): one DDA ray, returned as the
// occlusion LPF cutoff in Hz, i.e. the value of AudioCommand::lpfCutoffHz. The listener is explicit
// because this module holds no engine state (the engine passes its m_listenerPos). Callers that also
// want the gain or the range flag use measureOcclusion() + occlusionTargetFor() directly.
[[nodiscard]] float computeVoxelOcclusion(const VoxelSolidityQuery& query, const glm::vec3& listener,
                                          const glm::vec3& soundPos, const OcclusionParams& params = {}) noexcept;

} // namespace betacraft::acoustics
