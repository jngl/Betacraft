#include "acoustics/OcclusionScheduler.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::acoustics {

namespace {
// Absorbs floating-point noise in frame timestamps (e.g. 6 * (1/60) != 0.1 exactly).
constexpr double TIME_EPSILON = 1.0e-9;
} // namespace

OcclusionScheduler::OcclusionScheduler(double intervalSeconds, const OcclusionParams& params) noexcept
    : m_params(params),
      m_interval((std::isfinite(intervalSeconds) && intervalSeconds > 0.0) ? intervalSeconds
                                                                           : OCCLUSION_UPDATE_INTERVAL_S) {}

OcclusionScheduler::Slot* OcclusionScheduler::find(uint32_t handle) noexcept {
    for (Slot& slot : m_slots) {
        if (slot.active && slot.handle == handle) {
            return &slot;
        }
    }
    return nullptr;
}

const OcclusionScheduler::Slot* OcclusionScheduler::find(uint32_t handle) const noexcept {
    for (const Slot& slot : m_slots) {
        if (slot.active && slot.handle == handle) {
            return &slot;
        }
    }
    return nullptr;
}

bool OcclusionScheduler::addSource(uint32_t handle, const glm::vec3& position) noexcept {
    if (find(handle) != nullptr) {
        return false;
    }
    for (Slot& slot : m_slots) {
        if (!slot.active) {
            slot = Slot{handle, position, 0.0, OcclusionTarget{}, OcclusionTarget{}, false, true};
            ++m_count;
            return true;
        }
    }
    return false;
}

bool OcclusionScheduler::removeSource(uint32_t handle) noexcept {
    Slot* slot = find(handle);
    if (slot == nullptr) {
        return false;
    }
    slot->active = false;
    --m_count;
    return true;
}

bool OcclusionScheduler::setSourcePosition(uint32_t handle, const glm::vec3& position) noexcept {
    Slot* slot = find(handle);
    if (slot == nullptr) {
        return false;
    }
    slot->position = position;
    return true;
}

void OcclusionScheduler::clear() noexcept {
    m_slots.fill(Slot{});
    m_count = 0;
}

std::optional<OcclusionTarget> OcclusionScheduler::target(uint32_t handle) const noexcept {
    const Slot* slot = find(handle);
    if (slot == nullptr || !slot->evaluated) {
        return std::nullopt;
    }
    return slot->target;
}

OcclusionTarget OcclusionScheduler::interpolate(const Slot& slot, double nowSeconds) const noexcept {
    const double elapsed = nowSeconds - slot.lastEvaluated;
    if (!(elapsed > 0.0)) { // Also catches NaN: hold the ramp's start value
        return slot.rampStart;
    }
    if (elapsed >= m_interval) {
        return slot.target;
    }
    const float t = static_cast<float>(elapsed / m_interval);
    const OcclusionTarget& a = slot.rampStart;
    const OcclusionTarget& b = slot.target;
    // Geometric cutoff ramp: a^(1-t) * b^t, i.e. linear in octaves. Both ends are >= minCutoffHz > 0.
    return OcclusionTarget{a.gain + (b.gain - a.gain) * t, a.cutoffHz * std::pow(b.cutoffHz / a.cutoffHz, t)};
}

std::optional<OcclusionTarget> OcclusionScheduler::interpolatedTarget(uint32_t handle, double nowSeconds) const noexcept {
    const Slot* slot = find(handle);
    if (slot == nullptr || !slot->evaluated) {
        return std::nullopt;
    }
    return interpolate(*slot, nowSeconds);
}

size_t OcclusionScheduler::reevaluationBudget(double nowSeconds) const noexcept {
    if (!m_hasLastUpdate) {
        return CAPACITY;
    }
    const double dt = nowSeconds - m_lastUpdate;
    if (!(dt > 0.0)) {
        return 1; // Repeated or backwards timestamp: keep trickling, never burst
    }
    const double demand = static_cast<double>(m_count) * dt / m_interval;
    if (demand >= static_cast<double>(CAPACITY)) {
        return CAPACITY;
    }
    // +1 slack absorbs frame-time jitter so steady-state sources do not slip behind 10 Hz.
    return std::min(CAPACITY, static_cast<size_t>(std::ceil(demand)) + 1);
}

std::span<const OcclusionUpdate> OcclusionScheduler::update(double nowSeconds, const glm::vec3& listener,
                                                            const VoxelSolidityQuery& query) noexcept {
    if (!std::isfinite(nowSeconds)) {
        return {};
    }
    size_t budget = reevaluationBudget(nowSeconds);
    m_lastUpdate = nowSeconds;
    m_hasLastUpdate = true;

    size_t produced = 0;
    std::optional<size_t> firstDeferred;
    for (size_t n = 0; n < CAPACITY; ++n) {
        const size_t index = (m_cursor + n) % CAPACITY;
        Slot& slot = m_slots[index];
        if (!slot.active) {
            continue;
        }
        if (slot.evaluated) {
            if (nowSeconds < slot.lastEvaluated) {
                slot.lastEvaluated = nowSeconds; // Clock went backwards: re-anchor, no burst
                slot.rampStart = slot.target;    // ... and finish the ramp: its timeline is gone
                continue;
            }
            if (nowSeconds - slot.lastEvaluated < m_interval - TIME_EPSILON) {
                continue;
            }
            if (budget == 0) {
                if (!firstDeferred) {
                    firstDeferred = index;
                }
                continue; // Due but over budget: first in line next update
            }
            --budget;
        }
        // (New sources bypass the budget: a fresh sound must never start unoccluded.)
        const OcclusionSample sample = measureOcclusion(query, listener, slot.position);
        const OcclusionTarget fresh = occlusionTargetFor(sample, m_params);
        // Continue from wherever the previous ramp currently is, so a new sample never causes a jump;
        // the very first sample has no history and snaps.
        slot.rampStart = slot.evaluated ? interpolate(slot, nowSeconds) : fresh;
        slot.target = fresh;
        slot.lastEvaluated = nowSeconds;
        slot.evaluated = true;
        m_updates[produced++] =
            OcclusionUpdate{slot.handle,        slot.target.gain,  slot.target.cutoffHz,
                            sample.solidVoxels, sample.truncated, sample.beyondAudibleRange};
    }
    if (firstDeferred) {
        m_cursor = *firstDeferred;
    }
    return {m_updates.data(), produced};
}

} // namespace betacraft::acoustics
