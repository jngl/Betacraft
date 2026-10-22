#pragma once

#include "streaming/PackedChunkVertex.hpp"
#include "voxel/Block.hpp"
#include "voxel/BlockShapes.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <type_traits>
#include <vector>

// S5.4 geometry of the detailed shapes (voxel::isDetailedShape): ladders, doors, beds, fences,
// crops, farmland and stairs, built from 1/16-block boxes and planes. Shared by
// render::ChunkMesher (live World reads) and SnapshotMesher (18^3 snapshot reads)
// so the two meshes stay identical by construction: each passes a sampler returning the
// voxel at an offset from the meshed cell.
//
// Lighting is flat per face, without ambient occlusion: a face on the cell boundary takes the
// light of the cell it faces, an inner face that of the cell itself (of the cell it faces when
// the block stops light, like stairs and farmland). When that cell stops light too, the cell
// above the block is used if brighter. Boundary faces against an opaque cube are culled like
// cube faces.

namespace betacraft::streaming {

struct ShapeVoxel {
    voxel::BlockId id{voxel::BlockId::Air};
    uint8_t meta{0};
    uint8_t skyLight{15};
    uint8_t blockLight{0};
};

namespace shape_mesher_detail {

constexpr uint32_t normalIndex(core::FaceDir dir) noexcept {
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

// Corner selectors (0 = box min, 1 = box max per axis), wound like the cube faces of the
// meshers so front faces keep their orientation.
constexpr std::array<glm::uvec3, 4> faceCorners(core::FaceDir dir) noexcept {
    switch (dir) {
        case core::FaceDir::PosX: return {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}};
        case core::FaceDir::NegX: return {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}};
        case core::FaceDir::PosY: return {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}};
        case core::FaceDir::NegY: return {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}};
        case core::FaceDir::PosZ: return {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};
        case core::FaceDir::NegZ: return {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}};
    }
    return {};
}

constexpr uint32_t faceBit(core::FaceDir dir) noexcept {
    return 1u << static_cast<uint32_t>(dir);
}

inline constexpr uint32_t kAllFaces = 0x3Fu;

template <class Sample>
class ShapeEmitter {
public:
    ShapeEmitter(std::vector<PackedChunkVertex>& out, const voxel::BlockDef& def, uint32_t ux, uint32_t uy, uint32_t uz,
                 uint32_t secY, Sample& sample) noexcept
        : m_out(out), m_def(def), m_ux(ux), m_uy(uy), m_uz(uz), m_secY(secY), m_sample(sample) {}

    // The six faces of `box` named by `faces`, tile per face from tileFor(FaceDir).
    template <class TileFor>
    void box(const voxel::ShapeBox& b, uint32_t faces, TileFor&& tileFor) {
        for (int f = 0; f < 6; ++f) {
            const auto dir = static_cast<core::FaceDir>(f);
            if ((faces & faceBit(dir)) == 0) {
                continue;
            }
            const glm::ivec3 n = core::faceNormal(dir);
            const bool onBoundary = (n.x > 0 && b.x1 == 16) || (n.x < 0 && b.x0 == 0) ||
                                    (n.y > 0 && b.y1 == 16) || (n.y < 0 && b.y0 == 0) ||
                                    (n.z > 0 && b.z1 == 16) || (n.z < 0 && b.z0 == 0);
            if (onBoundary) {
                const ShapeVoxel nb = m_sample(n.x, n.y, n.z);
                const voxel::BlockDef& nbDef = voxel::BlockRegistry::get(nb.id);
                if (nbDef.opaque && nbDef.shape == voxel::RenderShape::Cube) {
                    continue;
                }
                if (nb.id == m_def.id && m_def.shape == voxel::RenderShape::Fence) {
                    continue; // Stacked posts and joined bars hide each other
                }
            }
            quad(b, dir, tileFor(dir), faceLight(n, onBoundary));
        }
    }

    // A double-sided plane `b` (one axis has zero thickness), lit by the cell itself.
    void plane(const voxel::ShapeBox& b, core::FaceDir front, uint32_t tile) {
        const ShapeVoxel self = m_sample(0, 0, 0);
        const glm::ivec3 n = core::faceNormal(front);
        const core::FaceDir back = n.x != 0 ? (n.x > 0 ? core::FaceDir::NegX : core::FaceDir::PosX)
                                 : n.y != 0 ? (n.y > 0 ? core::FaceDir::NegY : core::FaceDir::PosY)
                                            : (n.z > 0 ? core::FaceDir::NegZ : core::FaceDir::PosZ);
        quad(b, front, tile, self);
        quad(b, back, tile, self);
    }

private:
    [[nodiscard]] ShapeVoxel faceLight(const glm::ivec3& n, bool onBoundary) {
        const bool facedCell = onBoundary || m_def.lightOpacity >= 15;
        ShapeVoxel light = facedCell ? m_sample(n.x, n.y, n.z) : m_sample(0, 0, 0);
        if (voxel::BlockRegistry::get(light.id).lightOpacity >= 15) {
            // A light-stopping cell (the next stair block) is dark inside: take the sky over
            // this block when brighter.
            const ShapeVoxel above = m_sample(0, 1, 0);
            light.skyLight = std::max(light.skyLight, above.skyLight);
            light.blockLight = std::max(light.blockLight, above.blockLight);
        }
        return light;
    }

    void quad(const voxel::ShapeBox& b, core::FaceDir dir, uint32_t tile, const ShapeVoxel& light) {
        const std::array<glm::uvec3, 4> corners = faceCorners(dir);
        const uint32_t normIdx = normalIndex(dir);
        const uint32_t sky = light.skyLight;
        const uint32_t blk = m_def.lightEmission > 0 ? m_def.lightEmission : light.blockLight;
        const uint32_t w1 = PackedChunkVertex::packWord1(sky, blk, 255, 0, false, false, m_secY);
        std::array<PackedChunkVertex, 4> v{};
        for (size_t k = 0; k < 4; ++k) {
            const uint32_t sx = corners[k].x != 0 ? b.x1 : b.x0;
            const uint32_t sy = corners[k].y != 0 ? std::min<uint32_t>(b.y1, 16) : b.y0;
            const uint32_t sz = corners[k].z != 0 ? b.z1 : b.z0;
            // Integer corner at or above the point, and how many sixteenths below it.
            const auto split = [](uint32_t base, uint32_t sixteenths, uint32_t& off) {
                const uint32_t up = (sixteenths + 15u) / 16u;
                off = up * 16u - sixteenths;
                return base + up;
            };
            uint32_t ox = 0, oy = 0, oz = 0;
            const uint32_t px = split(m_ux, sx, ox);
            const uint32_t py = split(m_uy, sy, oy);
            const uint32_t pz = split(m_uz, sz, oz);
            v[k] = PackedChunkVertex{PackedChunkVertex::packWord0(px, py, pz, normIdx, 3, static_cast<uint32_t>(k), tile),
                                     w1};
            v[k].setFineOffset(ox, oy, oz);
        }
        // Same diagonal choice as the cube faces with uniform AO (rotated quad).
        m_out.insert(m_out.end(), {v[1], v[2], v[3], v[0]});
    }

    std::vector<PackedChunkVertex>& m_out;
    const voxel::BlockDef& m_def;
    uint32_t m_ux;
    uint32_t m_uy;
    uint32_t m_uz;
    uint32_t m_secY;
    Sample& m_sample;
};

} // namespace shape_mesher_detail

// Appends the geometry of the detailed-shape block `def` (metadata `meta`) whose cell starts
// at packed local position (ux, uy, uz) of section `secY`. sample(dx, dy, dz) -> ShapeVoxel
// reads the voxel at that offset from the cell (|d| <= 1).
template <class Sample>
void emitDetailedShape(std::vector<PackedChunkVertex>& out, const voxel::BlockDef& def, uint8_t meta, uint32_t ux,
                       uint32_t uy, uint32_t uz, uint32_t secY, Sample&& sample) {
    using namespace shape_mesher_detail;
    using voxel::RenderShape;
    using voxel::ShapeBox;
    ShapeEmitter<std::remove_reference_t<Sample>> e(out, def, ux, uy, uz, secY, sample);
    const auto defTiles = [&def](core::FaceDir dir) -> uint32_t { return def.tileForFace(dir); };

    switch (def.shape) {
        case RenderShape::Ladder: {
            // A plate one sixteenth off the wall, as Beta draws it.
            const glm::ivec3 wall = voxel::ladderSupportOffset(meta);
            ShapeBox p{0, 0, 0, 16, 16, 16};
            core::FaceDir front = core::FaceDir::NegZ;
            if (wall.z > 0) { p.z0 = p.z1 = 15; front = core::FaceDir::NegZ; }
            if (wall.z < 0) { p.z0 = p.z1 = 1; front = core::FaceDir::PosZ; }
            if (wall.x > 0) { p.x0 = p.x1 = 15; front = core::FaceDir::NegX; }
            if (wall.x < 0) { p.x0 = p.x1 = 1; front = core::FaceDir::PosX; }
            e.plane(p, front, def.tileForFace(core::FaceDir::PosZ));
            break;
        }
        case RenderShape::Crops: {
            // Beta renderBlockCrops: four planes at 1/4 and 3/4 of the cell.
            const uint32_t tile = voxel::kCropsFirstTile + std::min<uint32_t>(meta, voxel::kCropsMaxStage);
            e.plane({4, 0, 0, 4, 16, 16}, core::FaceDir::PosX, tile);
            e.plane({12, 0, 0, 12, 16, 16}, core::FaceDir::NegX, tile);
            e.plane({0, 0, 4, 16, 16, 4}, core::FaceDir::PosZ, tile);
            e.plane({0, 0, 12, 16, 16, 12}, core::FaceDir::NegZ, tile);
            break;
        }
        case RenderShape::Door: {
            const bool upper = (meta & voxel::kDoorUpperBit) != 0;
            const bool iron = def.id == voxel::BlockId::IronDoor;
            const uint32_t tile = iron ? (upper ? voxel::kIronDoorUpperTile : voxel::kIronDoorLowerTile)
                                       : (upper ? voxel::kDoorUpperTile : voxel::kDoorLowerTile);
            e.box(voxel::doorBox(meta), kAllFaces, [tile](core::FaceDir) { return tile; });
            break;
        }
        case RenderShape::Bed: {
            const bool head = (meta & voxel::kBedHeadBit) != 0;
            const uint32_t top = head ? voxel::kBedHeadTopTile : voxel::kBedFootTopTile;
            const uint32_t side = head ? voxel::kBedHeadSideTile : voxel::kBedFootSideTile;
            const uint32_t bottom = def.tileForFace(core::FaceDir::NegY);
            e.box({0, 0, 0, 16, voxel::kBedHeight, 16}, kAllFaces, [&](core::FaceDir dir) {
                return dir == core::FaceDir::PosY ? top : (dir == core::FaceDir::NegY ? bottom : side);
            });
            break;
        }
        case RenderShape::Farmland: {
            const uint32_t top = meta > 0 ? voxel::kFarmlandWetTile : voxel::kFarmlandDryTile;
            e.box({0, 0, 0, 16, voxel::kFarmlandHeight, 16}, kAllFaces, [&](core::FaceDir dir) {
                return dir == core::FaceDir::PosY ? top : def.tileForFace(dir);
            });
            break;
        }
        case RenderShape::Fence: {
            // Beta 1.7.3 renderBlockFence: a 4/16 post, and two bars towards each neighbouring
            // fence (fences connect to fences only).
            e.box({6, 0, 6, 10, 16, 10}, kAllFaces, defTiles);
            const auto joins = [&](int dx, int dz) { return sample(dx, 0, dz).id == def.id; };
            for (const uint8_t y0 : {uint8_t{12}, uint8_t{6}}) {
                const auto y1 = static_cast<uint8_t>(y0 + 3);
                if (joins(1, 0)) e.box({10, y0, 7, 16, y1, 9}, kAllFaces & ~faceBit(core::FaceDir::NegX), defTiles);
                if (joins(-1, 0)) e.box({0, y0, 7, 6, y1, 9}, kAllFaces & ~faceBit(core::FaceDir::PosX), defTiles);
                if (joins(0, 1)) e.box({7, y0, 10, 9, y1, 16}, kAllFaces & ~faceBit(core::FaceDir::NegZ), defTiles);
                if (joins(0, -1)) e.box({7, y0, 0, 9, y1, 6}, kAllFaces & ~faceBit(core::FaceDir::PosZ), defTiles);
            }
            break;
        }
        case RenderShape::Stairs: {
            // A bottom slab and the upper step on the metadata's side; the step's underside
            // lies on the slab and is never seen.
            e.box({0, 0, 0, 16, 8, 16}, kAllFaces, defTiles);
            ShapeBox step{0, 8, 0, 16, 16, 16};
            switch (meta & 3) {
                case 0: step.x0 = 8; break;
                case 1: step.x1 = 8; break;
                case 2: step.z0 = 8; break;
                default: step.z1 = 8; break;
            }
            e.box(step, kAllFaces & ~faceBit(core::FaceDir::NegY), defTiles);
            break;
        }
        case RenderShape::Flat: {
            // S8.5 wire (bright tile when powered) and pressure plates: a 1/16 slab whose
            // underside lies on the block below.
            uint32_t tile = def.tileForFace(core::FaceDir::PosY);
            if (def.id == voxel::BlockId::RedstoneWire) {
                tile = meta > 0 ? voxel::kRedstoneWireOnTile : voxel::kRedstoneWireOffTile;
            }
            e.box(voxel::flatBox(def.id), kAllFaces, [tile](core::FaceDir) { return tile; });
            break;
        }
        case RenderShape::Lever: {
            // S8.5 lever (cobblestone base and a stick that leans the other way when on) and
            // button, turned onto the wall or floor they hang on.
            if (def.id == voxel::BlockId::Lever) {
                e.box(voxel::orientAttachedBox(voxel::leverBaseBox(), meta), kAllFaces,
                      [](core::FaceDir) { return 16u; });
                e.box(voxel::orientAttachedBox(voxel::leverHandleBox(meta), meta), kAllFaces,
                      [&def](core::FaceDir) { return static_cast<uint32_t>(def.tileForFace(core::FaceDir::PosZ)); });
            } else {
                e.box(voxel::orientAttachedBox(voxel::buttonBox(meta), meta), kAllFaces, defTiles);
            }
            break;
        }
        default:
            break;
    }
}

} // namespace betacraft::streaming
