#include "streaming/SnapshotMesher.hpp"

#include "streaming/BlockShapeMesher.hpp"

#include <algorithm>
#include <array>

// Port of render::ChunkMesher::buildMesh onto SectionNeighborhoodSnapshot18 reads.
// The legacy mesher samples the live World for neighbours (racy off the main thread);
// this one only reads the immutable snapshot, and must stay bit-identical to it so that
// switching the renderer to the async pipeline is invisible (enforced by parity tests).
// Every neighbour/AO lookup is within +-1 voxel per axis of an interior block, which is
// exactly what the 18^3 guard border covers.

namespace betacraft::streaming {

namespace {

using Snap = SectionNeighborhoodSnapshot18;

constexpr uint32_t faceDirToNormalIdx(core::FaceDir dir) noexcept {
    switch (dir) {
        case core::FaceDir::PosY: return 0;
        case core::FaceDir::NegY: return 1;
        case core::FaceDir::PosZ: return 2;
        case core::FaceDir::NegZ: return 3;
        case core::FaceDir::PosX: return 4;
        case core::FaceDir::NegX: return 5;
    }
    return 0;
}

bool shouldRenderBlockFace(voxel::BlockId selfId, const voxel::BlockDef& selfDef, voxel::BlockId neighborId,
                           const voxel::BlockDef& neighborDef) noexcept {
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
    uint32_t aoIdx{3};
    float ao{1.0f};
    uint32_t skyLight{15};
    uint32_t blockLight{0};
};

VertexLightSample sampleVertexAOAndLight(const Snap& snap, const glm::ivec3& baseNeighbor, const glm::ivec3& tangent1,
                                         const glm::ivec3& tangent2, bool smoothLighting) noexcept {
    const auto sampleSky = [&](const glm::ivec3& p) -> uint32_t { return snap.getSkyLight(p.x, p.y, p.z); };
    const auto sampleBlk = [&](const glm::ivec3& p) -> uint32_t { return snap.getBlockLight(p.x, p.y, p.z); };

    if (!smoothLighting) {
        return VertexLightSample{.aoIdx = 3, .ao = 1.0f, .skyLight = sampleSky(baseNeighbor),
                                 .blockLight = sampleBlk(baseNeighbor)};
    }

    const glm::ivec3 s1Pos = baseNeighbor + tangent1;
    const glm::ivec3 s2Pos = baseNeighbor + tangent2;
    const glm::ivec3 cornerPos = baseNeighbor + tangent1 + tangent2;

    const bool s1Opaque = voxel::BlockRegistry::isOpaque(snap.getBlock(s1Pos.x, s1Pos.y, s1Pos.z));
    const bool s2Opaque = voxel::BlockRegistry::isOpaque(snap.getBlock(s2Pos.x, s2Pos.y, s2Pos.z));
    const bool cOpaque = voxel::BlockRegistry::isOpaque(snap.getBlock(cornerPos.x, cornerPos.y, cornerPos.z));

    int occluded = 0;
    if (s1Opaque && s2Opaque) {
        occluded = 3;
    } else {
        occluded = (s1Opaque ? 1 : 0) + (s2Opaque ? 1 : 0) + (cOpaque ? 1 : 0);
    }

    static constexpr float aoTable[4] = {0.50f, 0.66f, 0.82f, 1.00f};
    const uint32_t aoIdx = static_cast<uint32_t>(3 - std::clamp(occluded, 0, 3));

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

    return VertexLightSample{.aoIdx = aoIdx, .ao = aoTable[aoIdx], .skyLight = (sumSky + count / 2) / count,
                             .blockLight = (sumBlk + count / 2) / count};
}

void appendPackedQuad(std::vector<PackedChunkVertex>& out, const std::array<PackedChunkVertex, 4>& v,
                      const std::array<float, 4>& ao) {
    // Rotate the quad so the triangulation diagonal follows the brighter AO pair,
    // preventing anisotropic AO artefacts (identical rule to the legacy mesher).
    if (ao[0] + ao[2] > ao[1] + ao[3]) {
        out.insert(out.end(), {v[0], v[1], v[2], v[3]});
    } else {
        out.insert(out.end(), {v[1], v[2], v[3], v[0]});
    }
}

struct FaceGeometry {
    std::array<glm::uvec3, 4> corners;
    std::array<glm::ivec3, 4> t1;
    std::array<glm::ivec3, 4> t2;
};

constexpr FaceGeometry faceGeometry(core::FaceDir dir) noexcept {
    switch (dir) {
        case core::FaceDir::PosX:
            return {{{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}},
                    {{{0, -1, 0}, {0, -1, 0}, {0, 1, 0}, {0, 1, 0}}},
                    {{{0, 0, 1}, {0, 0, -1}, {0, 0, -1}, {0, 0, 1}}}};
        case core::FaceDir::NegX:
            return {{{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
                    {{{0, -1, 0}, {0, -1, 0}, {0, 1, 0}, {0, 1, 0}}},
                    {{{0, 0, -1}, {0, 0, 1}, {0, 0, 1}, {0, 0, -1}}}};
        case core::FaceDir::PosY:
            return {{{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}},
                    {{{-1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}}},
                    {{{0, 0, 1}, {0, 0, 1}, {0, 0, -1}, {0, 0, -1}}}};
        case core::FaceDir::NegY:
            return {{{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
                    {{{-1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}}},
                    {{{0, 0, -1}, {0, 0, -1}, {0, 0, 1}, {0, 0, 1}}}};
        case core::FaceDir::PosZ:
            return {{{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
                    {{{-1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}}},
                    {{{0, -1, 0}, {0, -1, 0}, {0, 1, 0}, {0, 1, 0}}}};
        case core::FaceDir::NegZ:
            return {{{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}},
                    {{{1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {1, 0, 0}}},
                    {{{0, -1, 0}, {0, -1, 0}, {0, 1, 0}, {0, 1, 0}}}};
    }
    return {};
}

void emitCross(std::vector<PackedChunkVertex>& out, const voxel::BlockDef& def, uint32_t lx, uint32_t ly, uint32_t lz,
               uint32_t skyLight, uint32_t blkLight, uint32_t biomeTint, uint32_t secY) {
    const uint32_t matTile = def.tileForFace(core::FaceDir::PosZ);
    constexpr uint32_t normalIdx = 6; // cross-billboard flora (explicit UV mapping)
    const uint32_t w1 = PackedChunkVertex::packWord1(skyLight, blkLight, biomeTint, 0, def.tintFoliage, false, secY);

    const auto addDoubleSidedQuad = [&](uint32_t x0, uint32_t y0, uint32_t z0, uint32_t x1, uint32_t y1, uint32_t z1) {
        const std::array<PackedChunkVertex, 4> front{{
            {PackedChunkVertex::packWord0(x0, y0, z0, normalIdx, 3, 3, matTile), w1},
            {PackedChunkVertex::packWord0(x1, y0, z1, normalIdx, 3, 2, matTile), w1},
            {PackedChunkVertex::packWord0(x1, y1 + 1, z1, normalIdx, 3, 1, matTile), w1},
            {PackedChunkVertex::packWord0(x0, y1 + 1, z0, normalIdx, 3, 0, matTile), w1}}};
        const std::array<PackedChunkVertex, 4> back{{
            {PackedChunkVertex::packWord0(x1, y0, z1, normalIdx, 3, 3, matTile), w1},
            {PackedChunkVertex::packWord0(x0, y0, z0, normalIdx, 3, 2, matTile), w1},
            {PackedChunkVertex::packWord0(x0, y1 + 1, z0, normalIdx, 3, 1, matTile), w1},
            {PackedChunkVertex::packWord0(x1, y1 + 1, z1, normalIdx, 3, 0, matTile), w1}}};
        appendPackedQuad(out, front, {1.0f, 1.0f, 1.0f, 1.0f});
        appendPackedQuad(out, back, {1.0f, 1.0f, 1.0f, 1.0f});
    };
    addDoubleSidedQuad(lx, ly, lz, lx + 1, ly, lz + 1);
    addDoubleSidedQuad(lx, ly, lz + 1, lx + 1, ly, lz);
}

void emitTorch(std::vector<PackedChunkVertex>& out, const voxel::BlockDef& def, uint32_t lx, uint32_t ly, uint32_t lz,
               uint32_t skyLight, uint32_t blkLight, uint32_t secY) {
    const uint32_t tile = def.tileForFace(core::FaceDir::PosZ);
    const uint32_t w1 = PackedChunkVertex::packWord1(skyLight, blkLight, 255, 0, false, false, secY);
    const auto quad = [&](uint32_t x0, uint32_t y0, uint32_t z0, uint32_t x1, uint32_t y1, uint32_t z1, uint32_t norm) {
        const std::array<PackedChunkVertex, 4> q{{
            {PackedChunkVertex::packWord0(x0, y0, z0, norm, 3, 0, tile), w1},
            {PackedChunkVertex::packWord0(x1, y0, z1, norm, 3, 1, tile), w1},
            {PackedChunkVertex::packWord0(x1, y1, z1, norm, 3, 2, tile), w1},
            {PackedChunkVertex::packWord0(x0, y1, z0, norm, 3, 3, tile), w1}}};
        appendPackedQuad(out, q, {1.0f, 1.0f, 1.0f, 1.0f});
    };
    quad(lx, ly, lz + 1, lx + 1, ly, lz + 1, 2);
    quad(lx + 1, ly, lz, lx, ly, lz, 3);
    quad(lx + 1, ly, lz + 1, lx + 1, ly, lz, 4);
    quad(lx, ly, lz, lx, ly, lz + 1, 5);
    quad(lx, ly + 1, lz + 1, lx + 1, ly + 1, lz + 1, 0);
}

} // namespace

std::optional<RawMeshBuffer> SnapshotMesher::build(const SectionNeighborhoodSnapshot18& snapshot, int32_t sectionY,
                                                   const MeshOptions& options, std::stop_token stopToken,
                                                   const MeshProgressFn& onLayerDone) {
    if (stopToken.stop_requested()) {
        return std::nullopt;
    }

    RawMeshBuffer mesh;
    if (snapshot.isInteriorEmpty()) {
        return mesh;
    }
    const uint32_t secY = static_cast<uint32_t>(sectionY);

    for (int32_t ly = 0; ly < Snap::SIZE; ++ly) {
        // Cooperative cancellation granularity: one 16x16 layer (~1/16 of the job).
        if (stopToken.stop_requested()) {
            return std::nullopt;
        }
        for (int32_t lz = 0; lz < Snap::SIZE; ++lz) {
            for (int32_t lx = 0; lx < Snap::SIZE; ++lx) {
                const voxel::BlockId id = snapshot.getBlock(lx, ly, lz);
                if (id == voxel::BlockId::Air) {
                    continue;
                }
                const voxel::BlockDef& def = voxel::BlockRegistry::get(id);
                std::vector<PackedChunkVertex>& target =
                    (def.layer == voxel::RenderLayer::Translucent) ? mesh.translucentVertices : mesh.opaqueVertices;

                const uint32_t ux = static_cast<uint32_t>(lx);
                const uint32_t uy = static_cast<uint32_t>(ly);
                const uint32_t uz = static_cast<uint32_t>(lz);
                const uint32_t skyLight = snapshot.getSkyLight(lx, ly, lz);
                const uint32_t blkLight =
                    (def.lightEmission > 0) ? def.lightEmission : snapshot.getBlockLight(lx, ly, lz);
                const uint32_t biomeTint = def.tintGrassTop ? 128u : (def.tintFoliage ? 160u : 255u);

                if (def.shape == voxel::RenderShape::Cross) {
                    emitCross(target, def, ux, uy, uz, skyLight, blkLight, biomeTint, secY);
                    continue;
                }
                if (def.shape == voxel::RenderShape::Torch) {
                    emitTorch(target, def, ux, uy, uz, skyLight, blkLight, secY);
                    continue;
                }

                if (voxel::isDetailedShape(def.shape)) {
                    const auto sample = [&](int32_t dx, int32_t dy, int32_t dz) {
                        return ShapeVoxel{snapshot.getBlock(lx + dx, ly + dy, lz + dz),
                                          snapshot.getMetadata(lx + dx, ly + dy, lz + dz),
                                          snapshot.getSkyLight(lx + dx, ly + dy, lz + dz),
                                          snapshot.getBlockLight(lx + dx, ly + dy, lz + dz)};
                    };
                    emitDetailedShape(target, def, snapshot.getMetadata(lx, ly, lz), ux, uy, uz, secY, sample);
                    continue;
                }

                const bool isSlab = (def.shape == voxel::RenderShape::Slab);
                const bool isTranslucent = (def.layer == voxel::RenderLayer::Translucent);

                for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
                    const auto dir = static_cast<core::FaceDir>(faceIdx);
                    const glm::ivec3 n = core::faceNormal(dir);
                    const glm::ivec3 nbPos(lx + n.x, ly + n.y, lz + n.z);
                    const voxel::BlockId nbId = snapshot.getBlock(nbPos.x, nbPos.y, nbPos.z);
                    const voxel::BlockDef& nbDef = voxel::BlockRegistry::get(nbId);

                    if (def.shape != voxel::RenderShape::Cactus && !(isSlab && dir == core::FaceDir::PosY) &&
                        !shouldRenderBlockFace(id, def, nbId, nbDef)) {
                        continue;
                    }

                    const uint32_t tile = def.tileForFace(dir);
                    const uint32_t normIdx = faceDirToNormalIdx(dir);
                    const FaceGeometry geo = faceGeometry(dir);

                    std::array<PackedChunkVertex, 4> quad{};
                    std::array<float, 4> aoValues{};
                    for (size_t k = 0; k < 4; ++k) {
                        const VertexLightSample s =
                            sampleVertexAOAndLight(snapshot, nbPos, geo.t1[k], geo.t2[k], options.smoothLighting);
                        aoValues[k] = s.ao;
                        const uint32_t vBlk = (def.lightEmission > 0) ? def.lightEmission : s.blockLight;
                        quad[k] = PackedChunkVertex{
                            PackedChunkVertex::packWord0(ux + geo.corners[k].x, uy + geo.corners[k].y,
                                                         uz + geo.corners[k].z, normIdx, s.aoIdx,
                                                         static_cast<uint32_t>(k), tile),
                            PackedChunkVertex::packWord1(s.skyLight, vBlk, biomeTint, 0, def.tintFoliage,
                                                         isTranslucent, secY)};
                    }
                    appendPackedQuad(target, quad, aoValues);
                }
            }
        }
        if (onLayerDone) {
            onLayerDone(ly + 1);
        }
    }

    mesh.opaqueQuadCount = static_cast<uint32_t>(mesh.opaqueVertices.size() / 4);
    mesh.translucentQuadCount = static_cast<uint32_t>(mesh.translucentVertices.size() / 4);
    return mesh;
}

} // namespace betacraft::streaming
