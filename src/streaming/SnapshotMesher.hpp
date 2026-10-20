#pragma once

#include "streaming/PackedChunkVertex.hpp"
#include "streaming/SectionNeighborhoodSnapshot18.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <vector>

namespace betacraft::streaming {

// Raw CPU-side geometry for one section, ready for a direct glBufferData upload.
struct RawMeshBuffer {
    std::vector<PackedChunkVertex> opaqueVertices;
    std::vector<PackedChunkVertex> translucentVertices;
    uint32_t opaqueQuadCount{0};
    uint32_t translucentQuadCount{0};

    [[nodiscard]] bool empty() const noexcept { return opaqueVertices.empty() && translucentVertices.empty(); }
};

struct MeshOptions {
    bool smoothLighting{true};
};

// Called on the meshing thread after each completed 16x16 layer with the number of layers
// done (1..16). For profiling/telemetry and for tests that need a deterministic point
// inside a running build; must not block.
using MeshProgressFn = std::function<void(int32_t layersDone)>;

class SnapshotMesher {
public:
    // Returns std::nullopt when `stopToken` is (or becomes) stop-requested: the token is
    // polled before the first and before every following layer (cancellation latency
    // bounded by ~1/16 of a build).
    [[nodiscard]] static std::optional<RawMeshBuffer> build(
        const SectionNeighborhoodSnapshot18& snapshot, int32_t sectionY, const MeshOptions& options,
        std::stop_token stopToken = {}, const MeshProgressFn& onLayerDone = {});
};

} // namespace betacraft::streaming
