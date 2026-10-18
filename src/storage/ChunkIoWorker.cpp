#include "storage/ChunkIoWorker.hpp"

#include "core/Logger.hpp"

#include <utility>

namespace betacraft::storage {

ChunkIoWorker::ChunkIoWorker(IChunkStore& store, ChunkIoConfig config)
    : m_store(store), m_threaded(config.threaded) {
    if (m_threaded) {
        m_thread = std::jthread([this](std::stop_token stop) { workerLoop(std::move(stop)); });
    }
}

ChunkIoWorker::~ChunkIoWorker() {
    {
        const std::lock_guard lock(m_mutex);
        m_stopping = true;
        m_loadOrder.clear(); // Nobody will take the results
    }
    m_workCv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join(); // Returns once the save queue is empty
    } else {
        std::unique_lock lock(m_mutex);
        while (hasWorkLocked()) {
            runOneLocked(lock);
        }
    }
    if (!m_store.flush()) {
        core::Logger::warn("ChunkIoWorker", "Final flush of the chunk store failed");
    }
}

void ChunkIoWorker::enqueueSave(SaveJob job) {
    auto bytes = std::make_shared<const std::vector<uint8_t>>(std::move(job.bundleBytes));
    {
        const std::lock_guard lock(m_mutex);
        ++m_stats.savesEnqueued;
        auto [it, inserted] = m_saves.try_emplace(job.coord, bytes);
        if (inserted) {
            m_saveOrder.push_back(job.coord);
        } else {
            it->second = std::move(bytes); // Same place in the queue, newest contents
            ++m_stats.savesSuperseded;
        }
    }
    m_workCv.notify_one();
}

void ChunkIoWorker::loadAsync(core::ChunkCoord coord, uint64_t ticket) {
    {
        const std::lock_guard lock(m_mutex);
        if (m_stopping) return;
        m_loadOrder.push_back({coord, ticket});
    }
    m_workCv.notify_one();
}

std::vector<LoadResult> ChunkIoWorker::takeCompletedLoads() {
    std::vector<LoadResult> out;
    const std::lock_guard lock(m_mutex);
    out.swap(m_completed);
    return out;
}

ChunkIoWorker::Bytes ChunkIoWorker::pendingBytesLocked(core::ChunkCoord coord) const {
    if (const auto it = m_saves.find(coord); it != m_saves.end()) {
        return it->second; // Newer than the one being written, if any
    }
    if (m_writingCoord && *m_writingCoord == coord) {
        return m_writingBytes;
    }
    return nullptr;
}

std::optional<std::vector<uint8_t>> ChunkIoWorker::pendingSave(core::ChunkCoord coord) const {
    Bytes bytes;
    {
        const std::lock_guard lock(m_mutex);
        bytes = pendingBytesLocked(coord);
    }
    if (!bytes) return std::nullopt;
    return *bytes; // Immutable once queued: copied outside the lock
}

bool ChunkIoWorker::hasPendingSave(core::ChunkCoord coord) const {
    const std::lock_guard lock(m_mutex);
    return pendingBytesLocked(coord) != nullptr;
}

void ChunkIoWorker::runOneLocked(std::unique_lock<std::mutex>& lock) {
    if (!m_loadOrder.empty()) {
        const LoadJob job = m_loadOrder.front();
        const core::ChunkCoord coord = job.coord;
        m_loadOrder.pop_front();
        LoadResult result{coord, std::nullopt, false, job.ticket};
        Bytes pending = pendingBytesLocked(coord);
        if (!pending) {
            m_busy = true;
            lock.unlock();
            std::optional<std::vector<uint8_t>> read = m_store.read(coord);
            lock.lock();
            m_busy = false;
            pending = pendingBytesLocked(coord); // A save queued meanwhile is newer than the disk
            if (!pending) {
                result.bundleBytes = std::move(read);
            }
        }
        if (pending) {
            result.bundleBytes = *pending;
            result.fromPendingSave = true;
            ++m_stats.loadsFromQueue;
        }
        ++m_stats.loadsCompleted;
        if (!m_stopping) {
            m_completed.push_back(std::move(result));
        }
    } else if (!m_saveOrder.empty()) {
        const core::ChunkCoord coord = m_saveOrder.front();
        m_saveOrder.pop_front();
        const auto it = m_saves.find(coord);
        Bytes bytes = std::move(it->second);
        m_saves.erase(it);
        m_writingCoord = coord;
        m_writingBytes = bytes;
        m_busy = true;
        lock.unlock();
        const bool ok = m_store.write(coord, *bytes);
        lock.lock();
        m_busy = false;
        m_writingCoord.reset();
        m_writingBytes.reset();
        if (ok) {
            ++m_stats.savesWritten;
        } else {
            ++m_stats.saveFailures;
            m_failedSinceFlush = true;
            core::Logger::warn("ChunkIoWorker", "Failed to write chunk ", coord.x, ",", coord.z);
            if (!m_saves.contains(coord)) { // A newer save replaces this one anyway
                m_failed.push_back({coord, *bytes});
            }
        }
    }
    if (idleLocked()) {
        m_idleCv.notify_all();
    }
}

void ChunkIoWorker::workerLoop(std::stop_token stop) {
    std::unique_lock lock(m_mutex);
    for (;;) {
        m_workCv.wait(lock, [&] { return hasWorkLocked() || m_stopping || stop.stop_requested(); });
        if (!hasWorkLocked()) {
            if (m_stopping || stop.stop_requested()) return;
            continue;
        }
        runOneLocked(lock);
    }
}

bool ChunkIoWorker::waitIdle(std::chrono::nanoseconds timeout) {
    std::unique_lock lock(m_mutex);
    if (!m_threaded) {
        while (hasWorkLocked()) {
            runOneLocked(lock);
        }
        return true;
    }
    if (timeout == std::chrono::nanoseconds::max()) {
        m_idleCv.wait(lock, [this] { return idleLocked(); });
        return true;
    }
    return m_idleCv.wait_for(lock, timeout, [this] { return idleLocked(); });
}

size_t ChunkIoWorker::runPendingInline(size_t maxJobs) {
    if (m_threaded) return 0;
    std::unique_lock lock(m_mutex);
    size_t ran = 0;
    while (ran < maxJobs && hasWorkLocked()) {
        runOneLocked(lock);
        ++ran;
    }
    return ran;
}

bool ChunkIoWorker::flushAll() {
    (void)waitIdle();
    bool ok = m_store.flush();
    const std::lock_guard lock(m_mutex);
    ok = ok && !m_failedSinceFlush;
    m_failedSinceFlush = false;
    return ok;
}

std::vector<SaveJob> ChunkIoWorker::takeFailedSaves() {
    std::vector<SaveJob> out;
    const std::lock_guard lock(m_mutex);
    out.swap(m_failed);
    return out;
}

size_t ChunkIoWorker::pendingSaves() const {
    const std::lock_guard lock(m_mutex);
    return m_saveOrder.size() + (m_writingCoord ? 1U : 0U);
}

size_t ChunkIoWorker::pendingLoads() const {
    const std::lock_guard lock(m_mutex);
    return m_loadOrder.size() + (m_busy && !m_writingCoord ? 1U : 0U);
}

size_t ChunkIoWorker::completedLoads() const {
    const std::lock_guard lock(m_mutex);
    return m_completed.size();
}

ChunkIoStats ChunkIoWorker::stats() const {
    const std::lock_guard lock(m_mutex);
    return m_stats;
}

} // namespace betacraft::storage
