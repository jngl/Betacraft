#pragma once

#include "core/Types.hpp"
#include "streaming/AsyncChunkPipeline.hpp"
#include "voxel/SectionCoord.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

namespace betacraft::world {
class World;
}

namespace betacraft::streaming {

struct WorldMeshStreamerConfig {
    PipelineConfig pipeline{};
    // Snapshot captures run on the main thread; this caps a frame's share when a burst of
    // chunks loads at once. The rest stays dirty for the next update.
    size_t maxRequestsPerUpdate{512};
};

// Main-thread glue between the live World and the AsyncChunkPipeline (roadmap P1.1):
// reports each resident chunk's lifecycle (TerrainGen and Lit on arrival, Decorated once
// World's pass 2 ran), captures and submits every dirty section of decorated chunks,
// re-requests what the bounded queue evicted or refused, and cancels unloaded columns.
// The World only marks sections dirty; this class is the one place that turns those bits
// into meshing jobs.
//
// Uploader contract (drainUploads): a ChunkMeshResult with empty buffers means "this
// section has no geometry any more, delete its GPU mesh"; it is sent for sections that
// became empty and for every meshed section of an unloaded chunk.
class WorldMeshStreamer {
public:
    explicit WorldMeshStreamer(WorldMeshStreamerConfig config = {});
    ~WorldMeshStreamer();

    WorldMeshStreamer(const WorldMeshStreamer&) = delete;
    WorldMeshStreamer& operator=(const WorldMeshStreamer&) = delete;
    WorldMeshStreamer(WorldMeshStreamer&&) = delete;
    WorldMeshStreamer& operator=(WorldMeshStreamer&&) = delete;

    // Once per frame on the thread that owns the World, after chunk streaming and edits.
    // Clears the dirty bits of the sections it submitted.
    void update(world::World& world, const glm::vec3& viewerPos, const glm::vec3& viewerForward);

    // Render thread (owner of the GL context): pending deletions first, then staged meshes
    // within `budget` (at least one per call).
    DrainStats drainUploads(std::chrono::nanoseconds budget, const MeshUploader& uploader);

    // Mesh options are fixed per pipeline, so a change rebuilds it and re-meshes every
    // decorated chunk on the next update. GPU meshes stay until their replacement arrives.
    void setSmoothLighting(bool enabled);
    [[nodiscard]] bool smoothLighting() const noexcept { return m_config.pipeline.meshOptions.smoothLighting; }

    // Stops and joins the workers. Must run before the World and the GL context go away;
    // the destructor does it too.
    void shutdown();

    // Test / headless hooks.
    size_t runPendingInline(size_t maxJobs) { return m_pipeline->runPendingInline(maxJobs); }
    [[nodiscard]] bool waitIdle(std::chrono::nanoseconds timeout) { return m_pipeline->waitIdle(timeout); }
    // Anything left to capture, mesh, upload or delete (render thread: reads the staging).
    [[nodiscard]] bool hasPendingWork() const;

    [[nodiscard]] const AsyncChunkPipeline& pipeline() const noexcept { return *m_pipeline; }
    [[nodiscard]] AsyncChunkPipeline& pipeline() noexcept { return *m_pipeline; }
    [[nodiscard]] size_t trackedChunks() const noexcept { return m_chunks.size(); }

private:
    struct TrackedChunk {
        bool stagesReported{false}; // TerrainGen + Lit (and Decorated if reported) told to m_pipeline
        bool decoratedReported{false};
    };

    void rebuildPipeline();
    // The section has no geometry any more: forget it and schedule its GPU deletion.
    void dropSection(voxel::SectionCoord coord);
    // The chunk left the World: cancel its column and delete whatever it had on the GPU.
    void forgetChunk(core::ChunkCoord coord);

    WorldMeshStreamerConfig m_config;
    std::unique_ptr<AsyncChunkPipeline> m_pipeline;
    std::unordered_map<core::ChunkCoord, TrackedChunk> m_chunks;
    std::unordered_map<voxel::SectionCoord, uint64_t> m_versions;
    // Sections that may own a GPU mesh: they need an explicit deletion when they empty
    // out or their chunk unloads.
    std::unordered_set<voxel::SectionCoord> m_meshed;
    std::vector<voxel::SectionCoord> m_pendingRemovals;
    size_t m_deferredSections{0}; // dirty sections the last update left for later
    bool m_remeshAll{false};
};

} // namespace betacraft::streaming
