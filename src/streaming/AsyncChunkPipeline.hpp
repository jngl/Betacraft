#pragma once

#include "core/Types.hpp"
#include "streaming/ChunkLifecycle.hpp"
#include "streaming/PingPongStaging.hpp"
#include "streaming/SectionNeighborhoodSnapshot18.hpp"
#include "streaming/SnapshotMesher.hpp"
#include "streaming/UploadBudget.hpp"
#include "voxel/SectionCoord.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

namespace betacraft::world {
class World;
}

namespace betacraft::streaming {

// Coarse scheduling class; within a class jobs are ordered by distance to the viewer,
// then FIFO. Lower value = served first.
enum class TaskPriority : uint8_t { ImmediateCamera = 0, HighNear, MediumFar, LowBackground };

enum class SubmitResult : uint8_t {
    Accepted,             // queued as a new job
    Replaced,             // superseded the pending job of the same section (dedup)
    AcceptedEvictedOther, // queue was full: the worst pending job was dropped for this one
    RejectedQueueFull,    // queue full and this job is not better than the worst one
    RejectedStale,        // version older than the newest version known for the section
    RejectedInvalid,      // null snapshot or section Y outside the column
    RejectedNotReady,     // lifecycle enforced and the chunk is not Decorated yet
    RejectedShutdown      // pipeline is shut down
};

struct ChunkMeshResult {
    voxel::SectionCoord coord{};
    uint64_t version{0};
    RawMeshBuffer buffers;
};

// Worker-side mesher. Must only read the snapshot; should poll the stop_token and
// return std::nullopt when asked to stop. Injectable so tests can gate execution.
using MeshFunction = std::function<std::optional<RawMeshBuffer>(
    const SectionNeighborhoodSnapshot18&, voxel::SectionCoord, const MeshOptions&, std::stop_token)>;

using MeshUploader = std::function<void(ChunkMeshResult&)>;

// Authoritative per-section version (e.g. the World's edit counter), consulted by
// drainUploads on the drain thread right before each upload; std::nullopt means the
// section no longer exists. Because it runs on the drain thread, which in this engine is
// the main thread owning the World, it may read World state directly.
using SectionVersionSource = std::function<std::optional<uint64_t>(voxel::SectionCoord)>;

// N-1 logical cores (the main thread keeps one), never fewer than one worker.
[[nodiscard]] inline size_t defaultPipelineWorkerCount() noexcept {
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 1u ? static_cast<size_t>(hw - 1u) : size_t{1};
}

struct PipelineConfig {
    // 0 = inline mode: no threads, jobs only run inside runPendingInline() (deterministic
    // tests, single-core fallback). Default is N-1 logical cores.
    size_t workerCount{defaultPipelineWorkerCount()};
    size_t maxQueuedJobs{4096};
    MeshOptions meshOptions{};
    MeshFunction mesher{};       // empty -> SnapshotMesher::build
    MonotonicClock clock{};      // empty -> steadyClockNow
    // Strict Unloaded -> TerrainGen -> Lit -> Decorated -> Meshed -> Uploaded machine per
    // chunk: meshing requests for chunks not yet Decorated are RejectedNotReady.
    bool enforceChunkLifecycle{true};
    // Optional: when set, a staged mesh is uploaded only if its version equals this
    // source's (Fiche 1 §5 "chunkVersion == world.getSectionVersion"). Without it the
    // caller MUST report edits through noteSectionVersion (or re-request) and unloads
    // through cancelSection/cancelChunk.
    SectionVersionSource authoritativeVersion{};
    // View weighting (set via setViewer): sections whose bounding sphere lies outside the
    // cone of half-angle acos(viewConeCos) around the look direction rank as if they were
    // outOfViewDistanceScale times farther. 0.5 = 60 degrees, wider than a typical
    // half-FOV so peripheral sections are not starved. Sections closer than
    // nearRadiusBlocks are never penalised (turning around must not reveal holes).
    // A scale of 1 disables the weighting.
    float viewConeCos{0.5f};
    float outOfViewDistanceScale{2.0f};
    float nearRadiusBlocks{32.0f};
};

struct PipelineStats {
    uint64_t submitted{0};
    uint64_t replaced{0};
    uint64_t evicted{0};
    uint64_t rejectedFull{0};
    uint64_t rejectedStale{0};
    uint64_t rejectedNotReady{0};
    uint64_t cancelled{0};
    uint64_t skippedStale{0};    // job was already stale when a worker picked it up
    uint64_t discardedStale{0};  // mesh finished but became stale before publication
    uint64_t completed{0};       // meshes published to staging
    uint64_t aborted{0};         // mesher returned nullopt while still current (shutdown)
    uint64_t abortedStale{0};    // running job stopped because it became stale
    uint64_t failed{0};          // mesher threw
    uint64_t discardedAtDrain{0};
    uint64_t uploaded{0};
    uint64_t uploadFailed{0};    // consumed, but the uploader threw (item is lost)
};

// Asynchronous section-meshing orchestrator (roadmap P1.1).
//
// Threading contract:
//  * submission / cancel / version / viewer calls: main (tick) thread; they are
//    internally synchronised, so any thread is safe;
//  * drainUploads(): ONE consumer thread (the render thread owning the GL context);
//  * workers: std::jthread pool, each reads only an immutable snapshot.
//
// Staleness: the pipeline keeps, per section, the newest version known (raised by
// requestSectionMesh and noteSectionVersion, forgotten by cancelSection) and the ticket of
// the newest accepted submission. Tickets are never reused, so only that one submission
// can ever be uploaded, even when the caller repeats a version number (chunk reloaded with
// a restarted counter, or a neighbour edit that changed only the 1-voxel border). Anything
// else is dropped at three points: before meshing (saves CPU), before publication, and at
// drain (authoritative, since edits can land at any time). A job that becomes stale while
// running has its stop_token signalled so the worker gives up at the next layer.
//
// Integration contract (what the caller must report, since the pipeline never reads the
// live World on its own):
//  * chunk loaded/generated -> advanceChunkStage(TerrainGen, Lit, Decorated) when the
//    lifecycle is enforced (default), before requesting its sections;
//  * block edit             -> noteSectionVersion and/or a new request for every section
//    whose 18^3 neighbourhood changed, OR configure authoritativeVersion so the drain
//    compares against the World itself;
//  * chunk unloaded         -> cancelChunk;
//  * evictions              -> poll takeEvictedSections() and re-request them;
//  * every frame            -> setViewer(camera pos, look dir), drainUploads(budget, ...).
class AsyncChunkPipeline {
public:
    explicit AsyncChunkPipeline(PipelineConfig config = {});
    ~AsyncChunkPipeline();

    AsyncChunkPipeline(const AsyncChunkPipeline&) = delete;
    AsyncChunkPipeline& operator=(const AsyncChunkPipeline&) = delete;
    AsyncChunkPipeline(AsyncChunkPipeline&&) = delete;
    AsyncChunkPipeline& operator=(AsyncChunkPipeline&&) = delete;

    [[nodiscard]] static size_t defaultWorkerCount() noexcept { return defaultPipelineWorkerCount(); }

    // ---- main thread: scheduling -----------------------------------------------------
    SubmitResult requestSectionMesh(voxel::SectionCoord coord, std::shared_ptr<const SectionNeighborhoodSnapshot18> snapshot,
                                    uint64_t version, TaskPriority priority = TaskPriority::HighNear);

    // Captures the 18^3 snapshot synchronously on the calling thread, then submits.
    SubmitResult requestSectionMeshFromWorld(const world::World& world, voxel::SectionCoord coord, uint64_t version,
                                             TaskPriority priority = TaskPriority::HighNear);

    // Re-keys every pending job by its distance to `pos` (O(n log n), call on movement).
    // Non-finite positions are ignored: the last valid position keeps ordering the queue.
    void setViewerPosition(const glm::vec3& pos);
    // Position + look direction (any length; zero or non-finite = no view weighting).
    // setViewerPosition keeps the last direction, so movement alone needs no camera.
    void setViewer(const glm::vec3& pos, const glm::vec3& forward);

    // A block edit bumped the section version: anything older is now stale. Monotonic.
    void noteSectionVersion(voxel::SectionCoord coord, uint64_t version);

    // Section/chunk unloaded: drops the pending job and forgets the section, so in-flight
    // and staged results for it are discarded (running jobs are told to stop). A later
    // request starts from scratch. cancelChunk also moves the chunk to Unloaded.
    void cancelSection(voxel::SectionCoord coord);
    void cancelChunk(core::ChunkCoord chunk);

    // True when `version` is the newest version known for the section.
    [[nodiscard]] bool isCurrent(voxel::SectionCoord coord, uint64_t version) const;

    // Sections whose pending job was evicted by the bounded queue since the last call.
    // Their mesh is now out of date and nothing will refresh it: the streamer should
    // request them again once they are close enough to win a queue slot.
    [[nodiscard]] std::vector<voxel::SectionCoord> takeEvictedSections();
    // True when a CURRENT submission is pending or in flight, i.e. a fresh mesh is coming
    // without another request. A superseded job still running does not count.
    [[nodiscard]] bool isScheduled(voxel::SectionCoord coord) const;

    // ---- chunk lifecycle (PipelineConfig::enforceChunkLifecycle) ----------------------------
    // Reports a stage produced outside the pipeline (terrain generation, lighting,
    // decoration: today World::ensureChunkLoaded does all three synchronously, so the
    // integrator reports TerrainGen, Lit, Decorated right after loading a chunk). Only the
    // strict next stage is accepted. Meshed/Uploaded are derived by the pipeline and are
    // refused here; Unloaded is equivalent to cancelChunk. False when refused or when the
    // lifecycle is not enforced.
    [[nodiscard]] bool advanceChunkStage(core::ChunkCoord chunk, ChunkStage to);
    [[nodiscard]] ChunkStage chunkStage(core::ChunkCoord chunk) const;
    [[nodiscard]] size_t trackedChunks() const;

    // ---- render thread: budgeted GPU upload --------------------------------------------
    DrainStats drainUploads(std::chrono::nanoseconds budget, const MeshUploader& uploader);

    // ---- control -------------------------------------------------------------------------
    // Runs up to maxJobs pending jobs on the calling thread; returns how many were taken.
    size_t runPendingInline(size_t maxJobs);
    void pause();
    void resume();
    // Blocks until no job is pending or in flight (results are then staged). Never use a
    // sleep to synchronise with the pipeline; use this.
    [[nodiscard]] bool waitIdle(std::chrono::nanoseconds timeout);
    // Cooperative stop: signals every worker's stop_token, wakes them, joins them and drops
    // pending jobs. Idempotent; also run by the destructor.
    void shutdown();

    // ---- introspection -------------------------------------------------------------------
    [[nodiscard]] size_t workerCount() const;
    [[nodiscard]] size_t liveWorkers() const noexcept { return m_liveWorkers.load(std::memory_order_acquire); }
    [[nodiscard]] size_t pendingJobs() const;
    [[nodiscard]] size_t inFlightJobs() const;
    [[nodiscard]] size_t stagedResults() const;
    [[nodiscard]] size_t maxQueuedJobs() const noexcept { return m_maxQueuedJobs; }
    [[nodiscard]] PipelineStats stats() const;
    [[nodiscard]] size_t trackedSections() const;        // sections with version bookkeeping
    [[nodiscard]] size_t pendingEvictionReports() const; // not yet taken by takeEvictedSections

private:
    struct JobKey {
        TaskPriority priority{TaskPriority::HighNear};
        float distanceSq{0.0f}; // never NaN (see keyDistanceLocked): strict weak ordering
        uint64_t sequence{0}; // FIFO tie-break and uniqueness
        voxel::SectionCoord coord{};

        bool operator<(const JobKey& o) const noexcept {
            if (priority != o.priority) return priority < o.priority;
            if (distanceSq != o.distanceSq) return distanceSq < o.distanceSq;
            return sequence < o.sequence;
        }
    };

    struct PendingJob {
        JobKey key;
        std::shared_ptr<const SectionNeighborhoodSnapshot18> snapshot;
        uint64_t version{0};
        uint64_t ticket{0};
    };

    struct RunningJob {
        voxel::SectionCoord coord{};
        std::shared_ptr<const SectionNeighborhoodSnapshot18> snapshot;
        uint64_t version{0};
        uint64_t ticket{0};
        std::stop_source stop; // per-job: fired when the job becomes stale or on shutdown
    };

    struct InFlightEntry {
        uint64_t ticket{0};
        std::stop_source stop; // shares state with RunningJob::stop
    };

    using StopList = std::vector<std::stop_source>;

    // How far the section's CURRENT submission got; drives the column's Meshed/Uploaded
    // stages. Any new ticket (or ticket 0) restarts at Waiting.
    enum class MeshProgress : uint8_t { Waiting, Staged, Uploaded };

    // What the pipeline knows about a section. ticket 0 = no submission may be uploaded
    // (e.g. the section was edited after its last request, or its job was evicted).
    struct SectionState {
        uint64_t version{0};
        uint64_t ticket{0};
        MeshProgress progress{MeshProgress::Waiting};
    };

    struct StagedResult {
        uint64_t ticket{0};
        ChunkMeshResult result;
    };

    void workerLoop(std::stop_token stopToken);
    [[nodiscard]] SubmitResult submitLocked(voxel::SectionCoord coord, std::shared_ptr<const SectionNeighborhoodSnapshot18> snapshot,
                                            uint64_t version, TaskPriority priority);
    void cancelSectionLocked(voxel::SectionCoord coord, StopList& stale);
    // Re-derives the column's Decorated/Meshed/Uploaded stage from its sections' progress.
    void refreshChunkStageLocked(core::ChunkCoord chunk);
    // Running jobs of `coord` that can no longer be uploaded: their stop must be requested.
    void collectStaleRunningLocked(voxel::SectionCoord coord, StopList& out);
    static void requestStops(StopList& stops) noexcept;
    [[nodiscard]] static bool isAccepted(SubmitResult r) noexcept;
    [[nodiscard]] std::optional<RunningJob> popBestLocked();
    void executeJob(RunningJob job, std::stop_token stopToken);
    void finishJobLocked(voxel::SectionCoord coord, uint64_t ticket);
    void evictLocked(const JobKey& worst);
    void rekeyPendingLocked();
    // Squared distance, view-weighted. Never NaN, so JobKey::operator< stays a strict
    // weak ordering.
    [[nodiscard]] float keyDistanceLocked(voxel::SectionCoord coord) const noexcept;
    // Pending, or a job carrying the section's current ticket is running.
    [[nodiscard]] bool isScheduledLocked(voxel::SectionCoord coord) const;
    void eraseJobLocked(voxel::SectionCoord coord);
    [[nodiscard]] bool isLatestSubmissionLocked(voxel::SectionCoord coord, uint64_t ticket) const;

    const size_t m_maxQueuedJobs;
    const bool m_enforceLifecycle;
    const float m_viewConeCos;
    const float m_outOfViewScaleSq;
    const float m_nearRadiusSq;
    const MeshOptions m_meshOptions;
    const MeshFunction m_mesher;
    const SectionVersionSource m_authoritativeVersion;
    const MonotonicClock m_clock;

    mutable std::mutex m_mutex; // guards everything below up to m_workers
    std::condition_variable_any m_workCv;
    std::condition_variable m_idleCv;
    std::set<JobKey> m_order;
    std::unordered_map<voxel::SectionCoord, PendingJob> m_pending;
    std::unordered_map<voxel::SectionCoord, std::vector<InFlightEntry>> m_inFlight; // coord -> running jobs
    size_t m_inFlightTotal{0};
    std::unordered_map<voxel::SectionCoord, SectionState> m_sections;
    std::vector<voxel::SectionCoord> m_evicted; // drained by takeEvictedSections(), eviction order
    std::unordered_set<voxel::SectionCoord> m_evictedSet; // dedup: bounded by distinct sections
    glm::vec3 m_viewer{0.0f};
    glm::vec3 m_forward{0.0f}; // unit length, or zero when there is no view direction
    uint64_t m_sequence{0};
    uint64_t m_nextTicket{1};
    bool m_paused{false};
    bool m_shutdown{false};
    PipelineStats m_stats{};
    ChunkLifecycleTracker m_lifecycle; // only maintained when m_enforceLifecycle

    std::stop_source m_inlineStop; // stop signal for jobs run via runPendingInline
    PingPongStaging<StagedResult> m_staging;
    std::atomic<size_t> m_liveWorkers{0};
    std::vector<std::jthread> m_workers; // last member: threads die before the state they use
};

} // namespace betacraft::streaming
