#include "acoustics/EnclosureDetector.hpp"

#include "acoustics/FdnReverb.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::acoustics {

namespace {

constexpr float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

std::array<glm::vec3, EnclosureDetector::NUM_PROBES> buildProbeDirections() noexcept {
    std::array<glm::vec3, EnclosureDetector::NUM_PROBES> dirs{};
    size_t n = 0;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dy == 0 && dz == 0) {
                    continue;
                }
                dirs[n++] = glm::normalize(glm::vec3{static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)});
            }
        }
    }
    return dirs;
}

} // namespace

float feedbackForDecay(float rt60Seconds, float sampleRate) noexcept {
    if (!(rt60Seconds > 0.0f) || !(sampleRate > 0.0f)) {
        return 0.0f;
    }
    constexpr float meanDelay = static_cast<float>(FdnReverbProcessor::TOTAL_DELAY_SAMPLES) /
                                static_cast<float>(FdnReverbProcessor::NUM_DELAY_LINES);
    const float g = std::pow(10.0f, -3.0f * meanDelay / (rt60Seconds * sampleRate));
    return std::clamp(g, 0.0f, FdnReverbProcessor::MAX_FEEDBACK);
}

EnclosureDetector::EnclosureDetector(const EnclosureParams& params) noexcept : m_params(params) {
    if (!(m_params.probeRange > 0.0f) || !std::isfinite(m_params.probeRange)) {
        m_params.probeRange = EnclosureParams{}.probeRange;
    }
}

const std::array<glm::vec3, EnclosureDetector::NUM_PROBES>& EnclosureDetector::probeDirections() noexcept {
    static const std::array<glm::vec3, NUM_PROBES> dirs = buildProbeDirections();
    return dirs;
}

EnclosureSample EnclosureDetector::probe(const glm::vec3& listener, const VoxelSolidityQuery& query) const noexcept {
    const float range = m_params.probeRange;
    EnclosureSample sample;
    sample.meanFreePath = range;
    sample.minHitDistance = range;
    if (!std::isfinite(listener.x) || !std::isfinite(listener.y) || !std::isfinite(listener.z)) {
        return sample;
    }

    const auto& dirs = probeDirections();
    float distanceSum = 0.0f;
    sample.skyOpen = false; // Set again below if the straight-up probe escapes
    for (size_t p = 0; p < NUM_PROBES; ++p) {
        float hitDistance = range;
        bool first = true;
        (void)traverseVoxels(
            listener, listener + dirs[p] * range,
            [&](const core::BlockPos& voxel, float tEnter, float) noexcept {
                if (first) {
                    first = false; // Listener voxel
                    return true;
                }
                if (query.isSolid(voxel)) {
                    hitDistance = tEnter * range;
                    return false;
                }
                return true;
            },
            MAX_DDA_STEPS);

        if (hitDistance < range) {
            ++sample.hits;
            sample.minHitDistance = std::min(sample.minHitDistance, hitDistance);
        } else if (dirs[p].y > 0.99f) {
            sample.skyOpen = true;
        }
        distanceSum += hitDistance;
    }

    sample.valid = true;
    sample.enclosure = static_cast<float>(sample.hits) / static_cast<float>(NUM_PROBES);
    sample.meanFreePath = distanceSum / static_cast<float>(NUM_PROBES);
    return sample;
}

ReverbParams EnclosureDetector::reverbFor(const EnclosureSample& sample) const noexcept {
    const float enclosure = sample.valid ? std::clamp(sample.enclosure, 0.0f, 1.0f) : 0.0f;
    // Squared so that a floor alone (9/26 of the probes) stays almost dry: a real reverb tail needs
    // walls and a ceiling, not just ground reflections.
    const float skyScale = sample.skyOpen ? std::clamp(m_params.skyOpenWetScale, 0.0f, 1.0f) : 1.0f;
    const float wet = enclosure * enclosure * skyScale;
    const float size = std::clamp(sample.meanFreePath / (0.75f * m_params.probeRange), 0.0f, 1.0f);
    const float tail = wet * size;

    ReverbParams r;
    r.sendLevel = lerp(m_params.minSend, m_params.maxSend, wet);
    r.decaySeconds = lerp(m_params.minDecaySeconds, m_params.maxDecaySeconds, tail);
    r.feedback = feedbackForDecay(r.decaySeconds);
    r.damping = std::clamp(lerp(m_params.openDamping, m_params.caveDamping, tail), 0.0f, FdnReverbProcessor::MAX_DAMPING);
    return r;
}

} // namespace betacraft::acoustics
