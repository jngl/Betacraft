#include "streaming/WorldMeshStreamer.hpp"

#include "voxel/Chunk.hpp"
#include "world/World.hpp"

#include <algorithm>

namespace betacraft::streaming {

namespace {

// Sky and plain air: no capture, no job. A palette can keep a single Air entry after edits,
// so this only skips the cheap common case; the snapshot's own check catches the rest.
[[nodiscard]] bool isTriviallyEmpty(const world::PalettedSection& section) noexcept {
    return section.isHomogeneous() && section.getBlock(0, 0, 0) == world::BlockId::Air;
}

} // namespace

WorldMeshStreamer::WorldMeshStreamer(WorldMeshStreamerConfig config) : m_config(std::move(config)) {
    rebuildPipeline();
}

WorldMeshStreamer::~WorldMeshStreamer() {
    shutdown();
}

void WorldMeshStreamer::rebuildPipeline() {
    if (m_pipeline) {
        m_pipeline->shutdown();
    }
    m_pipeline = std::make_unique<AsyncChunkPipeline>(m_config.pipeline);
    m_versions.clear();
    // Keep tracking the columns: one that unloads before the next update still owes the GPU
    // its deletions. The new pipeline only needs their lifecycle reported again.
    for (auto& [coord, tracked] : m_chunks) {
        tracked.stagesReported = false;
    }
}

void WorldMeshStreamer::update(world::World& world, const glm::vec3& viewerPos, const glm::vec3& viewerForward) {
    m_pipeline->setViewer(viewerPos, viewerForward);

    for (auto it = m_chunks.begin(); it != m_chunks.end();) {
        if (world.getChunk(it->first) == nullptr) {
            forgetChunk(it->first);
            it = m_chunks.erase(it);
        } else {
            ++it;
        }
    }

    // World generated (TerrainGen) and lit (Lit) the chunk inside ensureChunkLoaded; pass-2
    // decoration only runs once its 3x3 neighbourhood is resident, so frontier chunks wait at
    // Lit and are not meshed: no tree pop-in at the view edge.
    for (const auto& [coord, chunk] : world.getChunks()) {
        auto [it, inserted] = m_chunks.try_emplace(coord);
        TrackedChunk& tracked = it->second;
        const bool decorated = world.isChunkDecorated(coord);
        if (!inserted && tracked.decoratedReported && !decorated) {
            // Unloaded and reloaded since the last update: back to pass-1 terrain.
            forgetChunk(coord);
            tracked = TrackedChunk{};
        }
        if (!tracked.stagesReported) {
            (void)m_pipeline->advanceChunkStage(coord, ChunkStage::TerrainGen);
            (void)m_pipeline->advanceChunkStage(coord, ChunkStage::Lit);
            if (tracked.decoratedReported) {
                (void)m_pipeline->advanceChunkStage(coord, ChunkStage::Decorated); // Rebuilt pipeline
            }
            tracked.stagesReported = true;
        }
        if (!tracked.decoratedReported && decorated) {
            (void)m_pipeline->advanceChunkStage(coord, ChunkStage::Decorated);
            tracked.decoratedReported = true;
        }
    }

    if (m_remeshAll) {
        for (const auto& [coord, chunk] : world.getChunks()) {
            chunk->setMeshDirty(true);
        }
        m_remeshAll = false;
    }
    // Their queued job was dropped for a nearer one; nothing else would refresh them.
    for (const voxel::SectionCoord sc : m_pipeline->takeEvictedSections()) {
        if (world::Chunk* chunk = world.getChunkMutable(sc.chunk())) {
            chunk->markSectionDirty(static_cast<size_t>(sc.y));
        }
    }

    struct Candidate {
        voxel::SectionCoord coord;
        float distanceSq;
    };
    std::vector<Candidate> dirty;
    for (const auto& [coord, tracked] : m_chunks) {
        if (!tracked.decoratedReported) continue;
        const world::Chunk* chunk = world.getChunk(coord);
        if (chunk == nullptr || chunk->dirtySectionMask() == 0) continue;
        for (size_t s = 0; s < world::Chunk::SECTIONS_PER_CHUNK; ++s) {
            if (chunk->isSectionDirty(s)) {
                const voxel::SectionCoord sc{coord.x, static_cast<int32_t>(s), coord.z};
                dirty.push_back({sc, sc.distanceSqTo(viewerPos)});
            }
        }
    }
    std::sort(dirty.begin(), dirty.end(),
              [](const Candidate& a, const Candidate& b) { return a.distanceSq < b.distanceSq; });

    size_t submitted = 0;
    size_t processed = 0;
    for (const Candidate& c : dirty) {
        if (submitted >= m_config.maxRequestsPerUpdate) break;
        ++processed;
        world::Chunk* chunk = world.getChunkMutable(c.coord.chunk());
        const size_t s = static_cast<size_t>(c.coord.y);
        std::shared_ptr<const SectionNeighborhoodSnapshot18> snapshot;
        if (!isTriviallyEmpty(chunk->getSection(s))) {
            snapshot = SectionNeighborhoodSnapshot18::capture(world, c.coord);
        }
        if (!snapshot || snapshot->isInteriorEmpty()) {
            chunk->clearSectionDirty(s);
            dropSection(c.coord);
            continue;
        }
        const uint64_t version = ++m_versions[c.coord];
        const SubmitResult r = m_pipeline->requestSectionMesh(c.coord, std::move(snapshot), version);
        ++submitted;
        if (r == SubmitResult::RejectedQueueFull) {
            --processed; // farther sections would be refused too; they all stay dirty
            break;
        }
        chunk->clearSectionDirty(s);
        m_meshed.insert(c.coord);
    }
    m_deferredSections = dirty.size() - processed;
}

void WorldMeshStreamer::forgetChunk(core::ChunkCoord coord) {
    // Stops running jobs of the column and moves it back to Unloaded.
    m_pipeline->cancelChunk(coord);
    for (int32_t y = 0; y < voxel::SectionCoord::SECTIONS_PER_COLUMN; ++y) {
        const voxel::SectionCoord sc{coord.x, y, coord.z};
        m_versions.erase(sc);
        if (m_meshed.erase(sc) != 0) {
            m_pendingRemovals.push_back(sc);
        }
    }
}

void WorldMeshStreamer::dropSection(voxel::SectionCoord coord) {
    if (m_meshed.erase(coord) == 0) {
        return;
    }
    // Anything still queued, running or staged for it describes geometry that is gone.
    m_pipeline->cancelSection(coord);
    m_pendingRemovals.push_back(coord);
}

DrainStats WorldMeshStreamer::drainUploads(std::chrono::nanoseconds budget, const MeshUploader& uploader) {
    // Deletions first: a mesh staged after them belongs to a later submission (the section
    // refilled or the chunk came back), so it must win.
    std::vector<voxel::SectionCoord> removals;
    removals.swap(m_pendingRemovals);
    for (const voxel::SectionCoord coord : removals) {
        ChunkMeshResult empty{coord, 0, {}};
        uploader(empty);
    }
    return m_pipeline->drainUploads(budget, uploader);
}

void WorldMeshStreamer::setSmoothLighting(bool enabled) {
    if (enabled == m_config.pipeline.meshOptions.smoothLighting) {
        return;
    }
    m_config.pipeline.meshOptions.smoothLighting = enabled;
    rebuildPipeline();
    m_remeshAll = true;
}

void WorldMeshStreamer::shutdown() {
    if (m_pipeline) {
        m_pipeline->shutdown();
    }
}

bool WorldMeshStreamer::hasPendingWork() const {
    return m_remeshAll || m_deferredSections > 0 || !m_pendingRemovals.empty() ||
           m_pipeline->pendingEvictionReports() > 0 || m_pipeline->pendingJobs() > 0 ||
           m_pipeline->inFlightJobs() > 0 || m_pipeline->stagedResults() > 0;
}

} // namespace betacraft::streaming
