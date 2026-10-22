#include "world/ChunkGenPipeline.hpp"

#include <iterator>
#include <utility>

namespace betacraft::world {

ChunkGenPipeline::ChunkGenPipeline(std::shared_ptr<const world::WorldGen> gen, ChunkGenConfig config)
    : m_gen(std::move(gen)) {
    m_workers.reserve(config.workerCount);
    for (size_t i = 0; i < config.workerCount; ++i) {
        m_workers.emplace_back([this](std::stop_token stop) { workerLoop(std::move(stop)); });
    }
}

ChunkGenPipeline::~ChunkGenPipeline() {
    {
        const std::lock_guard lock(m_mutex);
        m_shutdown = true;
        m_order.clear();
        m_pending.clear();
        for (auto& [coord, running] : m_running) {
            running.stop.request_stop();
        }
        m_running.clear();
    }
    for (std::jthread& worker : m_workers) {
        worker.request_stop();
    }
    m_workCv.notify_all();
    m_workers.clear(); // Joins
}

std::unique_ptr<voxel::Chunk> ChunkGenPipeline::generate(const world::WorldGen& gen, core::ChunkCoord coord) {
    auto chunk = std::make_unique<voxel::Chunk>(coord);
    gen.generateChunk(*chunk);
    chunk->recalculateLighting();
    return chunk;
}

int64_t ChunkGenPipeline::distanceSqLocked(core::ChunkCoord coord) const noexcept {
    const int64_t dx = static_cast<int64_t>(coord.x) - m_center.x;
    const int64_t dz = static_cast<int64_t>(coord.z) - m_center.z;
    return dx * dx + dz * dz;
}

bool ChunkGenPipeline::request(core::ChunkCoord coord) {
    {
        const std::lock_guard lock(m_mutex);
        if (m_shutdown || m_pending.contains(coord) || m_running.contains(coord)) {
            return false;
        }
        for (const auto& [sequence, r] : m_ready) {
            if (r.coord == coord) return false;
        }
        const JobKey key{distanceSqLocked(coord), m_sequence++, coord};
        m_order.insert(key);
        m_pending.emplace(coord, key);
        ++m_stats.requested;
    }
    m_workCv.notify_one();
    return true;
}

void ChunkGenPipeline::setCenter(core::ChunkCoord center) {
    const std::lock_guard lock(m_mutex);
    if (center == m_center) return;
    m_center = center;
    m_order.clear();
    for (auto& [coord, key] : m_pending) {
        key.distanceSq = distanceSqLocked(coord);
        m_order.insert(key);
    }
}

bool ChunkGenPipeline::cancel(core::ChunkCoord coord) {
    const std::lock_guard lock(m_mutex);
    if (const auto it = m_pending.find(coord); it != m_pending.end()) {
        m_order.erase(it->second);
        m_pending.erase(it);
        ++m_stats.cancelled;
        if (idleLocked()) m_idleCv.notify_all();
        return true;
    }
    if (const auto it = m_running.find(coord); it != m_running.end()) {
        it->second.stop.request_stop(); // Its result no longer matches m_running: discarded
        m_running.erase(it);
        ++m_stats.cancelled;
        return true;
    }
    for (auto it = m_ready.begin(); it != m_ready.end(); ++it) {
        if (it->second.coord == coord) {
            m_ready.erase(it);
            ++m_stats.cancelled;
            return true;
        }
    }
    return false;
}

void ChunkGenPipeline::setWorldGen(std::shared_ptr<const world::WorldGen> gen) {
    const std::lock_guard lock(m_mutex);
    m_gen = std::move(gen);
    m_stats.cancelled += m_pending.size() + m_running.size() + m_ready.size();
    m_order.clear();
    m_pending.clear();
    for (auto& [coord, running] : m_running) {
        running.stop.request_stop();
    }
    m_running.clear();
    m_ready.clear();
    if (idleLocked()) m_idleCv.notify_all();
}

std::shared_ptr<const world::WorldGen> ChunkGenPipeline::worldGen() const {
    const std::lock_guard lock(m_mutex);
    return m_gen;
}

ChunkGenPipeline::Job ChunkGenPipeline::popJobLocked() {
    const JobKey key = *m_order.begin();
    m_order.erase(m_order.begin());
    m_pending.erase(key.coord);
    Running running{m_nextTicket++, std::stop_source{}};
    Job job{key.coord, running.ticket, running.stop.get_token(), m_gen, key.sequence};
    m_running[key.coord] = std::move(running);
    ++m_active;
    return job;
}

void ChunkGenPipeline::runJob(std::unique_lock<std::mutex>& lock, Job job) {
    std::unique_ptr<voxel::Chunk> chunk;
    if (!job.stop.stop_requested()) {
        lock.unlock();
        chunk = generate(*job.gen, job.coord);
        lock.lock();
    }
    --m_active;
    const auto it = m_running.find(job.coord);
    const bool current = it != m_running.end() && it->second.ticket == job.ticket;
    if (current && chunk && !job.stop.stop_requested()) {
        m_running.erase(it);
        m_ready.emplace(job.sequence, GeneratedChunk{job.coord, std::move(chunk)});
        ++m_stats.completed;
    } else {
        if (current) m_running.erase(it);
        ++m_stats.discarded;
    }
    if (idleLocked()) m_idleCv.notify_all();
}

void ChunkGenPipeline::workerLoop(std::stop_token stop) {
    std::unique_lock lock(m_mutex);
    while (!stop.stop_requested()) {
        m_workCv.wait(lock, stop, [this] { return !m_order.empty() || m_shutdown; });
        if (stop.stop_requested() || m_shutdown) return;
        runJob(lock, popJobLocked());
    }
}

std::vector<GeneratedChunk> ChunkGenPipeline::takeReady(size_t maxChunks) {
    std::vector<GeneratedChunk> out;
    const std::lock_guard lock(m_mutex);
    while (!m_ready.empty() && out.size() < maxChunks) {
        const auto it = nearestReadyLocked();
        out.push_back(std::move(it->second));
        m_ready.erase(it);
    }
    return out;
}

std::optional<GeneratedChunk> ChunkGenPipeline::popReady() {
    const std::lock_guard lock(m_mutex);
    if (m_ready.empty()) return std::nullopt;
    const auto it = nearestReadyLocked();
    GeneratedChunk out = std::move(it->second);
    m_ready.erase(it);
    return out;
}

std::map<uint64_t, GeneratedChunk>::iterator ChunkGenPipeline::nearestReadyLocked() {
    // Few results wait at a time (the World drains them every frame): a scan is enough.
    auto best = m_ready.begin();
    int64_t bestDistance = distanceSqLocked(best->second.coord);
    for (auto it = std::next(best); it != m_ready.end(); ++it) {
        const int64_t distance = distanceSqLocked(it->second.coord);
        if (distance < bestDistance) { // Ties keep the earlier request (map order)
            best = it;
            bestDistance = distance;
        }
    }
    return best;
}

bool ChunkGenPipeline::isScheduled(core::ChunkCoord coord) const {
    const std::lock_guard lock(m_mutex);
    if (m_pending.contains(coord) || m_running.contains(coord)) return true;
    for (const auto& [sequence, r] : m_ready) {
        if (r.coord == coord) return true;
    }
    return false;
}

size_t ChunkGenPipeline::runPendingInline(size_t maxJobs) {
    if (!m_workers.empty()) return 0;
    std::unique_lock lock(m_mutex);
    size_t ran = 0;
    while (ran < maxJobs && !m_order.empty()) {
        runJob(lock, popJobLocked());
        ++ran;
    }
    return ran;
}

bool ChunkGenPipeline::waitIdle(std::chrono::nanoseconds timeout) {
    if (m_workers.empty()) {
        (void)runPendingInline(SIZE_MAX);
        return true;
    }
    std::unique_lock lock(m_mutex);
    if (timeout == std::chrono::nanoseconds::max()) {
        m_idleCv.wait(lock, [this] { return idleLocked(); });
        return true;
    }
    return m_idleCv.wait_for(lock, timeout, [this] { return idleLocked(); });
}

size_t ChunkGenPipeline::pendingJobs() const {
    const std::lock_guard lock(m_mutex);
    return m_pending.size();
}

size_t ChunkGenPipeline::runningJobs() const {
    const std::lock_guard lock(m_mutex);
    return m_active;
}

size_t ChunkGenPipeline::readyResults() const {
    const std::lock_guard lock(m_mutex);
    return m_ready.size();
}

ChunkGenStats ChunkGenPipeline::stats() const {
    const std::lock_guard lock(m_mutex);
    return m_stats;
}

} // namespace betacraft::world
