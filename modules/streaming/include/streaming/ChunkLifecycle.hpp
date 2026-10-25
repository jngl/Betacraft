#pragma once

#include "core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace betacraft::streaming {

// Strict per-chunk streaming state machine:
//   Unloaded -> TerrainGen -> Lit -> Decorated -> Meshed -> Uploaded
// Extra legal edges: any loaded stage -> Unloaded (eviction), and Meshed/Uploaded ->
// Decorated (a block edit invalidates the mesh but keeps terrain and decoration).
// Light does not need its own back edge: World::setBlock relights synchronously
// (DifferentialLightingEngine::setBlockAndUpdateLighting) before it returns, so by the
// time an edit reaches the pipeline the chunk is lit again and only the mesh is stale.
// Anything else (skipping a stage, going back to generation) is a scheduling bug.
// AsyncChunkPipeline owns one tracker and enforces it (PipelineConfig::enforceChunkLifecycle).
enum class ChunkStage : uint8_t { Unloaded = 0, TerrainGen, Lit, Decorated, Meshed, Uploaded };

[[nodiscard]] constexpr bool canTransition(ChunkStage from, ChunkStage to) noexcept {
    if (from == to) {
        return false;
    }
    if (to == ChunkStage::Unloaded) {
        return true;
    }
    if (static_cast<uint8_t>(to) == static_cast<uint8_t>(from) + 1u) {
        return true;
    }
    return to == ChunkStage::Decorated && (from == ChunkStage::Meshed || from == ChunkStage::Uploaded);
}

[[nodiscard]] constexpr std::string_view stageName(ChunkStage s) noexcept {
    switch (s) {
        case ChunkStage::Unloaded:   return "Unloaded";
        case ChunkStage::TerrainGen: return "TerrainGen";
        case ChunkStage::Lit:        return "Lit";
        case ChunkStage::Decorated:  return "Decorated";
        case ChunkStage::Meshed:     return "Meshed";
        case ChunkStage::Uploaded:   return "Uploaded";
    }
    return "Invalid";
}

// Main-thread bookkeeping of every streamed chunk's stage. Unloaded chunks are not
// stored, so memory follows the loaded set rather than the explored area.
class ChunkLifecycleTracker {
public:
    [[nodiscard]] ChunkStage stage(core::ChunkCoord c) const noexcept {
        const auto it = m_stages.find(c);
        return it == m_stages.end() ? ChunkStage::Unloaded : it->second;
    }

    // Applies the transition if legal; otherwise leaves the state untouched and counts it.
    bool advance(core::ChunkCoord c, ChunkStage to) {
        if (!canTransition(stage(c), to)) {
            ++m_rejected;
            return false;
        }
        if (to == ChunkStage::Unloaded) {
            m_stages.erase(c);
        } else {
            m_stages[c] = to;
        }
        return true;
    }

    [[nodiscard]] size_t countIn(ChunkStage s) const noexcept {
        size_t n = 0;
        for (const auto& [coord, stage] : m_stages) {
            n += (stage == s) ? 1u : 0u;
        }
        return n;
    }

    [[nodiscard]] size_t trackedCount() const noexcept { return m_stages.size(); }
    [[nodiscard]] uint64_t rejectedTransitions() const noexcept { return m_rejected; }

private:
    std::unordered_map<core::ChunkCoord, ChunkStage> m_stages;
    uint64_t m_rejected{0};
};

} // namespace betacraft::streaming
