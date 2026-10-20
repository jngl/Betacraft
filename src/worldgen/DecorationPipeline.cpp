#include "worldgen/DecorationPipeline.hpp"

#include "voxel/Chunk.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace betacraft::worldgen {

DecorationPipeline::DecorationPipeline(uint64_t worldSeed, ChunkAccess& chunks) noexcept
    : m_planner(worldSeed), m_chunks(chunks) {}

std::vector<core::ChunkCoord> DecorationPipeline::onTerrainReady(core::ChunkCoord coord) {
    world::Chunk* chunk = m_chunks.findChunk(coord);
    if (chunk == nullptr) return {};

    if (const auto known = m_states.find(coord); known != m_states.end()) {
        if (known->second.decorated) {
            // Planning is a pure function of seed + pass-1 terrain: only a fresh pass-1 copy
            // reproduces the stored plan exactly; the decorated instance does not.
            if (m_planner.plan(*chunk) != known->second.plan) return {};
            known->second.decorated = false;
        }
        return decorateReadyAround(coord);
    }

    if (const auto kept = m_retained.find(coord); kept != m_retained.end()) {
        FeaturePlan plan = std::move(kept->second);
        m_retained.erase(kept);
        return registerChunk(coord, std::move(plan), false);
    }
    return registerChunk(coord, m_planner.plan(*chunk), false);
}

std::vector<core::ChunkCoord> DecorationPipeline::onUndecoratedChunkLoaded(core::ChunkCoord coord, FeaturePlan plan) {
    if (m_states.contains(coord)) return {};
    return registerChunk(coord, std::move(plan), false);
}

std::vector<core::ChunkCoord> DecorationPipeline::onDecoratedChunkLoaded(core::ChunkCoord coord, FeaturePlan plan) {
    if (m_states.contains(coord)) return {};
    return registerChunk(coord, std::move(plan), true);
}

std::vector<core::ChunkCoord> DecorationPipeline::onDecoratedChunkLoaded(core::ChunkCoord coord,
                                                                       const world::Chunk& pass1Copy) {
    if (pass1Copy.getCoord() != coord || m_states.contains(coord)) return {};
    return onDecoratedChunkLoaded(coord, m_planner.plan(pass1Copy));
}

std::vector<core::ChunkCoord> DecorationPipeline::registerChunk(core::ChunkCoord coord, FeaturePlan plan,
                                                                bool decorated) {
    std::sort(plan.begin(), plan.end()); // keep the canonical order whatever the source
    m_retained.erase(coord);             // an explicit plan supersedes a retained one
    m_states.emplace(coord, ChunkState{std::move(plan), decorated, false});
    return decorateReadyAround(coord);
}

void DecorationPipeline::onChunkUnloaded(core::ChunkCoord coord, PlanRetention retention) {
    if (retention == PlanRetention::CallerRestores) m_retained.erase(coord);
    const auto it = m_states.find(coord);
    if (it == m_states.end()) return;
    if (retention == PlanRetention::Retain && !it->second.decorated && it->second.consumed) {
        m_retained[coord] = std::move(it->second.plan);
    }
    m_states.erase(it);
}

bool DecorationPipeline::isDecorated(core::ChunkCoord coord) const noexcept {
    const auto it = m_states.find(coord);
    return it != m_states.end() && it->second.decorated;
}

const FeaturePlan* DecorationPipeline::findPlan(core::ChunkCoord coord) const noexcept {
    const auto it = m_states.find(coord);
    return it == m_states.end() ? nullptr : &it->second.plan;
}

size_t DecorationPipeline::awaitingDecorationCount() const noexcept {
    return static_cast<size_t>(std::count_if(m_states.begin(), m_states.end(),
                                             [](const auto& kv) { return !kv.second.decorated; }));
}

std::vector<core::ChunkCoord> DecorationPipeline::decorateReadyAround(core::ChunkCoord coord) {
    // Only the 3x3 around the new arrival can have become complete.
    std::vector<core::ChunkCoord> done;
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const core::ChunkCoord n{coord.x + dx, coord.z + dz};
            if (tryDecorate(n)) done.push_back(n);
        }
    }
    return done;
}

bool DecorationPipeline::tryDecorate(core::ChunkCoord coord) {
    const auto self = m_states.find(coord);
    if (self == m_states.end() || self->second.decorated) return false;

    // Gate: all 9 plans of the neighbourhood must exist, and all 9 chunks must be resident
    // (conditional features read their neighbours' pass-1 water; never guess).
    std::array<ChunkState*, 9> states{};
    std::array<world::Chunk*, 9> chunks{};
    size_t i = 0;
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx, ++i) {
            const core::ChunkCoord n{coord.x + dx, coord.z + dz};
            const auto it = m_states.find(n);
            if (it == m_states.end()) return false;
            states[i] = &it->second;
            chunks[i] = m_chunks.findChunk(n);
            if (chunks[i] == nullptr) return false;
        }
    }
    world::Chunk& chunk = *chunks[4];

    // Sea-level water is pass-1 data that decoration never alters, so it reads the same
    // whether a neighbour is decorated yet or not: conditions resolve order-independently.
    auto isWaterAt = [&](int32_t wx, int32_t y, int32_t wz) noexcept {
        const int32_t dx = core::floorDiv(wx, world::Chunk::WIDTH) - coord.x;
        const int32_t dz = core::floorDiv(wz, world::Chunk::DEPTH) - coord.z;
        if (dx < -1 || dx > 1 || dz < -1 || dz > 1) return false; // unreachable: overhang <= 2
        const world::Chunk& c = *chunks[static_cast<size_t>((dz + 1) * 3 + (dx + 1))];
        return c.getBlock(core::floorMod(wx, world::Chunk::WIDTH), y, core::floorMod(wz, world::Chunk::DEPTH)) ==
               world::BlockId::Water;
    };

    for (const ChunkState* st : states) {
        for (const Feature& f : st->plan) {
            if (featureIntersectsChunk(f, coord) && featureConditionHolds(f, isWaterAt)) {
                (void)rasterizeFeature(f, chunk);
            }
        }
    }
    self->second.decorated = true;
    for (ChunkState* st : states) st->consumed = true; // see onChunkUnloaded
    return true;
}

} // namespace betacraft::worldgen
