#pragma once

#include "core/Types.hpp"
#include "worldgen/DecorationFeatures.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace betacraft::voxel {
class Chunk;
}

namespace betacraft::worldgen {

// Minimal view of the resident chunk set (World::getChunkMutable fits it directly).
class ChunkAccess {
public:
    virtual ~ChunkAccess() = default;
    [[nodiscard]] virtual voxel::Chunk* findChunk(core::ChunkCoord coord) noexcept = 0;
};

// Adapts a callable `voxel::Chunk*(core::ChunkCoord)` (e.g. a lambda over
// World::getChunkMutable) so World does not have to inherit from ChunkAccess.
// The callable must not throw (findChunk is noexcept).
template <typename Fn>
class FunctionChunkAccess final : public ChunkAccess {
public:
    explicit FunctionChunkAccess(Fn fn) noexcept(std::is_nothrow_move_constructible_v<Fn>)
        : m_fn(std::move(fn)) {}
    [[nodiscard]] voxel::Chunk* findChunk(core::ChunkCoord coord) noexcept override { return m_fn(coord); }

private:
    Fn m_fn;
};

// Whether onChunkUnloaded keeps a copy of a consumed, undecorated chunk's plan.
enum class PlanRetention : uint8_t {
    Retain,        // the pipeline keeps it until the chunk returns
    CallerRestores // the caller persisted findPlan() or regenerates identical pass-1 terrain
};

// P3.4 two-pass generation, pass 2 orchestration.
//
//   Pass 1 (caller): terrain + caves for one chunk, autonomously.
//   onTerrainReady(c): plans c's features from its pass-1 terrain (Tier 1), then decorates
//   every chunk of c's 3x3 neighbourhood whose own 3x3 neighbourhood is now fully planned.
//   Decorating chunk n = rasterizing, into n only, every planned feature of n's 3x3 that
//   touches n (Tier 2). Since features overhang at most 8 blocks, those 9 plans contain
//   every feature that can reach n: trees are never cut at borders, and because each
//   chunk is written exactly once from the same 9 plans (merged commutatively), the
//   result is identical for any load order.
//
// Streaming order: decoration changes skylight (logs, leaves) and must run BEFORE lighting.
// streaming::ChunkLifecycle currently orders TerrainGen -> Lit -> Decorated with no edge back
// to Lit, so integration must either run pass 2 between TerrainGen and Lit (a chunk may
// only be lit once it is decorated) or add a Decorated -> Lit relight edge; every chunk
// returned by the on*() calls needs relighting + remeshing.
//
// Not thread-safe: drive it from the thread that owns the chunk map. Decorating writes
// only into the chunk being decorated, so a future parallel version only needs per-chunk
// exclusivity.
class DecorationPipeline {
public:
    DecorationPipeline(uint64_t worldSeed, ChunkAccess& chunks) noexcept;

    DecorationPipeline(const DecorationPipeline&) = delete;
    DecorationPipeline& operator=(const DecorationPipeline&) = delete;

    // Pass 1 of `coord` finished (chunk must be resident). Returns the chunks decorated by
    // this call, in deterministic order; each needs relighting + remeshing. Ignored (empty)
    // if the chunk is unknown to ChunkAccess.
    //  * New coordinate: planned from its pass-1 terrain, or from the plan retained when it
    //    was unloaded after a neighbour had consumed it (see onChunkUnloaded).
    //  * Already registered, undecorated: the stored plan is kept (never re-planned from
    //    possibly edited data); decoration of its 3x3 is simply retried.
    //  * Already registered, decorated: if the resident chunk re-plans to exactly the stored
    //    plan it is a fresh pass-1 copy (evicted and regenerated without onChunkUnloaded)
    //    and is decorated again; otherwise it is the decorated instance and the duplicate
    //    notification is ignored (re-planning decorated data would be wrong).
    std::vector<core::ChunkCoord> onTerrainReady(core::ChunkCoord coord);

    // An undecorated chunk restored from disk with the plan saved alongside it (findPlan at
    // save time). Required across sessions once a neighbour may already have consumed that
    // plan: re-planning edited data would cut the trees whose overhang the neighbour holds.
    // Ignored (empty) if the chunk is already registered.
    std::vector<core::ChunkCoord> onUndecoratedChunkLoaded(core::ChunkCoord coord, FeaturePlan plan);

    // A chunk restored from disk that was already decorated. Its plan (saved alongside it,
    // or re-planned from a regenerated pass-1 copy) is needed so that neighbours still
    // waiting for pass 2 receive the parts of its trees that overhang into them.
    std::vector<core::ChunkCoord> onDecoratedChunkLoaded(core::ChunkCoord coord, FeaturePlan plan);
    // Same, but re-plans from `pass1Copy`: a scratch chunk into which WorldGen re-ran pass 1
    // (terrain + caves) for `coord`. Since planning is a pure function of seed + pass-1
    // terrain, this reproduces the original plan without persisting it. Ignored (empty) if
    // pass1Copy belongs to another coordinate or the chunk is already registered.
    std::vector<core::ChunkCoord> onDecoratedChunkLoaded(core::ChunkCoord coord, const voxel::Chunk& pass1Copy);

    // Forget a chunk. Neighbours waiting on it stall until it returns. An undecorated chunk
    // whose plan a decorated neighbour already rasterized keeps that plan in memory, so a
    // reload (possibly of edited data) reuses it; persist findPlan() to survive sessions.
    // CallerRestores drops the copy (also one retained by an earlier unload).
    void onChunkUnloaded(core::ChunkCoord coord, PlanRetention retention = PlanRetention::Retain);

    // Plans kept for unloaded, undecorated, consumed chunks (released when they return).
    [[nodiscard]] size_t retainedPlanCount() const noexcept { return m_retained.size(); }

    [[nodiscard]] bool hasPlan(core::ChunkCoord coord) const noexcept { return m_states.contains(coord); }
    [[nodiscard]] bool isDecorated(core::ChunkCoord coord) const noexcept;
    [[nodiscard]] const FeaturePlan* findPlan(core::ChunkCoord coord) const noexcept;
    [[nodiscard]] size_t plannedChunkCount() const noexcept { return m_states.size(); }
    [[nodiscard]] size_t awaitingDecorationCount() const noexcept;
    [[nodiscard]] const FeaturePlanner& planner() const noexcept { return m_planner; }

private:
    struct ChunkState {
        FeaturePlan plan;
        bool decorated{false};
        bool consumed{false}; // some chunk was decorated from this plan
    };

    std::vector<core::ChunkCoord> registerChunk(core::ChunkCoord coord, FeaturePlan plan, bool decorated);

    std::vector<core::ChunkCoord> decorateReadyAround(core::ChunkCoord coord);
    bool tryDecorate(core::ChunkCoord coord);

    FeaturePlanner m_planner;
    ChunkAccess& m_chunks;
    std::unordered_map<core::ChunkCoord, ChunkState> m_states;
    std::unordered_map<core::ChunkCoord, FeaturePlan> m_retained;
};

} // namespace betacraft::worldgen
