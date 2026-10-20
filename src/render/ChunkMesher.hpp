#pragma once

#include "core/Types.hpp"
#include "render/GLHeaders.hpp"
#include "render/PackedChunkVertex.hpp"
#include "voxel/Chunk.hpp"
#include "world/World.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace betacraft::render {

// Legacy format retained for dynamic overlays (particles, hand, crack)
struct ChunkVertex {
    glm::vec3 position;
    glm::vec2 uv;
    glm::vec3 color;     // RGB tint * directional shade * ambient occlusion
    glm::vec2 light;     // x = skyLight (0..1), y = blockLight (0..1)
};

struct ChunkMeshData {
    std::vector<PackedChunkVertex> opaqueVertices;
    std::vector<PackedChunkVertex> translucentVertices;
};

class ChunkMeshGL : private core::NonCopyable {
public:
    ChunkMeshGL() noexcept = default;
    ~ChunkMeshGL() noexcept;

    ChunkMeshGL(ChunkMeshGL&& other) noexcept;
    ChunkMeshGL& operator=(ChunkMeshGL&& other) noexcept;

    void upload(const ChunkMeshData& data);
    void upload(const std::vector<PackedChunkVertex>& opaque, const std::vector<PackedChunkVertex>& translucent);
    void drawOpaque() const noexcept;
    void drawTranslucent() const noexcept;

    [[nodiscard]] size_t getOpaqueVertexCount() const noexcept { return m_opaqueCount; }
    [[nodiscard]] size_t getTranslucentVertexCount() const noexcept { return m_translucentCount; }

    static GLuint getSharedEBO() noexcept;
    static void initSharedEBO(size_t maxQuads = 65536) noexcept;
    static void destroySharedEBO() noexcept;

private:
    void destroy() noexcept;

    GLuint m_opaqueVao{0};
    GLuint m_opaqueVbo{0};
    size_t m_opaqueCount{0};

    GLuint m_translucentVao{0};
    GLuint m_translucentVbo{0};
    size_t m_translucentCount{0};
};

class ChunkMesher {
public:
    [[nodiscard]] static ChunkMeshData buildMesh(
        const world::World& world,
        const voxel::Chunk& chunk,
        bool smoothLighting = true
    );
};

} // namespace betacraft::render
