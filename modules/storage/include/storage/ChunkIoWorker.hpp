#pragma once

#include "core/Types.hpp"
#include "storage/IChunkStore.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <vector>

namespace betacraft::storage {

// One chunk to write: its bundle already encoded (ChunkBundleCodec) on the main thread.
struct SaveJob {
    core::ChunkCoord coord{};
    std::vector<uint8_t> bundleBytes;
};

// The bundle bytes of a loadAsync; nullopt when the store has no (readable) chunk there.
// `ticket` echoes the one passed to loadAsync, so the caller can tell the answer to its
// current request from a late answer to an older, cancelled one for the same chunk.
struct LoadResult {
    core::ChunkCoord coord{};
    std::optional<std::vector<uint8_t>> bundleBytes;
    bool fromPendingSave{false}; // Served from the save queue, not the store
    uint64_t ticket{0};
};

struct ChunkIoStats {
    uint64_t savesEnqueued{0};
    uint64_t savesSuperseded{0}; // Replaced by a newer save of the same chunk before written
    uint64_t savesWritten{0};
    uint64_t saveFailures{0};
    uint64_t loadsCompleted{0};
    uint64_t loadsFromQueue{0}; // Answered from a pending save
};

struct ChunkIoConfig {
    // false: no thread; jobs run on the caller inside runPendingInline, waitIdle and flushAll
    // (deterministic tests).
    bool threaded{true};
};

// Chunk disk I/O off the main thread. One std::jthread serves:
//  * loads (loadAsync), first, in request order; results are collected by takeCompletedLoads;
//  * saves (enqueueSave), in order, keeping only the newest job of a chunk; IChunkStore::write
//    (compression included, RegionFile) runs on the worker.
// A chunk whose save is still queued or being written is read from the queue (loadAsync and
// pendingSave), so a reload never sees older bytes than the last save. The store must be
// thread-safe against the caller's own reads (RegionChunkStore is).
class ChunkIoWorker {
public:
    explicit ChunkIoWorker(IChunkStore& store, ChunkIoConfig config = {});
    // Writes every pending save (no loss), joins the thread and flushes the store.
    ~ChunkIoWorker();

    ChunkIoWorker(const ChunkIoWorker&) = delete;
    ChunkIoWorker& operator=(const ChunkIoWorker&) = delete;

    void enqueueSave(SaveJob job);
    void loadAsync(core::ChunkCoord coord, uint64_t ticket = 0);
    [[nodiscard]] std::vector<LoadResult> takeCompletedLoads();

    // Copy of the newest queued (or being written) bytes of `coord`, if any.
    [[nodiscard]] std::optional<std::vector<uint8_t>> pendingSave(core::ChunkCoord coord) const;
    [[nodiscard]] bool hasPendingSave(core::ChunkCoord coord) const;

    // Blocks until every save enqueued so far is written, then flushes the store. False when
    // a write since the previous flushAll failed (see takeFailedSaves) or the flush did.
    bool flushAll();
    // Blocks until no load or save is queued or running (loads then wait in
    // takeCompletedLoads). Tests synchronise with this, never with a sleep.
    bool waitIdle(std::chrono::nanoseconds timeout = std::chrono::nanoseconds::max());
    // Unthreaded mode: runs up to `maxJobs` queued jobs on the caller. Returns how many ran.
    size_t runPendingInline(size_t maxJobs);

    // Saves whose write failed since the last call (the store kept the previous bundle).
    [[nodiscard]] std::vector<SaveJob> takeFailedSaves();

    [[nodiscard]] size_t pendingSaves() const;    // Queued or being written
    [[nodiscard]] size_t pendingLoads() const;    // Queued or being read
    [[nodiscard]] size_t completedLoads() const;  // Waiting in takeCompletedLoads
    [[nodiscard]] ChunkIoStats stats() const;
    [[nodiscard]] bool isThreaded() const noexcept { return m_threaded; }

private:
    using Bytes = std::shared_ptr<const std::vector<uint8_t>>;

    void workerLoop(std::stop_token stop);
    [[nodiscard]] bool hasWorkLocked() const noexcept { return !m_loadOrder.empty() || !m_saveOrder.empty(); }
    [[nodiscard]] bool idleLocked() const noexcept { return !hasWorkLocked() && !m_busy; }
    [[nodiscard]] Bytes pendingBytesLocked(core::ChunkCoord coord) const;
    // Takes the next job (loads first) and runs it with the lock released.
    void runOneLocked(std::unique_lock<std::mutex>& lock);

    IChunkStore& m_store;
    const bool m_threaded;

    mutable std::mutex m_mutex;
    std::condition_variable_any m_workCv;
    std::condition_variable m_idleCv;
    struct LoadJob {
        core::ChunkCoord coord{};
        uint64_t ticket{0};
    };
    std::deque<LoadJob> m_loadOrder;
    std::deque<core::ChunkCoord> m_saveOrder; // Each coord once; its bytes in m_saves
    std::unordered_map<core::ChunkCoord, Bytes> m_saves;
    std::optional<core::ChunkCoord> m_writingCoord; // Save being written, still readable
    Bytes m_writingBytes;
    bool m_busy{false};     // A job runs outside the lock
    bool m_stopping{false}; // Destructor: finish the saves, drop the loads
    bool m_failedSinceFlush{false};
    std::vector<LoadResult> m_completed;
    std::vector<SaveJob> m_failed;
    ChunkIoStats m_stats{};

    std::jthread m_thread; // Last member: stops before the state it uses
};

} // namespace betacraft::storage
