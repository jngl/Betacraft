// ChunkIoWorker: saves serialised on the caller, written on one worker thread with only
// the newest job per chunk kept, loads first and answered from the save queue while a save is
// pending. Synchronisation goes through waitIdle / flushAll and explicit gates, never sleep.
#include "StorageTestHelpers.hpp"
#include "storage/ChunkIoWorker.hpp"
#include "storage/RegionChunkStore.hpp"
#include "test_support/TempDir.hpp"

#include <gtest/gtest.h>

#include <condition_variable>
#include <map>
#include <mutex>
#include <utility>

using namespace betacraft;
using storage::ChunkIoConfig;
using storage::ChunkIoWorker;
using storage::SaveJob;
using test::storage::noise;

namespace {

struct CoordLess {
    bool operator()(const core::ChunkCoord& a, const core::ChunkCoord& b) const noexcept {
        return a.x != b.x ? a.x < b.x : a.z < b.z;
    }
};

// In-memory store recording every write. With the gate closed, write() blocks after
// announcing itself, so a test can hold the worker in the middle of a save.
class RecordingStore final : public storage::IChunkStore {
public:
    std::optional<std::vector<uint8_t>> read(core::ChunkCoord coord) override {
        const std::lock_guard lock(m_mutex);
        const auto it = m_data.find(coord);
        if (it == m_data.end()) return std::nullopt;
        return it->second;
    }
    bool contains(core::ChunkCoord coord) override {
        const std::lock_guard lock(m_mutex);
        return m_data.contains(coord);
    }
    bool write(core::ChunkCoord coord, std::span<const uint8_t> bytes) override {
        std::unique_lock lock(m_mutex);
        ++m_writesStarted;
        m_cv.notify_all();
        m_cv.wait(lock, [this] { return m_gateOpen; });
        m_data[coord].assign(bytes.begin(), bytes.end());
        ++m_writes;
        return true;
    }
    bool flush() override {
        const std::lock_guard lock(m_mutex);
        ++m_flushes;
        m_flushedWrites = m_writes;
        return true;
    }

    void closeGate() {
        const std::lock_guard lock(m_mutex);
        m_gateOpen = false;
    }
    void openGate() {
        {
            const std::lock_guard lock(m_mutex);
            m_gateOpen = true;
        }
        m_cv.notify_all();
    }
    void waitWritesStarted(int n) {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [&] { return m_writesStarted >= n; });
    }
    void put(core::ChunkCoord coord, std::vector<uint8_t> bytes) {
        const std::lock_guard lock(m_mutex);
        m_data[coord] = std::move(bytes);
    }
    [[nodiscard]] int writes() const {
        const std::lock_guard lock(m_mutex);
        return m_writes;
    }
    [[nodiscard]] int flushes() const {
        const std::lock_guard lock(m_mutex);
        return m_flushes;
    }
    [[nodiscard]] int flushedWrites() const {
        const std::lock_guard lock(m_mutex);
        return m_flushedWrites;
    }
    [[nodiscard]] size_t size() const {
        const std::lock_guard lock(m_mutex);
        return m_data.size();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_gateOpen{true};
    int m_writesStarted{0};
    int m_writes{0};
    int m_flushes{0};
    int m_flushedWrites{0};
    std::map<core::ChunkCoord, std::vector<uint8_t>, CoordLess> m_data;
};

[[nodiscard]] std::vector<uint8_t> bytesOf(uint8_t tag, size_t size = 64) {
    std::vector<uint8_t> out = noise(size, tag);
    out[0] = tag;
    return out;
}

} // namespace

TEST(ChunkIoWorker, LastWriteWinsPerCoord) {
    // Unthreaded: three queued saves of one chunk become a single write of the last.
    {
        RecordingStore store;
        ChunkIoWorker io(store, ChunkIoConfig{.threaded = false});
        for (uint8_t v = 1; v <= 3; ++v) {
            io.enqueueSave({{4, -2}, bytesOf(v)});
        }
        io.enqueueSave({{5, -2}, bytesOf(9)});
        EXPECT_EQ(io.pendingSaves(), 2U);
        EXPECT_EQ(io.runPendingInline(100), 2U);
        EXPECT_EQ(store.writes(), 2);
        EXPECT_EQ(store.read({4, -2}), bytesOf(3));
        EXPECT_EQ(store.read({5, -2}), bytesOf(9));
        EXPECT_EQ(io.stats().savesSuperseded, 2U);
    }
    // Threaded: while version 1 is being written, versions 2 and 3 collapse into one job that
    // is written after it, so the store ends with version 3.
    RecordingStore store;
    ChunkIoWorker io(store);
    store.closeGate();
    io.enqueueSave({{0, 0}, bytesOf(1)});
    store.waitWritesStarted(1);
    io.enqueueSave({{0, 0}, bytesOf(2)});
    io.enqueueSave({{0, 0}, bytesOf(3)});
    EXPECT_EQ(io.pendingSave({0, 0}), bytesOf(3)) << "the newest queued bytes shadow the one in flight";
    store.openGate();
    ASSERT_TRUE(io.flushAll());
    EXPECT_EQ(store.read({0, 0}), bytesOf(3));
    EXPECT_EQ(store.writes(), 2);
    EXPECT_FALSE(io.hasPendingSave({0, 0}));
}

TEST(ChunkIoWorker, LoadSeesPendingSave) {
    // Queued save: the load is answered from the queue, never from the older store contents.
    {
        RecordingStore store;
        store.put({1, 1}, bytesOf(7));
        ChunkIoWorker io(store, ChunkIoConfig{.threaded = false});
        io.enqueueSave({{1, 1}, bytesOf(8)});
        io.loadAsync({1, 1});
        io.loadAsync({2, 2}); // Absent
        EXPECT_EQ(io.runPendingInline(2), 2U) << "loads run before saves";
        EXPECT_EQ(store.writes(), 0);
        std::vector<storage::LoadResult> loads = io.takeCompletedLoads();
        ASSERT_EQ(loads.size(), 2U);
        EXPECT_EQ(loads[0].coord, (core::ChunkCoord{1, 1}));
        EXPECT_TRUE(loads[0].fromPendingSave);
        EXPECT_EQ(loads[0].bundleBytes, bytesOf(8));
        EXPECT_FALSE(loads[1].bundleBytes.has_value());
        EXPECT_FALSE(loads[1].fromPendingSave);
    }
    // Save being written (store blocked mid-write): still served from the queue.
    RecordingStore store;
    store.put({3, 3}, bytesOf(1));
    ChunkIoWorker io(store);
    store.closeGate();
    io.enqueueSave({{3, 3}, bytesOf(2)});
    store.waitWritesStarted(1);
    EXPECT_EQ(io.pendingSave({3, 3}), bytesOf(2));
    io.loadAsync({3, 3});
    store.openGate();
    ASSERT_TRUE(io.waitIdle());
    std::vector<storage::LoadResult> loads = io.takeCompletedLoads();
    ASSERT_EQ(loads.size(), 1U);
    EXPECT_EQ(loads[0].bundleBytes, bytesOf(2));
    // Once written, the store answers.
    io.loadAsync({3, 3});
    ASSERT_TRUE(io.waitIdle());
    loads = io.takeCompletedLoads();
    ASSERT_EQ(loads.size(), 1U);
    EXPECT_FALSE(loads[0].fromPendingSave);
    EXPECT_EQ(loads[0].bundleBytes, bytesOf(2));
}

TEST(ChunkIoWorker, FlushAllDrainsQueue) {
    test::TempDir dir("betacraft_s7_io_");
    {
        storage::RegionChunkStore store(dir.path() / "region");
        ChunkIoWorker io(store);
        for (int32_t i = 0; i < 64; ++i) {
            // Two regions, and every chunk saved twice: the second version must be the one kept.
            io.enqueueSave({{i - 32, i % 5}, noise(3000 + static_cast<size_t>(i) * 17, static_cast<uint32_t>(i))});
            io.enqueueSave({{i - 32, i % 5}, noise(2000 + static_cast<size_t>(i) * 13, static_cast<uint32_t>(i + 100))});
        }
        ASSERT_TRUE(io.flushAll());
        EXPECT_EQ(io.pendingSaves(), 0U);
        EXPECT_EQ(io.stats().savesWritten + io.stats().savesSuperseded, 128U);

        // Headers are on disk: a read-only store opened beside it sees every chunk.
        storage::RegionChunkStore reader(dir.path() / "region", true);
        for (int32_t i = 0; i < 64; ++i) {
            EXPECT_EQ(reader.read({i - 32, i % 5}),
                      noise(2000 + static_cast<size_t>(i) * 13, static_cast<uint32_t>(i + 100)))
                << i;
        }
    }
}

TEST(ChunkIoWorker, ShutdownJoinsWithoutLoss) {
    RecordingStore store;
    {
        ChunkIoWorker io(store);
        store.closeGate();
        for (int32_t i = 0; i < 200; ++i) {
            io.enqueueSave({{i, -i}, bytesOf(static_cast<uint8_t>(i))});
        }
        io.loadAsync({999, 999}); // Dropped at shutdown, never blocks it
        store.waitWritesStarted(1);
        store.openGate();
    } // Destructor: every queued save written, thread joined, store flushed
    EXPECT_EQ(store.size(), 200U);
    EXPECT_EQ(store.writes(), 200);
    EXPECT_GE(store.flushes(), 1);
    EXPECT_EQ(store.flushedWrites(), 200) << "the final flush comes after the last write";
    for (int32_t i = 0; i < 200; ++i) {
        EXPECT_EQ(store.read({i, -i}), bytesOf(static_cast<uint8_t>(i)));
    }
}

TEST(ChunkIoWorker, FailedWriteReportedAndFlushFails) {
    test::TempDir dir("betacraft_s7_io_ro_");
    storage::RegionChunkStore store(dir.path() / "region", true); // Read-only: writes fail
    ChunkIoWorker io(store);
    io.enqueueSave({{0, 0}, bytesOf(1)});
    EXPECT_FALSE(io.flushAll());
    const std::vector<SaveJob> failed = io.takeFailedSaves();
    ASSERT_EQ(failed.size(), 1U);
    EXPECT_EQ(failed[0].bundleBytes, bytesOf(1));
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "region"));
    EXPECT_TRUE(io.flushAll()) << "nothing failed since";
}

// Each answer carries the ticket of its loadAsync, so World can drop a late answer
// to a cancelled request of the same chunk.
TEST(ChunkIoWorker, LoadResultsEchoTheirTicket) {
    RecordingStore store;
    store.put({4, 4}, bytesOf(7));
    ChunkIoWorker io(store, ChunkIoConfig{.threaded = false});
    io.loadAsync({4, 4}, 11);
    io.loadAsync({4, 4}, 12);
    io.loadAsync({5, 5}); // default ticket
    ASSERT_TRUE(io.waitIdle());
    const std::vector<storage::LoadResult> loads = io.takeCompletedLoads();
    ASSERT_EQ(loads.size(), 3U);
    EXPECT_EQ(loads[0].ticket, 11U);
    EXPECT_EQ(loads[1].ticket, 12U);
    EXPECT_EQ(loads[2].ticket, 0U);
    EXPECT_EQ(loads[0].bundleBytes, bytesOf(7));
    EXPECT_FALSE(loads[2].bundleBytes.has_value());
}
