#include "render/ChunkMesher.hpp"
#include "render/TextureAtlas.hpp"
#include "streaming/BlockShapeMesher.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace betacraft::render {

using streaming::PackedChunkVertex;

namespace {

GLuint s_sharedEBO = 0;

void setupChunkVao(GLuint& vao, GLuint& vbo, const std::vector<PackedChunkVertex>& verts) {
    if (verts.empty()) {
        return;
    }
    if (vao == 0) {
        if (bc_glGenVertexArrays) bc_glGenVertexArrays(1, &vao);
        if (bc_glGenBuffers) bc_glGenBuffers(1, &vbo);
    }
    if (vao == 0 || vbo == 0) return;

    bc_glBindVertexArray(vao);
    bc_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    bc_glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(verts.size() * sizeof(PackedChunkVertex)),
        verts.data(),
        GL_STATIC_DRAW
    );

    // Attribute 0: uvec2 (two 32-bit unsigned integers: word0, word1)
    bc_glEnableVertexAttribArray(0);
    if (bc_glVertexAttribIPointer) {
        bc_glVertexAttribIPointer(
            0,
            2,
            GL_UNSIGNED_INT,
            sizeof(PackedChunkVertex),
            reinterpret_cast<const void*>(0)
        );
    }

    // Bind global static shared EBO containing pattern [0, 1, 2, 0, 2, 3]
    const GLuint sharedEbo = ChunkMeshGL::getSharedEBO();
    if (sharedEbo != 0) {
        bc_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sharedEbo);
    }

    bc_glBindVertexArray(0);
}

constexpr uint32_t faceDirToNormalIdx(core::FaceDir dir) noexcept {
    switch (dir) {
        case core::FaceDir::PosY: return 0; // +Y
        case core::FaceDir::NegY: return 1; // -Y
        case core::FaceDir::PosZ: return 2; // +Z
        case core::FaceDir::NegZ: return 3; // -Z
        case core::FaceDir::PosX: return 4; // +X
        case core::FaceDir::NegX: return 5; // -X
    }
    return 0;
}

bool shouldRenderBlockFace(
    voxel::BlockId selfId,
    const voxel::BlockDef& selfDef,
    voxel::BlockId neighborId,
    const voxel::BlockDef& neighborDef
) noexcept {
    if (neighborId == voxel::BlockId::Air) {
        return true;
    }
    if (voxel::BlockRegistry::isLiquid(selfId)) {
        return !voxel::isSameLiquid(selfId, neighborId);
    }
    if (selfDef.layer == voxel::RenderLayer::Translucent && neighborId == selfId) {
        return false;
    }
    if (neighborDef.opaque && neighborDef.shape == voxel::RenderShape::Cube) {
        return false;
    }
    return true;
}

struct VertexLightSample {
    uint32_t aoIdx{3}; // 3 = 1.00 (unoccluded)
    float ao{1.0f};
    uint32_t skyLight{15};
    uint32_t blockLight{0};
};

VertexLightSample sampleVertexAOAndLight(
    const world::World& world,
    const glm::ivec3& baseNeighbor,
    const glm::ivec3& tangent1,
    const glm::ivec3& tangent2,
    bool smoothLighting
) noexcept {
    const auto sampleSky = [&](const glm::ivec3& p) -> uint32_t {
        return static_cast<uint32_t>(world.getSkyLight(p.x, p.y, p.z));
    };
    const auto sampleBlk = [&](const glm::ivec3& p) -> uint32_t {
        return static_cast<uint32_t>(world.getBlockLight(p.x, p.y, p.z));
    };

    if (!smoothLighting) {
        return VertexLightSample{
            .aoIdx = 3,
            .ao = 1.0f,
            .skyLight = sampleSky(baseNeighbor),
            .blockLight = sampleBlk(baseNeighbor)
        };
    }

    const glm::ivec3 s1Pos = baseNeighbor + tangent1;
    const glm::ivec3 s2Pos = baseNeighbor + tangent2;
    const glm::ivec3 cornerPos = baseNeighbor + tangent1 + tangent2;

    const bool s1Opaque = voxel::BlockRegistry::isOpaque(world.getBlock(s1Pos.x, s1Pos.y, s1Pos.z));
    const bool s2Opaque = voxel::BlockRegistry::isOpaque(world.getBlock(s2Pos.x, s2Pos.y, s2Pos.z));
    const bool cOpaque  = voxel::BlockRegistry::isOpaque(world.getBlock(cornerPos.x, cornerPos.y, cornerPos.z));

    int occluded = 0;
    if (s1Opaque && s2Opaque) {
        occluded = 3;
    } else {
        occluded = (s1Opaque ? 1 : 0) + (s2Opaque ? 1 : 0) + (cOpaque ? 1 : 0);
    }

    static constexpr float aoTable[4] = {0.50f, 0.66f, 0.82f, 1.00f};
    const uint32_t aoIdx = static_cast<uint32_t>(3 - std::clamp(occluded, 0, 3));
    const float ao = aoTable[aoIdx];

    uint32_t sumSky = sampleSky(baseNeighbor);
    uint32_t sumBlk = sampleBlk(baseNeighbor);
    uint32_t count = 1;

    if (!s1Opaque) {
        sumSky += sampleSky(s1Pos);
        sumBlk += sampleBlk(s1Pos);
        ++count;
    }
    if (!s2Opaque) {
        sumSky += sampleSky(s2Pos);
        sumBlk += sampleBlk(s2Pos);
        ++count;
    }
    if (!(s1Opaque && s2Opaque) && !cOpaque) {
        sumSky += sampleSky(cornerPos);
        sumBlk += sampleBlk(cornerPos);
        ++count;
    }

    return VertexLightSample{
        .aoIdx = aoIdx,
        .ao = ao,
        .skyLight = (sumSky + count / 2) / count,
        .blockLight = (sumBlk + count / 2) / count
    };
}

void appendPackedQuad(
    std::vector<PackedChunkVertex>& out,
    const std::array<PackedChunkVertex, 4>& v,
    const std::array<float, 4>& ao
) {
    // Exactly 4 sommets per quad (32 bytes instead of 240 bytes)
    // Flip diagonal by rotation if necessary to prevent AO anisotropy
    if (ao[0] + ao[2] > ao[1] + ao[3]) {
        out.push_back(v[0]);
        out.push_back(v[1]);
        out.push_back(v[2]);
        out.push_back(v[3]);
    } else {
        out.push_back(v[1]);
        out.push_back(v[2]);
        out.push_back(v[3]);
        out.push_back(v[0]);
    }
}

} // namespace

GLuint ChunkMeshGL::getSharedEBO() noexcept {
    if (s_sharedEBO == 0) {
        initSharedEBO();
    }
    return s_sharedEBO;
}

void ChunkMeshGL::initSharedEBO(size_t maxQuads) noexcept {
    if (s_sharedEBO != 0 || !bc_glGenBuffers) {
        return;
    }
    bc_glGenBuffers(1, &s_sharedEBO);
    bc_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_sharedEBO);

    const auto eboData = streaming::generateSharedEboPattern(maxQuads);
    bc_glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(eboData.size() * sizeof(uint32_t)),
        eboData.data(),
        GL_STATIC_DRAW
    );
    bc_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void ChunkMeshGL::destroySharedEBO() noexcept {
    if (s_sharedEBO != 0 && bc_glDeleteBuffers) {
        bc_glDeleteBuffers(1, &s_sharedEBO);
        s_sharedEBO = 0;
    }
}

ChunkMeshGL::~ChunkMeshGL() noexcept {
    destroy();
}

ChunkMeshGL::ChunkMeshGL(ChunkMeshGL&& other) noexcept
    : m_opaqueVao(other.m_opaqueVao),
      m_opaqueVbo(other.m_opaqueVbo),
      m_opaqueCount(other.m_opaqueCount),
      m_translucentVao(other.m_translucentVao),
      m_translucentVbo(other.m_translucentVbo),
      m_translucentCount(other.m_translucentCount) {
    other.m_opaqueVao = 0;
    other.m_opaqueVbo = 0;
    other.m_opaqueCount = 0;
    other.m_translucentVao = 0;
    other.m_translucentVbo = 0;
    other.m_translucentCount = 0;
}

ChunkMeshGL& ChunkMeshGL::operator=(ChunkMeshGL&& other) noexcept {
    if (this != &other) {
        destroy();
        m_opaqueVao = other.m_opaqueVao;
        m_opaqueVbo = other.m_opaqueVbo;
        m_opaqueCount = other.m_opaqueCount;
        m_translucentVao = other.m_translucentVao;
        m_translucentVbo = other.m_translucentVbo;
        m_translucentCount = other.m_translucentCount;

        other.m_opaqueVao = 0;
        other.m_opaqueVbo = 0;
        other.m_opaqueCount = 0;
        other.m_translucentVao = 0;
        other.m_translucentVbo = 0;
        other.m_translucentCount = 0;
    }
    return *this;
}

void ChunkMeshGL::destroy() noexcept {
    if (m_opaqueVao != 0 && bc_glDeleteVertexArrays) {
        bc_glDeleteVertexArrays(1, &m_opaqueVao);
        bc_glDeleteBuffers(1, &m_opaqueVbo);
        m_opaqueVao = 0;
        m_opaqueVbo = 0;
    }
    if (m_translucentVao != 0 && bc_glDeleteVertexArrays) {
        bc_glDeleteVertexArrays(1, &m_translucentVao);
        bc_glDeleteBuffers(1, &m_translucentVbo);
        m_translucentVao = 0;
        m_translucentVbo = 0;
    }
    m_opaqueCount = 0;
    m_translucentCount = 0;
}

void ChunkMeshGL::upload(const ChunkMeshData& data) {
    upload(data.opaqueVertices, data.translucentVertices);
}

void ChunkMeshGL::upload(const std::vector<PackedChunkVertex>& opaque, const std::vector<PackedChunkVertex>& translucent) {
    m_opaqueCount = opaque.size();
    m_translucentCount = translucent.size();
    setupChunkVao(m_opaqueVao, m_opaqueVbo, opaque);
    setupChunkVao(m_translucentVao, m_translucentVbo, translucent);
}

void ChunkMeshGL::drawOpaque() const noexcept {
    if (m_opaqueCount == 0 || m_opaqueVao == 0) return;
    bc_glBindVertexArray(m_opaqueVao);
    const GLsizei indexCount = static_cast<GLsizei>((m_opaqueCount / 4) * 6);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    bc_glBindVertexArray(0);
}

void ChunkMeshGL::drawTranslucent() const noexcept {
    if (m_translucentCount == 0 || m_translucentVao == 0) return;
    bc_glBindVertexArray(m_translucentVao);
    const GLsizei indexCount = static_cast<GLsizei>((m_translucentCount / 4) * 6);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    bc_glBindVertexArray(0);
}

ChunkMeshData ChunkMesher::buildMesh(
    const world::World& world,
    const voxel::Chunk& chunk,
    bool smoothLighting
) {
    ChunkMeshData meshData;
    meshData.opaqueVertices.reserve(16384);
    meshData.translucentVertices.reserve(2048);

    const int32_t baseX = chunk.getCoord().x * voxel::Chunk::WIDTH;
    const int32_t baseZ = chunk.getCoord().z * voxel::Chunk::DEPTH;

    for (int32_t y = 0; y < voxel::Chunk::HEIGHT; ++y) {
        const uint32_t secY = static_cast<uint32_t>(y / 16);
        const uint32_t localY = static_cast<uint32_t>(y % 16);

        for (int32_t lz = 0; lz < voxel::Chunk::DEPTH; ++lz) {
            for (int32_t lx = 0; lx < voxel::Chunk::WIDTH; ++lx) {
                const voxel::BlockId id = chunk.getBlock(lx, y, lz);
                if (id == voxel::BlockId::Air) {
                    continue;
                }

                const voxel::BlockDef& def = voxel::BlockRegistry::get(id);
                const int32_t wx = baseX + lx;
                const int32_t wz = baseZ + lz;

                std::vector<PackedChunkVertex>& targetBuffer =
                    (def.layer == voxel::RenderLayer::Translucent)
                        ? meshData.translucentVertices
                        : meshData.opaqueVertices;

                const uint32_t skyLight = static_cast<uint32_t>(chunk.getSkyLight(lx, y, lz));
                const uint32_t blkLight = (def.lightEmission > 0)
                    ? static_cast<uint32_t>(def.lightEmission)
                    : static_cast<uint32_t>(chunk.getBlockLight(lx, y, lz));

                const uint32_t biomeTintVal = def.tintGrassTop ? 128u : (def.tintFoliage ? 160u : 255u);

                // Special geometry: Cross-billboard Flora (TallGrass, Flowers, Sapling, SugarCane)
                if (def.shape == voxel::RenderShape::Cross) {
                    const uint32_t matTile = def.tileForFace(core::FaceDir::PosZ);
                    const uint32_t normalIdx = 6; // Cross-billboard Flora (explicit UV mapping)
                    const uint32_t w1 = PackedChunkVertex::packWord1(
                        skyLight, blkLight, biomeTintVal, 0, def.tintFoliage, false, secY
                    );

                    // 2 intersecting vertical quads (each double-sided = 4 quads = 16 vertices)
                    auto addDoubleSidedQuad = [&](
                        uint32_t x0, uint32_t y0, uint32_t z0,
                        uint32_t x1, uint32_t y1, uint32_t z1
                    ) {
                        const std::array<PackedChunkVertex, 4> front{{
                            {PackedChunkVertex::packWord0(x0, y0,     z0, normalIdx, 3, 3, matTile), w1},
                            {PackedChunkVertex::packWord0(x1, y0,     z1, normalIdx, 3, 2, matTile), w1},
                            {PackedChunkVertex::packWord0(x1, y1 + 1, z1, normalIdx, 3, 1, matTile), w1},
                            {PackedChunkVertex::packWord0(x0, y1 + 1, z0, normalIdx, 3, 0, matTile), w1}
                        }};
                        const std::array<PackedChunkVertex, 4> back{{
                            {PackedChunkVertex::packWord0(x1, y0,     z1, normalIdx, 3, 3, matTile), w1},
                            {PackedChunkVertex::packWord0(x0, y0,     z0, normalIdx, 3, 2, matTile), w1},
                            {PackedChunkVertex::packWord0(x0, y1 + 1, z0, normalIdx, 3, 1, matTile), w1},
                            {PackedChunkVertex::packWord0(x1, y1 + 1, z1, normalIdx, 3, 0, matTile), w1}
                        }};
                        appendPackedQuad(targetBuffer, front, {1.0f, 1.0f, 1.0f, 1.0f});
                        appendPackedQuad(targetBuffer, back,  {1.0f, 1.0f, 1.0f, 1.0f});
                    };

                    addDoubleSidedQuad(lx, localY, lz, lx + 1, localY, lz + 1);
                    addDoubleSidedQuad(lx, localY, lz + 1, lx + 1, localY, lz);
                    continue;
                }

                // Special geometry: Torch (5 quads = 20 sommets)
                if (def.shape == voxel::RenderShape::Torch) {
                    const uint32_t tile = def.tileForFace(core::FaceDir::PosZ);
                    const uint32_t w1 = PackedChunkVertex::packWord1(skyLight, blkLight, 255, 0, false, false, secY);

                    auto emitTorchQuad = [&](
                        uint32_t x0, uint32_t y0, uint32_t z0,
                        uint32_t x1, uint32_t y1, uint32_t z1,
                        uint32_t norm
                    ) {
                        const std::array<PackedChunkVertex, 4> q{{
                            {PackedChunkVertex::packWord0(x0, y0, z0, norm, 3, 0, tile), w1},
                            {PackedChunkVertex::packWord0(x1, y0, z1, norm, 3, 1, tile), w1},
                            {PackedChunkVertex::packWord0(x1, y1, z1, norm, 3, 2, tile), w1},
                            {PackedChunkVertex::packWord0(x0, y1, z0, norm, 3, 3, tile), w1}
                        }};
                        appendPackedQuad(targetBuffer, q, {1.0f, 1.0f, 1.0f, 1.0f});
                    };

                    // 4 vertical sides + 1 top face
                    emitTorchQuad(lx, localY, lz + 1, lx + 1, localY, lz + 1, 2);
                    emitTorchQuad(lx + 1, localY, lz, lx, localY, lz, 3);
                    emitTorchQuad(lx + 1, localY, lz + 1, lx + 1, localY, lz, 4);
                    emitTorchQuad(lx, localY, lz, lx, localY, lz + 1, 5);
                    emitTorchQuad(lx, localY + 1, lz + 1, lx + 1, localY + 1, lz + 1, 0);
                    continue;
                }

                // S5.4 ladders, doors, fences, stairs...: boxes inside the cell (BlockShapeMesher)
                if (voxel::isDetailedShape(def.shape)) {
                    const auto sample = [&](int32_t dx, int32_t dy, int32_t dz) {
                        const int32_t sx = wx + dx;
                        const int32_t sy = y + dy;
                        const int32_t sz = wz + dz;
                        return streaming::ShapeVoxel{world.getBlock(sx, sy, sz), world.getMetadata(sx, sy, sz),
                                                     world.getSkyLight(sx, sy, sz), world.getBlockLight(sx, sy, sz)};
                    };
                    streaming::emitDetailedShape(targetBuffer, def, chunk.getMetadata(lx, y, lz),
                                                 static_cast<uint32_t>(lx), localY, static_cast<uint32_t>(lz), secY,
                                                 sample);
                    continue;
                }

                // Cube / Slab / Cactus / Liquid faces
                const bool isSlab = (def.shape == voxel::RenderShape::Slab);
                const bool isTranslucent = (def.layer == voxel::RenderLayer::Translucent);

                for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
                    const auto dir = static_cast<core::FaceDir>(faceIdx);
                    const glm::ivec3 n = core::faceNormal(dir);
                    const glm::ivec3 nbPos(wx + n.x, y + n.y, wz + n.z);

                    const voxel::BlockId nbId = world.getBlock(nbPos.x, nbPos.y, nbPos.z);
                    const voxel::BlockDef& nbDef = voxel::BlockRegistry::get(nbId);

                    if (def.shape != voxel::RenderShape::Cactus &&
                        !(isSlab && dir == core::FaceDir::PosY) &&
                        !shouldRenderBlockFace(id, def, nbId, nbDef)) {
                        continue;
                    }

                    const uint32_t tile = def.tileForFace(dir);
                    const uint32_t normIdx = faceDirToNormalIdx(dir);

                    // 4 corner coordinate offsets
                    // (x, y, z) offsets from block min (lx, localY, lz)
                    std::array<glm::uvec3, 4> corners{};
                    std::array<glm::ivec3, 4> t1{};
                    std::array<glm::ivec3, 4> t2{};

                    switch (dir) {
                        case core::FaceDir::PosX: // East (+X)
                            corners = {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}};
                            t1 = {{{0, -1, 0}, {0, -1, 0}, {0, 1, 0}, {0, 1, 0}}};
                            t2 = {{{0, 0, 1},  {0, 0, -1}, {0, 0, -1}, {0, 0, 1}}};
                            break;

                        case core::FaceDir::NegX: // West (-X)
                            corners = {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}};
                            t1 = {{{0, -1, 0}, {0, -1, 0}, {0, 1, 0}, {0, 1, 0}}};
                            t2 = {{{0, 0, -1}, {0, 0, 1},  {0, 0, 1}, {0, 0, -1}}};
                            break;

                        case core::FaceDir::PosY: // Top (+Y)
                            corners = {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}};
                            t1 = {{{-1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}}};
                            t2 = {{{0, 0, 1},  {0, 0, 1}, {0, 0, -1}, {0, 0, -1}}};
                            break;

                        case core::FaceDir::NegY: // Bottom (-Y)
                            corners = {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}};
                            t1 = {{{-1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}}};
                            t2 = {{{0, 0, -1}, {0, 0, -1}, {0, 0, 1}, {0, 0, 1}}};
                            break;

                        case core::FaceDir::PosZ: // South (+Z)
                            corners = {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};
                            t1 = {{{-1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}}};
                            t2 = {{{0, -1, 0}, {0, -1, 0}, {0, 1, 0}, {0, 1, 0}}};
                            break;

                        case core::FaceDir::NegZ: // North (-Z)
                            corners = {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}};
                            t1 = {{{1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {1, 0, 0}}};
                            t2 = {{{0, -1, 0}, {0, -1, 0}, {0, 1, 0}, {0, 1, 0}}};
                            break;
                    }

                    std::array<PackedChunkVertex, 4> quadVerts{};
                    std::array<float, 4> aoValues{};

                    for (size_t k = 0; k < 4; ++k) {
                        const VertexLightSample sample = sampleVertexAOAndLight(
                            world, nbPos, t1[k], t2[k], smoothLighting
                        );
                        aoValues[k] = sample.ao;

                        const uint32_t vx = static_cast<uint32_t>(lx) + corners[k].x;
                        const uint32_t vy = localY + corners[k].y;
                        const uint32_t vz = static_cast<uint32_t>(lz) + corners[k].z;

                        const uint32_t vSky = sample.skyLight;
                        const uint32_t vBlk = (def.lightEmission > 0) ? def.lightEmission : sample.blockLight;

                        const uint32_t w0 = PackedChunkVertex::packWord0(
                            vx, vy, vz, normIdx, sample.aoIdx, static_cast<uint32_t>(k), tile
                        );
                        const uint32_t w1 = PackedChunkVertex::packWord1(
                            vSky, vBlk, biomeTintVal, 0, def.tintFoliage, isTranslucent, secY
                        );

                        quadVerts[k] = PackedChunkVertex{w0, w1};
                    }

                    appendPackedQuad(targetBuffer, quadVerts, aoValues);
                }
            }
        }
    }

    return meshData;
}

} // namespace betacraft::render
