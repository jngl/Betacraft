#pragma once

#include "core/Types.hpp"
#include "voxel/Chunk.hpp"
#include "world/WorldGen.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <vector>

namespace betacraft::world {

// Half the logical cores (meshing and the main thread keep the rest), never fewer than one.
[[nodiscard]] inline size_t defaultGenWorkerCount() noexcept {
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 3u ? static_cast<size_t>(hw / 2u) : size_t{1};
}

struct ChunkGenConfig {
    // 0 = inline mode: no thread, jobs run only inside runPendingInline (deterministic tests).
    size_t workerCount{defaultGenWorkerCount()};
};

// A pass-1 chunk ready for World integration (terrain, caves, ores, dungeons, own lighting).
struct GeneratedChunk {
    core::ChunkCoord coord{};
    std::unique_ptr<world::Chunk> chunk;
};

struct ChunkGenStats {
    uint64_t requested{0};
    uint64_t cancelled{0};
    uint64_t completed{0}; // Published to the ready list
    uint64_t discarded{0}; // Finished after a cancel or a seed change: thrown away
};

// S7.7 asynchronous pass-1 generation. Workers own nothing of the World: each job reads a
// shared_ptr<const WorldGen> (replaced by setWorldGen on a seed change) and produces a fresh
// Chunk. Jobs are served nearest to the centre first (then FIFO) and can be cancelled: a
// pending job is dropped, a running one has its stop_token signalled and its result is
// discarded. Every call is internally synchronised; results are meant for the main thread.
class ChunkGenPipeline {
public:
    explicit ChunkGenPipeline(std::shared_ptr<const world::WorldGen> gen, ChunkGenConfig config = {});
    ~ChunkGenPipeline(); // Cancels everything and joins the workers

    ChunkGenPipeline(const ChunkGenPipeline&) = delete;
    ChunkGenPipeline& operator=(const ChunkGenPipeline&) = delete;

    // The job itself: a new Chunk, WorldGen::generateChunk (pass 1) then recalculateLighting.
    // Identical to what World::ensureChunkLoaded computes for a new chunk.
    [[nodiscard]] static std::unique_ptr<world::Chunk> generate(const world::WorldGen& gen, core::ChunkCoord coord);

    // False when the chunk is already pending, running or ready.
    bool request(core::ChunkCoord coord);
    // Re-orders the pending jobs by distance to `center` (chunk coordinates).
    void setCenter(core::ChunkCoord center);
    // Whatever stage the chunk is at, it will never come out of takeReady. False when unknown.
    bool cancel(core::ChunkCoord coord);
    // Seed change: every pending, running and ready job is discarded; later jobs use `gen`.
    void setWorldGen(std::shared_ptr<const world::WorldGen> gen);
    [[nodiscard]] std::shared_ptr<const world::WorldGen> worldGen() const;

    // Up to `maxChunks` finished chunks, nearest to the centre first then in request order
    // (never completion order), so the World integrates them in the same order whatever the
    // worker timing was.
    [[nodiscard]] std::vector<GeneratedChunk> takeReady(size_t maxChunks = SIZE_MAX);
    [[nodiscard]] std::optional<GeneratedChunk> popReady();

    [[nodiscard]] bool isScheduled(core::ChunkCoord coord) const; // Pending, running or ready
    // Inline mode: runs up to `maxJobs` pending jobs on the caller; returns how many ran.
    size_t runPendingInline(size_t maxJobs);
    // Blocks until no job is pending or running. Never sleep to synchronise; use this.
    bool waitIdle(std::chrono::nanoseconds timeout = std::chrono::nanoseconds::max());

    [[nodiscard]] size_t workerCount() const noexcept { return m_workers.size(); }
    [[nodiscard]] size_t pendingJobs() const;
    [[nodiscard]] size_t runningJobs() const;
    [[nodiscard]] size_t readyResults() const;
    [[nodiscard]] ChunkGenStats stats() const;

private:
    struct JobKey {
        int64_t distanceSq{0};
        uint64_t sequence{0}; // FIFO tie-break and uniqueness
        core::ChunkCoord coord{};

        bool operator<(const JobKey& o) const noexcept {
            if (distanceSq != o.distanceSq) return distanceSq < o.distanceSq;
            return sequence < o.sequence;
        }
    };
    struct Running {
        uint64_t ticket{0};
        std::stop_source stop;
    };
    struct Job {
        core::ChunkCoord coord{};
        uint64_t ticket{0};
        std::stop_token stop;
        std::shared_ptr<const world::WorldGen> gen;
        uint64_t sequence{0}; // Request order: the ready list is sorted by it
    };

    void workerLoop(std::stop_token stop);
    [[nodiscard]] int64_t distanceSqLocked(core::ChunkCoord coord) const noexcept;
    [[nodiscard]] Job popJobLocked();
    void runJob(std::unique_lock<std::mutex>& lock, Job job);
    // Nearest ready result (distance to m_center, then request order); m_ready not empty.
    [[nodiscard]] std::map<uint64_t, GeneratedChunk>::iterator nearestReadyLocked();
    [[nodiscard]] bool idleLocked() const noexcept { return m_order.empty() && m_active == 0; }

    mutable std::mutex m_mutex;
    std::condition_variable_any m_workCv;
    std::condition_variable m_idleCv;
    std::shared_ptr<const world::WorldGen> m_gen;
    std::set<JobKey> m_order;
    std::unordered_map<core::ChunkCoord, JobKey> m_pending;
    std::unordered_map<core::ChunkCoord, Running> m_running; // Current job of each chunk
    size_t m_active{0}; // Jobs running, cancelled ones included
    std::map<uint64_t, GeneratedChunk> m_ready; // Keyed by request sequence
    core::ChunkCoord m_center{};
    uint64_t m_sequence{0};
    uint64_t m_nextTicket{1};
    bool m_shutdown{false};
    ChunkGenStats m_stats{};

    std::vector<std::jthread> m_workers; // Last member: threads die before the state they use
};

} // namespace betacraft::world
