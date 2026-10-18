#include "streaming/AsyncChunkPipeline.hpp"

#include "world/Block.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <latch>
#include <utility>

namespace betacraft::streaming {

AsyncChunkPipeline::AsyncChunkPipeline(PipelineConfig config)
    : m_maxQueuedJobs(std::max<size_t>(1, config.maxQueuedJobs)),
      m_enforceLifecycle(config.enforceChunkLifecycle),
      m_viewConeCos(std::isfinite(config.viewConeCos) ? std::clamp(config.viewConeCos, -1.0f, 1.0f) : 0.5f),
      m_outOfViewScaleSq(std::isfinite(config.outOfViewDistanceScale) && config.outOfViewDistanceScale > 1.0f
                             ? config.outOfViewDistanceScale * config.outOfViewDistanceScale
                             : 1.0f),
      m_nearRadiusSq(std::isfinite(config.nearRadiusBlocks) && config.nearRadiusBlocks > 0.0f
                         ? config.nearRadiusBlocks * config.nearRadiusBlocks
                         : 0.0f),
      m_meshOptions(config.meshOptions),
      m_mesher(std::move(config.mesher)),
      m_authoritativeVersion(std::move(config.authoritativeVersion)),
      m_clock(config.clock ? std::move(config.clock) : MonotonicClock{&steadyClockNow}) {
    // BlockRegistry::get() lazily initialises a static table without synchronisation;
    // force it here, on the owning thread, before any worker can call it.
    world::BlockRegistry::initialize();

    if (config.workerCount == 0) {
        return; // inline mode
    }
    // Wait until every worker is actually running so liveWorkers() and shutdown
    // behaviour are deterministic right after construction. The latch is shared with the
    // workers: if spawning thread k throws (std::system_error), the constructor unwinds
    // while threads 0..k-1 may still count down, so it must not live on this stack frame.
    // Those threads are then stopped and joined by m_workers' destructor.
    auto started = std::make_shared<std::latch>(static_cast<std::ptrdiff_t>(config.workerCount));
    m_workers.reserve(config.workerCount);
    for (size_t i = 0; i < config.workerCount; ++i) {
        m_workers.emplace_back([this, started](std::stop_token stopToken) {
            m_liveWorkers.fetch_add(1, std::memory_order_acq_rel);
            started->count_down();
            workerLoop(stopToken);
            m_liveWorkers.fetch_sub(1, std::memory_order_acq_rel);
        });
    }
    started->wait();
}

AsyncChunkPipeline::~AsyncChunkPipeline() { shutdown(); }

// ------------------------------------------------------------------ scheduling ----

SubmitResult AsyncChunkPipeline::requestSectionMesh(SectionCoord coord,
                                                    std::shared_ptr<const SectionNeighborhoodSnapshot18> snapshot,
                                                    uint64_t version, TaskPriority priority) {
    if (!snapshot || !coord.hasValidY()) {
        return SubmitResult::RejectedInvalid;
    }
    StopList stale;
    SubmitResult result{};
    {
        const std::lock_guard lock(m_mutex);
        result = submitLocked(coord, std::move(snapshot), version, priority);
        collectStaleRunningLocked(coord, stale);
    }
    requestStops(stale);
    if (isAccepted(result)) {
        m_workCv.notify_one();
    }
    return result;
}

SubmitResult AsyncChunkPipeline::submitLocked(SectionCoord coord,
                                              std::shared_ptr<const SectionNeighborhoodSnapshot18> snapshot,
                                              uint64_t version, TaskPriority priority) {
    if (m_shutdown) {
        return SubmitResult::RejectedShutdown;
    }
    if (m_enforceLifecycle && m_lifecycle.stage(coord.chunk()) < ChunkStage::Decorated) {
        ++m_stats.rejectedNotReady; // meshing needs final blocks and light
        return SubmitResult::RejectedNotReady;
    }

    // find(), not operator[]: a rejected submission must leave no bookkeeping behind,
    // otherwise every far section the streamer tries would grow the map forever.
    const auto known = m_sections.find(coord);
    const SectionState previous = known != m_sections.end() ? known->second : SectionState{};
    if (version < previous.version) {
        ++m_stats.rejectedStale;
        return SubmitResult::RejectedStale;
    }

    SubmitResult result = SubmitResult::Accepted;
    const JobKey key{priority, keyDistanceLocked(coord), m_sequence++, coord};
    if (const auto it = m_pending.find(coord); it != m_pending.end()) {
        // De-duplication: one pending job per section, carrying the newest snapshot.
        const uint64_t ticket = m_nextTicket++;
        m_sections[coord] = SectionState{version, ticket};
        m_order.erase(it->second.key);
        it->second = PendingJob{key, std::move(snapshot), version, ticket};
        m_order.insert(key);
        ++m_stats.replaced;
        result = SubmitResult::Replaced;
    } else {
        if (m_pending.size() >= m_maxQueuedJobs) {
            const auto worst = std::prev(m_order.end());
            if (!(key < *worst)) {
                // The caller still told us `version` is the truth: a newer version makes
                // older in-flight/staged meshes stale even though this one is turned
                // away. An equal version keeps the last mesh displayable.
                if (known != m_sections.end() && version > previous.version) {
                    known->second = SectionState{version, 0};
                    refreshChunkStageLocked(coord.chunk());
                }
                ++m_stats.rejectedFull;
                return SubmitResult::RejectedQueueFull;
            }
            // Bounded queue: closer / more urgent work displaces the farthest job.
            evictLocked(*worst);
            result = SubmitResult::AcceptedEvictedOther;
        }
        const uint64_t ticket = m_nextTicket++;
        m_sections[coord] = SectionState{version, ticket};
        m_pending.emplace(coord, PendingJob{key, std::move(snapshot), version, ticket});
        m_order.insert(key);
    }
    ++m_stats.submitted;
    refreshChunkStageLocked(coord.chunk()); // the column's mesh is out of date again
    return result;
}

SubmitResult AsyncChunkPipeline::requestSectionMeshFromWorld(const world::World& world, SectionCoord coord,
                                                             uint64_t version, TaskPriority priority) {
    if (!coord.hasValidY()) {
        return SubmitResult::RejectedInvalid;
    }
    return requestSectionMesh(coord, SectionNeighborhoodSnapshot18::capture(world, coord), version, priority);
}

void AsyncChunkPipeline::setViewerPosition(const glm::vec3& pos) {
    // A NaN/inf position (camera or physics divide-by-zero) would poison every key and
    // break the std::set ordering; keep scheduling around the last valid position.
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) {
        return;
    }
    const std::lock_guard lock(m_mutex);
    m_viewer = pos;
    rekeyPendingLocked();
}

void AsyncChunkPipeline::setViewer(const glm::vec3& pos, const glm::vec3& forward) {
    const bool posValid = std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z);
    const float lenSq = glm::dot(forward, forward);
    const glm::vec3 unit = (std::isfinite(lenSq) && lenSq > 1e-12f) ? forward / std::sqrt(lenSq) : glm::vec3(0.0f);
    const std::lock_guard lock(m_mutex);
    if (posValid) {
        m_viewer = pos;
    }
    m_forward = unit;
    rekeyPendingLocked();
}

void AsyncChunkPipeline::noteSectionVersion(SectionCoord coord, uint64_t version) {
    StopList stale;
    {
        const std::lock_guard lock(m_mutex);
        // Unknown sections have nothing pending, in flight or staged to invalidate, and the
        // next request starts from scratch anyway: don't create bookkeeping for them.
        const auto it = m_sections.find(coord);
        if (it != m_sections.end() && version > it->second.version) {
            it->second = SectionState{version, 0}; // nothing submitted so far shows this edit
            refreshChunkStageLocked(coord.chunk());
        }
        collectStaleRunningLocked(coord, stale);
    }
    requestStops(stale);
}

void AsyncChunkPipeline::cancelSection(SectionCoord coord) {
    StopList stale;
    {
        const std::lock_guard lock(m_mutex);
        cancelSectionLocked(coord, stale);
        refreshChunkStageLocked(coord.chunk());
    }
    requestStops(stale);
}

void AsyncChunkPipeline::cancelChunk(core::ChunkCoord chunk) {
    StopList stale;
    {
        const std::lock_guard lock(m_mutex);
        for (int32_t y = 0; y < SectionCoord::SECTIONS_PER_COLUMN; ++y) {
            cancelSectionLocked(SectionCoord{chunk.x, y, chunk.z}, stale);
        }
        if (m_enforceLifecycle) {
            (void)m_lifecycle.advance(chunk, ChunkStage::Unloaded); // refused if already unloaded
        }
    }
    requestStops(stale);
}

bool AsyncChunkPipeline::isCurrent(SectionCoord coord, uint64_t version) const {
    const std::lock_guard lock(m_mutex);
    const auto it = m_sections.find(coord);
    return it != m_sections.end() && it->second.version == version;
}

std::vector<SectionCoord> AsyncChunkPipeline::takeEvictedSections() {
    std::vector<SectionCoord> evicted;
    const std::lock_guard lock(m_mutex);
    evicted.swap(m_evicted);
    m_evictedSet.clear();
    // Report every section that still needs a request: still loaded, and no CURRENT
    // submission pending or in flight. A stale in-flight job (superseded before the
    // eviction) will be discarded, so it must not hide the section from the streamer.
    std::erase_if(evicted, [this](const SectionCoord& c) {
        return !m_sections.contains(c) || isScheduledLocked(c);
    });
    return evicted;
}

bool AsyncChunkPipeline::isScheduled(SectionCoord coord) const {
    const std::lock_guard lock(m_mutex);
    return isScheduledLocked(coord);
}

// ----------------------------------------------------------- chunk lifecycle ----

bool AsyncChunkPipeline::advanceChunkStage(core::ChunkCoord chunk, ChunkStage to) {
    if (!m_enforceLifecycle) {
        return false;
    }
    if (to == ChunkStage::Unloaded) {
        bool wasLoaded = false;
        {
            const std::lock_guard lock(m_mutex);
            wasLoaded = m_lifecycle.stage(chunk) != ChunkStage::Unloaded;
        }
        if (wasLoaded) {
            cancelChunk(chunk);
        }
        return wasLoaded;
    }
    const std::lock_guard lock(m_mutex);
    // From Decorated on, stages follow the sections' meshes; edits are reported through
    // requestSectionMesh / noteSectionVersion, never by forcing a stage.
    if (to >= ChunkStage::Meshed || m_lifecycle.stage(chunk) >= ChunkStage::Decorated) {
        return false;
    }
    return m_lifecycle.advance(chunk, to);
}

ChunkStage AsyncChunkPipeline::chunkStage(core::ChunkCoord chunk) const {
    const std::lock_guard lock(m_mutex);
    return m_lifecycle.stage(chunk);
}

size_t AsyncChunkPipeline::trackedChunks() const {
    const std::lock_guard lock(m_mutex);
    return m_lifecycle.trackedCount();
}

// ------------------------------------------------------------------- uploads ----

DrainStats AsyncChunkPipeline::drainUploads(std::chrono::nanoseconds budget, const MeshUploader& uploader) {
    // Stats are updated per item, inside the callbacks, so an uploader that throws (GL
    // error surfaced as an exception) cannot leave the counters out of balance.
    return drainWithBudget(
        m_staging, budget, m_clock,
        [this](const StagedResult& r) {
            // The authoritative source (the World, on this thread) is consulted outside
            // our lock; it may lag or lead the caller's noteSectionVersion calls.
            bool current = true;
            if (m_authoritativeVersion) {
                const std::optional<uint64_t> truth = m_authoritativeVersion(r.result.coord);
                current = truth.has_value() && *truth == r.result.version;
            }
            const std::lock_guard lock(m_mutex);
            current = current && isLatestSubmissionLocked(r.result.coord, r.ticket);
            if (!current) {
                ++m_stats.discardedAtDrain;
            }
            return current;
        },
        [this, &uploader](StagedResult& r) {
            const SectionCoord coord = r.result.coord; // the uploader may move from r.result
            try {
                uploader(r.result);
            } catch (...) {
                const std::lock_guard lock(m_mutex);
                ++m_stats.uploadFailed; // section stays Staged: its column never reaches Uploaded
                throw;
            }
            const std::lock_guard lock(m_mutex);
            ++m_stats.uploaded;
            if (isLatestSubmissionLocked(coord, r.ticket)) {
                m_sections.find(coord)->second.progress = MeshProgress::Uploaded;
                refreshChunkStageLocked(coord.chunk());
            }
        });
}

// ------------------------------------------------------------------- control ----

size_t AsyncChunkPipeline::runPendingInline(size_t maxJobs) {
    size_t ran = 0;
    while (ran < maxJobs) {
        std::optional<RunningJob> job;
        {
            const std::lock_guard lock(m_mutex);
            if (m_shutdown) {
                break;
            }
            job = popBestLocked();
        }
        if (!job) {
            break;
        }
        executeJob(std::move(*job), m_inlineStop.get_token());
        ++ran;
    }
    return ran;
}

void AsyncChunkPipeline::pause() {
    const std::lock_guard lock(m_mutex);
    m_paused = true;
}

void AsyncChunkPipeline::resume() {
    {
        const std::lock_guard lock(m_mutex);
        m_paused = false;
    }
    m_workCv.notify_all();
}

bool AsyncChunkPipeline::waitIdle(std::chrono::nanoseconds timeout) {
    std::unique_lock lock(m_mutex);
    return m_idleCv.wait_for(lock, timeout, [this] { return m_order.empty() && m_inFlightTotal == 0; });
}

void AsyncChunkPipeline::shutdown() {
    std::vector<std::jthread> workers;
    {
        const std::lock_guard lock(m_mutex);
        m_shutdown = true;
        m_order.clear();
        m_pending.clear();
        workers.swap(m_workers);
    }
    m_inlineStop.request_stop();
    for (auto& w : workers) {
        w.request_stop(); // wakes condition_variable_any::wait(stop_token) and running meshers
    }
    m_workCv.notify_all();
    workers.clear(); // std::jthread joins on destruction
    {
        const std::lock_guard lock(m_mutex);
        m_idleCv.notify_all();
    }
}

// -------------------------------------------------------------- introspection ----

size_t AsyncChunkPipeline::workerCount() const {
    const std::lock_guard lock(m_mutex);
    return m_workers.size();
}

size_t AsyncChunkPipeline::pendingJobs() const {
    const std::lock_guard lock(m_mutex);
    return m_pending.size();
}

size_t AsyncChunkPipeline::inFlightJobs() const {
    const std::lock_guard lock(m_mutex);
    return m_inFlightTotal;
}

size_t AsyncChunkPipeline::stagedResults() const {
    // front is consumer-owned: call from the render (drain) thread only.
    return m_staging.frontRemaining() + m_staging.backSize();
}

PipelineStats AsyncChunkPipeline::stats() const {
    const std::lock_guard lock(m_mutex);
    return m_stats;
}

size_t AsyncChunkPipeline::trackedSections() const {
    const std::lock_guard lock(m_mutex);
    return m_sections.size();
}

size_t AsyncChunkPipeline::pendingEvictionReports() const {
    const std::lock_guard lock(m_mutex);
    return m_evicted.size();
}

// ------------------------------------------------------------------ internals ----

void AsyncChunkPipeline::workerLoop(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        std::optional<RunningJob> job;
        {
            std::unique_lock lock(m_mutex);
            m_workCv.wait(lock, stopToken, [this] { return !m_paused && !m_order.empty(); });
            if (stopToken.stop_requested()) {
                return;
            }
            job = popBestLocked();
        }
        if (job) {
            executeJob(std::move(*job), stopToken);
        }
    }
}

std::optional<AsyncChunkPipeline::RunningJob> AsyncChunkPipeline::popBestLocked() {
    if (m_order.empty()) {
        return std::nullopt;
    }
    const SectionCoord coord = m_order.begin()->coord;
    m_order.erase(m_order.begin());
    auto it = m_pending.find(coord);
    RunningJob job{coord, std::move(it->second.snapshot), it->second.version, it->second.ticket, {}};
    m_pending.erase(it);
    m_inFlight[coord].push_back(InFlightEntry{job.ticket, job.stop});
    ++m_inFlightTotal;
    return job;
}

void AsyncChunkPipeline::executeJob(RunningJob job, std::stop_token stopToken) {
    {
        const std::lock_guard lock(m_mutex);
        if (!isLatestSubmissionLocked(job.coord, job.ticket)) {
            ++m_stats.skippedStale; // edited or unloaded while queued: don't burn CPU
            finishJobLocked(job.coord, job.ticket);
            return;
        }
    }

    std::optional<RawMeshBuffer> mesh;
    bool failed = false;
    {
        // The mesher sees one token that fires on EITHER a pipeline stop (shutdown) or
        // this job becoming stale (unload, edit, newer submission).
        std::stop_callback forwardShutdown(stopToken, [&job]() noexcept { job.stop.request_stop(); });
        try {
            mesh = m_mesher ? m_mesher(*job.snapshot, job.coord, m_meshOptions, job.stop.get_token())
                            : SnapshotMesher::build(*job.snapshot, job.coord.y, m_meshOptions, job.stop.get_token());
        } catch (...) {
            // An exception escaping a std::jthread would std::terminate the whole game.
            failed = true;
        }
    }
    job.snapshot.reset(); // release the 17 KB copy before taking the lock

    const std::lock_guard lock(m_mutex);
    const bool current = isLatestSubmissionLocked(job.coord, job.ticket);
    if (failed) {
        ++m_stats.failed;
    } else if (!mesh) {
        ++(current ? m_stats.aborted : m_stats.abortedStale);
    } else if (!current) {
        ++m_stats.discardedStale;
    } else {
        // Published before the job is marked finished, so waitIdle() implies "staged".
        m_staging.publish(StagedResult{job.ticket, ChunkMeshResult{job.coord, job.version, std::move(*mesh)}});
        ++m_stats.completed;
        m_sections.find(job.coord)->second.progress = MeshProgress::Staged;
        refreshChunkStageLocked(job.coord.chunk());
    }
    finishJobLocked(job.coord, job.ticket);
}

void AsyncChunkPipeline::finishJobLocked(SectionCoord coord, uint64_t ticket) {
    if (const auto it = m_inFlight.find(coord); it != m_inFlight.end()) {
        std::erase_if(it->second, [ticket](const InFlightEntry& e) { return e.ticket == ticket; });
        if (it->second.empty()) {
            m_inFlight.erase(it);
        }
    }
    --m_inFlightTotal;
    if (m_order.empty() && m_inFlightTotal == 0) {
        m_idleCv.notify_all();
    }
}

void AsyncChunkPipeline::eraseJobLocked(SectionCoord coord) {
    if (const auto it = m_pending.find(coord); it != m_pending.end()) {
        m_order.erase(it->second.key);
        m_pending.erase(it);
    }
}

void AsyncChunkPipeline::rekeyPendingLocked() {
    std::set<JobKey> reordered;
    for (auto& [coord, job] : m_pending) {
        job.key.distanceSq = keyDistanceLocked(coord); // sequence kept: FIFO among ties survives
        reordered.insert(job.key);
    }
    m_order.swap(reordered);
}

float AsyncChunkPipeline::keyDistanceLocked(SectionCoord coord) const noexcept {
    // Radius of the sphere bounding a 16^3 section: 8 * sqrt(3).
    constexpr float kSectionRadius = 13.856406f;
    const glm::vec3 d = coord.centre() - m_viewer;
    const float distSq = glm::dot(d, d);
    if (std::isnan(distSq)) {
        return std::numeric_limits<float>::infinity(); // NaN can never reach the comparator
    }
    if (m_outOfViewScaleSq <= 1.0f || m_forward == glm::vec3(0.0f) || distSq <= m_nearRadiusSq) {
        return distSq;
    }
    // Conservative sphere-vs-cone test: widening the cone by the section radius keeps
    // sections that merely poke into the view in the "visible" class.
    const bool inView = glm::dot(d, m_forward) >= m_viewConeCos * std::sqrt(distSq) - kSectionRadius;
    const float key = inView ? distSq : distSq * m_outOfViewScaleSq; // overflow saturates to +inf
    return std::isnan(key) ? std::numeric_limits<float>::infinity() : key;
}

void AsyncChunkPipeline::evictLocked(const JobKey& worst) {
    const SectionCoord victim = worst.coord;
    m_pending.erase(victim);
    m_order.erase(worst);
    // The evicted job carried the section's newest ticket: no submission of it may be
    // uploaded any more until the streamer asks again.
    if (const auto it = m_sections.find(victim); it != m_sections.end()) {
        it->second.ticket = 0;
        it->second.progress = MeshProgress::Waiting;
        refreshChunkStageLocked(victim.chunk());
    }
    if (m_evictedSet.insert(victim).second) {
        m_evicted.push_back(victim); // one report per section, in eviction order
    }
    ++m_stats.evicted;
}

bool AsyncChunkPipeline::isScheduledLocked(SectionCoord coord) const {
    if (m_pending.contains(coord)) {
        return true;
    }
    const auto section = m_sections.find(coord);
    const auto running = m_inFlight.find(coord);
    if (section == m_sections.end() || section->second.ticket == 0 || running == m_inFlight.end()) {
        return false;
    }
    const uint64_t current = section->second.ticket;
    return std::ranges::any_of(running->second, [current](const InFlightEntry& e) { return e.ticket == current; });
}

void AsyncChunkPipeline::cancelSectionLocked(SectionCoord coord, StopList& stale) {
    if (m_pending.contains(coord)) {
        eraseJobLocked(coord);
        ++m_stats.cancelled;
    }
    m_sections.erase(coord); // in-flight and staged results lose their ticket
    collectStaleRunningLocked(coord, stale);
    if (m_order.empty() && m_inFlightTotal == 0) {
        m_idleCv.notify_all();
    }
}

void AsyncChunkPipeline::refreshChunkStageLocked(core::ChunkCoord chunk) {
    if (!m_enforceLifecycle) {
        return;
    }
    const ChunkStage stage = m_lifecycle.stage(chunk);
    if (stage < ChunkStage::Decorated) {
        return;
    }
    bool any = false;
    bool allStaged = true;
    bool allUploaded = true;
    for (int32_t y = 0; y < SectionCoord::SECTIONS_PER_COLUMN; ++y) {
        const auto it = m_sections.find(SectionCoord{chunk.x, y, chunk.z});
        if (it == m_sections.end()) {
            continue; // never requested (e.g. empty sky) or cancelled: not part of the mesh
        }
        any = true;
        allStaged = allStaged && it->second.progress != MeshProgress::Waiting;
        allUploaded = allUploaded && it->second.progress == MeshProgress::Uploaded;
    }
    ChunkStage target = ChunkStage::Decorated;
    if (any && allUploaded) {
        target = ChunkStage::Uploaded;
    } else if (any && allStaged) {
        target = ChunkStage::Meshed;
    }
    if (target == stage) {
        return;
    }
    // Only legal edges are taken: back to Decorated (mesh invalidated), then forward one
    // stage at a time.
    if (target < stage) {
        (void)m_lifecycle.advance(chunk, ChunkStage::Decorated);
    }
    for (ChunkStage s = m_lifecycle.stage(chunk); s < target;) {
        s = static_cast<ChunkStage>(static_cast<uint8_t>(s) + 1u);
        (void)m_lifecycle.advance(chunk, s);
    }
}

void AsyncChunkPipeline::collectStaleRunningLocked(SectionCoord coord, StopList& out) {
    const auto running = m_inFlight.find(coord);
    if (running == m_inFlight.end()) {
        return;
    }
    for (InFlightEntry& e : running->second) {
        if (!isLatestSubmissionLocked(coord, e.ticket) && !e.stop.stop_requested()) {
            out.push_back(e.stop);
        }
    }
}

void AsyncChunkPipeline::requestStops(StopList& stops) noexcept {
    // Outside m_mutex: request_stop runs the job's stop callbacks synchronously (e.g. a
    // condition_variable_any wake-up inside a user mesher), which must not nest our lock.
    for (std::stop_source& s : stops) {
        s.request_stop();
    }
}

bool AsyncChunkPipeline::isAccepted(SubmitResult r) noexcept {
    return r == SubmitResult::Accepted || r == SubmitResult::Replaced || r == SubmitResult::AcceptedEvictedOther;
}

bool AsyncChunkPipeline::isLatestSubmissionLocked(SectionCoord coord, uint64_t ticket) const {
    const auto it = m_sections.find(coord);
    return ticket != 0 && it != m_sections.end() && it->second.ticket == ticket;
}

} // namespace betacraft::streaming
