#include "acoustics/AcousticOcclusion.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace betacraft::acoustics {

namespace {

[[nodiscard]] core::BlockPos offset(const core::BlockPos& p, const core::BlockPos& d) noexcept {
    return core::BlockPos{p.x + d.x, p.y + d.y, p.z + d.z};
}

// At an exact edge / corner tie the ray leaves voxel `from` and enters voxel `to` through a single
// point, two or three axes changing at once. The DDA's X<Y<Z tie-break picks one intermediate voxel,
// the reversed ray another, and neither holds any path length. The junction is treated as sealed iff
// EVERY monotone lattice path through it crosses a solid intermediate: symmetric under reversing the
// ray, a lone grazed block does not muffle, and a diagonal seam of blocks (a wall in-game) stays
// airtight. At most 6 queries per junction.
[[nodiscard]] bool junctionSealed(const VoxelSolidityQuery& query, const core::BlockPos& from,
                                  const core::BlockPos& to) noexcept {
    std::array<core::BlockPos, 3> steps{};
    size_t axes = 0;
    const std::array<int32_t, 3> d{to.x - from.x, to.y - from.y, to.z - from.z};
    for (size_t i = 0; i < 3; ++i) {
        if (d[i] == 0) {
            continue;
        }
        if (d[i] < -1 || d[i] > 1) {
            return false; // Not a tie junction (cannot happen with a 6-connected walk)
        }
        core::BlockPos step{0, 0, 0};
        (i == 0 ? step.x : (i == 1 ? step.y : step.z)) = d[i];
        steps[axes++] = step;
    }
    if (axes == 2) {
        return query.isSolid(offset(from, steps[0])) && query.isSolid(offset(from, steps[1]));
    }
    if (axes != 3) {
        return false;
    }
    std::array<bool, 3> single{};
    for (size_t i = 0; i < 3; ++i) {
        single[i] = query.isSolid(offset(from, steps[i]));
    }
    // pair[k] = the voxel reached by the two steps other than k.
    std::array<bool, 3> pair{};
    for (size_t k = 0; k < 3; ++k) {
        pair[k] = query.isSolid(offset(offset(from, steps[(k + 1) % 3]), steps[(k + 2) % 3]));
    }
    // Path (i, j, k) visits single[i] then the voxel of steps i + j, i.e. pair[k].
    for (size_t i = 0; i < 3; ++i) {
        for (size_t k = 0; k < 3; ++k) {
            if (k != i && !single[i] && !pair[k]) {
                return false; // An open path slips through the corner
            }
        }
    }
    return true;
}

// One full-length ray fits in a single batch; longer custom budgets flush mid-traversal.
constexpr size_t VISIT_BATCH = MAX_DDA_STEPS + 16;

struct RecordedVisit {
    core::BlockPos voxel;
    float inside; // Parametric length inside the voxel
};

// Consumes the visited voxels in order. The endpoints are positional: the listener voxel is the first
// visit and the source voxel the last one of a ray that reached its end, so the hot loop needs no
// per-voxel endpoint test; the voxel before a tie junction is simply the previous batch entry.
class OcclusionAccumulator {
public:
    OcclusionAccumulator(const VoxelSolidityQuery& query, float length) noexcept
        : m_query(query), m_grazedT(length > 0.0f ? TIE_EPSILON_BLOCKS / length : 2.0f) {}

    // Occluder candidates batch[from, to); batch[from - 1] must be the visit just before batch[from].
    void consumeInterior(const RecordedVisit* batch, size_t from, size_t to) noexcept {
        for (size_t i = from; i < to; ++i) {
            const RecordedVisit& v = batch[i];
            if (v.inside < m_grazedT) [[unlikely]] {
                if (!m_inJunction) {
                    m_inJunction = true;
                    m_junctionStart = batch[i - 1].voxel;
                }
                continue;
            }
            resolveJunction(v.voxel);
            // Solidity is random along a ray: accumulate with selects, not a mispredicted branch.
            const bool solid = m_query.isSolid(v.voxel);
            m_solidVoxels += solid ? 1u : 0u;
            m_thicknessT += solid ? v.inside : 0.0f;
        }
    }

    // The source voxel: closes a pending junction but is never an occluder itself.
    void consumeEndpoint(const core::BlockPos& voxel) noexcept { resolveJunction(voxel); }

    [[nodiscard]] uint32_t solidVoxels() const noexcept { return m_solidVoxels; }
    [[nodiscard]] float thicknessT() const noexcept { return m_thicknessT; }

private:
    void resolveJunction(const core::BlockPos& entered) noexcept {
        if (m_inJunction) [[unlikely]] {
            m_inJunction = false;
            m_solidVoxels += junctionSealed(m_query, m_junctionStart, entered) ? 1u : 0u;
        }
    }

    const VoxelSolidityQuery& m_query;
    float m_grazedT; // Parametric length below which a visit is a tie artefact
    core::BlockPos m_junctionStart{0, 0, 0};
    uint32_t m_solidVoxels{0};
    float m_thicknessT{0.0f};
    bool m_inJunction{false};
};

} // namespace

OcclusionSample measureOcclusion(const VoxelSolidityQuery& query, const glm::vec3& listener, const glm::vec3& source,
                                 uint32_t maxSteps) noexcept {
    const glm::vec3 delta = source - listener;
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    OcclusionAccumulator acc(query, length);

    // Two passes over a stack batch instead of querying inside the DDA: a (virtual) call per voxel
    // would force the DDA's floating-point state out of registers (all SSE registers are caller-saved)
    // on every step. ~2.5 KB of stack, no heap.
    std::array<RecordedVisit, VISIT_BATCH> batch;
    size_t pending = 0;
    size_t firstCandidate = 1; // Index 0 of the first batch is the listener voxel
    TraversalResult r;
    if (maxSteps < VISIT_BATCH) {
        // At most maxSteps + 1 <= VISIT_BATCH visits (always the case for the spec's 144): the visitor
        // stays a couple of stores, so the compiler inlines it into the DDA loop.
        r = traverseVoxels(
            listener, source,
            [&](const core::BlockPos& voxel, float tEnter, float tExit) noexcept {
                batch[pending++] = RecordedVisit{voxel, tExit - tEnter};
                return true;
            },
            maxSteps);
    } else {
        r = traverseVoxels(
            listener, source,
            [&](const core::BlockPos& voxel, float tEnter, float tExit) noexcept {
                batch[pending++] = RecordedVisit{voxel, tExit - tEnter};
                if (pending == batch.size()) {
                    // Long custom budget: consume all but the newest visit (it may turn out to be the
                    // source voxel). Carry it over unconsumed, preceded by its (consumed) predecessor.
                    acc.consumeInterior(batch.data(), firstCandidate, pending - 1);
                    batch[0] = batch[pending - 2];
                    batch[1] = batch[pending - 1];
                    pending = 2;
                    firstCandidate = 1;
                }
                return true;
            },
            maxSteps);
    }

    if (r.visitedCount == 0) {
        return OcclusionSample{}; // Invalid input: report as unoccluded and invalid
    }
    if (r.reachedEnd) {
        if (r.visitedCount > 1) {
            acc.consumeInterior(batch.data(), firstCandidate, pending - 1);
            acc.consumeEndpoint(batch[pending - 1].voxel);
        }
    } else {
        acc.consumeInterior(batch.data(), firstCandidate, pending); // Truncated: no source voxel
    }

    OcclusionSample sample;
    sample.solidVoxels = acc.solidVoxels();
    sample.valid = true;
    sample.truncated = r.truncated;
    sample.beyondAudibleRange = length > static_cast<float>(MAX_AUDIBLE_RANGE);
    sample.pathLength = length;
    sample.solidThickness = acc.thicknessT() * length;
    return sample;
}

float computeVoxelOcclusion(const VoxelSolidityQuery& query, const glm::vec3& listener, const glm::vec3& soundPos,
                            const OcclusionParams& params) noexcept {
    return occlusionTargetFor(measureOcclusion(query, listener, soundPos), params).cutoffHz;
}

float occlusionCutoffHz(float solidVoxels, const OcclusionParams& params) noexcept {
    if (!(solidVoxels > 0.0f)) { // Also catches NaN
        return params.maxCutoffHz;
    }
    const float raw = params.maxCutoffHz * std::pow(params.cutoffFactorPerBlock, solidVoxels);
    return std::clamp(raw, params.minCutoffHz, params.maxCutoffHz);
}

float occlusionGain(float solidThickness, const OcclusionParams& params) noexcept {
    if (!(solidThickness > 0.0f)) {
        return 1.0f;
    }
    return std::clamp(std::pow(params.gainFactorPerBlock, solidThickness), params.minGain, 1.0f);
}

OcclusionTarget occlusionTargetFor(const OcclusionSample& sample, const OcclusionParams& params) noexcept {
    if (!sample.valid) {
        return OcclusionTarget{1.0f, params.maxCutoffHz};
    }
    return OcclusionTarget{occlusionGain(sample.solidThickness, params),
                           occlusionCutoffHz(static_cast<float>(sample.solidVoxels), params)};
}

} // namespace betacraft::acoustics
