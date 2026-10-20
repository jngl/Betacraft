#pragma once

// Test doubles shared by the AsyncChunkPipeline test files: snapshot factories, meshers
// that record or block deterministically (no sleeps), and config/drain helpers.

#include "streaming/AsyncChunkPipeline.hpp"

#include "StreamingTestHelpers.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <vector>

namespace betacraft::streaming::test {

using Snap = SectionNeighborhoodSnapshot18;
using SnapPtr = std::shared_ptr<const Snap>;

inline SnapPtr stoneCube() {
    return Snap::captureFrom([](int32_t x, int32_t y, int32_t z) -> std::optional<VoxelSample> {
        const bool on = x == 4 && y == 4 && z == 4;
        return VoxelSample{static_cast<world::BlockStateId>(on ? world::BlockId::Stone : world::BlockId::Air), 15, 0};
    });
}

inline SnapPtr emptySnap() { return std::make_shared<const Snap>(); }

// Records the order sections were meshed in and returns a tiny marker mesh whose single
// vertex encodes the section coordinate (so uploads are traceable).
struct RecordingMesher {
    std::mutex mutex;
    std::vector<voxel::SectionCoord> order;
    std::atomic<int> calls{0};

    MeshFunction fn() {
        return [this](const Snap&, voxel::SectionCoord c, const MeshOptions&, std::stop_token) -> std::optional<RawMeshBuffer> {
            {
                const std::lock_guard lock(mutex);
                order.push_back(c);
            }
            calls.fetch_add(1);
            RawMeshBuffer m;
            m.opaqueVertices.push_back({static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.z)});
            return m;
        };
    }
    std::vector<voxel::SectionCoord> snapshotOrder() {
        const std::lock_guard lock(mutex);
        return order;
    }
};

// Blocks every call until released (or until the job's stop_token fires), letting tests
// hold a job "in flight" deterministically without sleeps. With ignoreStop the gate only
// opens on release(): models a mesher between two cancellation checks, so a stopped
// (stale) job is still in flight.
struct GateMesher {
    bool ignoreStop{false};
    std::promise<void> startedPromise;
    std::shared_future<void> started{startedPromise.get_future().share()};
    std::atomic<bool> startedOnce{false};
    std::mutex mutex;
    std::condition_variable_any cv;
    bool released{false};
    std::atomic<int> calls{0};
    std::atomic<int> sawStop{0};

    MeshFunction fn() {
        return [this](const Snap&, voxel::SectionCoord, const MeshOptions&, std::stop_token st) -> std::optional<RawMeshBuffer> {
            calls.fetch_add(1);
            if (!startedOnce.exchange(true)) startedPromise.set_value();
            std::unique_lock lock(mutex);
            if (ignoreStop) {
                cv.wait(lock, [this] { return released; });
            } else {
                cv.wait(lock, st, [this] { return released; });
            }
            if (st.stop_requested()) {
                sawStop.fetch_add(1);
                return std::nullopt;
            }
            return RawMeshBuffer{};
        };
    }
    void release() {
        {
            const std::lock_guard lock(mutex);
            released = true;
        }
        cv.notify_all();
    }
};

// Scheduler-focused configs: the chunk lifecycle gate is disabled so tests can submit
// sections of chunks that were never reported as generated.
inline PipelineConfig inlineConfig(MeshFunction mesher = {}) {
    PipelineConfig cfg;
    cfg.workerCount = 0; // deterministic: jobs only run inside runPendingInline()
    cfg.enforceChunkLifecycle = false;
    if (mesher) cfg.mesher = std::move(mesher);
    return cfg;
}

inline PipelineConfig threadedConfig(size_t workers, MeshFunction mesher = {}) {
    PipelineConfig cfg;
    cfg.workerCount = workers;
    cfg.enforceChunkLifecycle = false;
    if (mesher) cfg.mesher = std::move(mesher);
    return cfg;
}

inline std::vector<ChunkMeshResult> drainAll(AsyncChunkPipeline& p) {
    using namespace std::chrono_literals;
    std::vector<ChunkMeshResult> out;
    while (p.drainUploads(1h, [&](ChunkMeshResult& r) { out.push_back(std::move(r)); }).uploaded > 0) {
    }
    return out;
}

} // namespace betacraft::streaming::test
