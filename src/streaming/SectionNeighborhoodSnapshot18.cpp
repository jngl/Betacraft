#include "streaming/SectionNeighborhoodSnapshot18.hpp"

#include "voxel/Chunk.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>

namespace betacraft::streaming {

namespace {

constexpr world::BlockStateId kAirState = static_cast<world::BlockStateId>(world::BlockId::Air);

// A default-constructed snapshot is already filled with kMissingVoxel, which is exactly
// World's value for unloaded chunks and for y >= 128: capture() skips those voxels.
static_assert(kMissingVoxel.state == kAirState && kMissingVoxel.skyLight == 15 && kMissingVoxel.blockLight == 0);

[[nodiscard]] constexpr uint8_t packLight(uint8_t sky, uint8_t block) noexcept {
    return static_cast<uint8_t>((std::min<uint8_t>(sky, 15) << 4u) | std::min<uint8_t>(block, 15));
}

} // namespace

std::shared_ptr<const SectionNeighborhoodSnapshot18> SectionNeighborhoodSnapshot18::capture(
    const world::World& world, voxel::SectionCoord coord) {
    // Resolve the 3x3 neighbourhood of chunk columns once: per-voxel hash-map lookups
    // through World::getBlock would dominate the capture cost on the tick thread.
    std::array<const world::Chunk*, 9> columns{};
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            columns[static_cast<size_t>((dz + 1) * 3 + (dx + 1))] = world.getChunk({coord.x + dx, coord.z + dz});
        }
    }

    auto snap = std::make_shared<SectionNeighborhoodSnapshot18>();
    bool interiorEmpty = true;

    // Each padded X row is copied as three contiguous runs (x = -1 | 0..15 | 16), one per
    // source column. A source section holding a single block (bitsPerVoxel 0) or uniform
    // light fills its run without decoding a voxel; generated sky and deep-stone sections
    // hit at least one of those paths.
    struct Run {
        int32_t first;  // local x of the first voxel
        int32_t length; // voxels
        size_t column;  // 0..2 along X
    };
    static constexpr std::array<Run, 3> kRuns{{{-1, 1, 0}, {0, SIZE, 1}, {SIZE, 1, 2}}};

    const int32_t baseY = coord.y * SIZE;
    for (int32_t ly = MIN_LOCAL; ly <= MAX_LOCAL; ++ly) {
        const int32_t wy = baseY + ly;
        if (wy >= world::Chunk::HEIGHT) {
            continue; // above the column: open sky (already the default fill)
        }
        if (wy < 0) {
            // Below bedrock is dark even in an unloaded column (World::getSkyLight order).
            for (int32_t lz = MIN_LOCAL; lz <= MAX_LOCAL; ++lz) {
                const size_t row = voxelIndex(MIN_LOCAL, ly, lz);
                std::fill_n(snap->m_blockStates.begin() + static_cast<std::ptrdiff_t>(row), STRIDE, kAirState);
                std::fill_n(snap->m_lightData.begin() + static_cast<std::ptrdiff_t>(row), STRIDE, packLight(0, 0));
            }
            continue;
        }
        const size_t sectionIndex = static_cast<size_t>(wy >> 4);
        const int32_t y = wy & 15;
        const bool interiorLayer = ly >= 0 && ly < SIZE;

        for (int32_t lz = MIN_LOCAL; lz <= MAX_LOCAL; ++lz) {
            const size_t czi = lz < 0 ? 0u : (lz >= SIZE ? 2u : 1u);
            const int32_t z = core::floorMod(lz, SIZE);
            const bool interiorRow = interiorLayer && czi == 1u;

            for (const Run& run : kRuns) {
                const world::Chunk* chunk = columns[czi * 3u + run.column];
                if (chunk == nullptr) {
                    continue; // unloaded: Air, sky 15, block 0 (default fill)
                }
                const world::PalettedSection& section = chunk->getSection(sectionIndex);
                const int32_t x0 = core::floorMod(run.first, SIZE);
                const size_t dst = voxelIndex(run.first, ly, lz);
                world::BlockStateId* states = snap->m_blockStates.data() + dst;
                uint8_t* light = snap->m_lightData.data() + dst;

                if (section.getBitsPerVoxel() == 0) {
                    std::fill_n(states, run.length, section.getBlockState(x0, y, z));
                } else {
                    for (int32_t i = 0; i < run.length; ++i) {
                        states[i] = section.getBlockState(x0 + i, y, z);
                    }
                }
                if (section.hasMetadata()) {
                    for (int32_t i = 0; i < run.length; ++i) {
                        states[i] = packState(states[i], section.getMetadata(x0 + i, y, z));
                    }
                }
                if (!section.hasLightData()) {
                    std::fill_n(light, run.length,
                                packLight(section.getUniformSkyLight(), section.getUniformBlockLight()));
                } else {
                    for (int32_t i = 0; i < run.length; ++i) {
                        light[i] = packLight(section.getSkyLight(x0 + i, y, z), section.getBlockLight(x0 + i, y, z));
                    }
                }
                if (interiorEmpty && interiorRow && run.column == 1u &&
                    std::any_of(states, states + run.length,
                                [](world::BlockStateId s) { return (s & kStateMask) != kAirState; })) {
                    interiorEmpty = false;
                }
            }
        }
    }
    snap->m_interiorEmpty = interiorEmpty;
    return snap;
}

} // namespace betacraft::streaming
