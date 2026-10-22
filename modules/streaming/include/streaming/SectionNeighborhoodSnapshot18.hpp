#pragma once

#include "voxel/Block.hpp"
#include "voxel/PalettedSection.hpp"
#include "voxel/SectionCoord.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace betacraft::world {
class World;
}

namespace betacraft::streaming {

// One voxel as seen by a capture source. Light values are 0..15 (clamped on capture).
struct VoxelSample {
    voxel::BlockStateId state{static_cast<voxel::BlockStateId>(voxel::BlockId::Air)};
    uint8_t skyLight{15};
    uint8_t blockLight{0};
    uint8_t meta{0}; // 4-bit block metadata (S5.4: door state, ladder side, crop stage...)
};

// Value used for voxels whose source is absent (unloaded neighbour chunk) or for reads
// outside the 18^3 cube. Mirrors World's own fallback for missing chunks: open air under
// full sky, so border faces render and are lit as if facing the void.
inline constexpr VoxelSample kMissingVoxel{static_cast<voxel::BlockStateId>(voxel::BlockId::Air), 15, 0};

// Immutable 18x18x18 copy of a 16^3 section plus a 1-voxel guard border on all six sides.
// Captured synchronously on the main/tick thread, then shared with mesher workers as
// std::shared_ptr<const ...>: workers never touch live World/Chunk/PalettedSection data,
// which removes every data race on neighbour lookups by construction.
class alignas(64) SectionNeighborhoodSnapshot18 {
public:
    static constexpr int32_t SIZE = 16;
    static constexpr int32_t STRIDE = SIZE + 2;
    static constexpr int32_t TOTAL_VOXELS = STRIDE * STRIDE * STRIDE; // 5832
    static constexpr int32_t MIN_LOCAL = -1;
    static constexpr int32_t MAX_LOCAL = SIZE;

    SectionNeighborhoodSnapshot18() noexcept { fill(kMissingVoxel); }

    [[nodiscard]] static constexpr bool inRange(int32_t lx, int32_t ly, int32_t lz) noexcept {
        return lx >= MIN_LOCAL && lx <= MAX_LOCAL && ly >= MIN_LOCAL && ly <= MAX_LOCAL &&
               lz >= MIN_LOCAL && lz <= MAX_LOCAL;
    }

    // Local coordinates are section-relative in [-1, 16]; X varies fastest, then Z, then Y.
    [[nodiscard]] static constexpr size_t voxelIndex(int32_t lx, int32_t ly, int32_t lz) noexcept {
        return static_cast<size_t>(((ly + 1) * STRIDE + (lz + 1)) * STRIDE + (lx + 1));
    }

    // Reads outside [-1, 16]^3 return kMissingVoxel instead of touching foreign memory.
    [[nodiscard]] voxel::BlockStateId getBlockState(int32_t lx, int32_t ly, int32_t lz) const noexcept {
        return inRange(lx, ly, lz) ? static_cast<voxel::BlockStateId>(m_blockStates[voxelIndex(lx, ly, lz)] & kStateMask)
                                   : kMissingVoxel.state;
    }
    [[nodiscard]] voxel::BlockId getBlock(int32_t lx, int32_t ly, int32_t lz) const noexcept {
        // Same narrowing as PalettedSection::getBlock (block id lives in the low byte).
        return static_cast<voxel::BlockId>(getBlockState(lx, ly, lz) & 0xFFu);
    }
    [[nodiscard]] uint8_t getSkyLight(int32_t lx, int32_t ly, int32_t lz) const noexcept {
        return inRange(lx, ly, lz) ? static_cast<uint8_t>((m_lightData[voxelIndex(lx, ly, lz)] >> 4u) & 0x0Fu)
                                   : kMissingVoxel.skyLight;
    }
    [[nodiscard]] uint8_t getBlockLight(int32_t lx, int32_t ly, int32_t lz) const noexcept {
        return inRange(lx, ly, lz) ? static_cast<uint8_t>(m_lightData[voxelIndex(lx, ly, lz)] & 0x0Fu)
                                   : kMissingVoxel.blockLight;
    }
    // Same as World::getMetadata (0 for missing voxels).
    [[nodiscard]] uint8_t getMetadata(int32_t lx, int32_t ly, int32_t lz) const noexcept {
        return inRange(lx, ly, lz) ? static_cast<uint8_t>(m_blockStates[voxelIndex(lx, ly, lz)] >> kMetaShift)
                                   : kMissingVoxel.meta;
    }

    // True when the 16^3 interior is all air (border ignored): lets the pipeline skip
    // scheduling a mesher job for empty sky sections entirely.
    [[nodiscard]] bool isInteriorEmpty() const noexcept { return m_interiorEmpty; }

    // Generic capture: `sampler(lx, ly, lz)` is called exactly once per voxel of the
    // padded cube and returns std::optional<VoxelSample>; std::nullopt -> kMissingVoxel.
    template <typename Sampler>
        requires std::invocable<Sampler&, int32_t, int32_t, int32_t>
    [[nodiscard]] static std::shared_ptr<const SectionNeighborhoodSnapshot18> captureFrom(Sampler&& sampler) {
        auto snap = std::make_shared<SectionNeighborhoodSnapshot18>();
        bool interiorEmpty = true;
        for (int32_t ly = MIN_LOCAL; ly <= MAX_LOCAL; ++ly) {
            for (int32_t lz = MIN_LOCAL; lz <= MAX_LOCAL; ++lz) {
                for (int32_t lx = MIN_LOCAL; lx <= MAX_LOCAL; ++lx) {
                    const std::optional<VoxelSample> sample = sampler(lx, ly, lz);
                    const VoxelSample v = sample.value_or(kMissingVoxel);
                    snap->setVoxelDirect(voxelIndex(lx, ly, lz), v.state, v.skyLight, v.blockLight, v.meta);
                    if (interiorEmpty && isInterior(lx, ly, lz) &&
                        v.state != static_cast<voxel::BlockStateId>(voxel::BlockId::Air)) {
                        interiorEmpty = false;
                    }
                }
            }
        }
        snap->m_interiorEmpty = interiorEmpty;
        return snap;
    }

    // Main-thread capture from the live World. Reproduces World::getBlock/getSkyLight/
    // getBlockLight semantics exactly (unloaded chunk -> air + sky 15, y >= 128 -> sky 15,
    // y < 0 -> sky 0) but resolves the 3x3 chunk column once instead of per voxel.
    [[nodiscard]] static std::shared_ptr<const SectionNeighborhoodSnapshot18> capture(
        const world::World& world, voxel::SectionCoord coord);

private:
    [[nodiscard]] static constexpr bool isInterior(int32_t lx, int32_t ly, int32_t lz) noexcept {
        return lx >= 0 && lx < SIZE && ly >= 0 && ly < SIZE && lz >= 0 && lz < SIZE;
    }

    // S5.4: the metadata nibble rides in the top 4 bits of the stored state (states hold a
    // block id, below 256), which keeps the snapshot at its 17 KB footprint.
    static constexpr uint32_t kMetaShift = 12;
    static constexpr voxel::BlockStateId kStateMask = (1u << kMetaShift) - 1u;

    [[nodiscard]] static constexpr voxel::BlockStateId packState(voxel::BlockStateId state, uint8_t meta) noexcept {
        return static_cast<voxel::BlockStateId>((state & kStateMask) | ((meta & 0x0Fu) << kMetaShift));
    }

    void setVoxelDirect(size_t index, voxel::BlockStateId block, uint8_t skyLight, uint8_t blkLight,
                        uint8_t meta = 0) noexcept {
        m_blockStates[index] = packState(block, meta);
        m_lightData[index] = static_cast<uint8_t>(((skyLight > 15 ? 15 : skyLight) << 4u) |
                                                  (blkLight > 15 ? 15 : blkLight));
    }

    void fill(const VoxelSample& v) noexcept {
        m_blockStates.fill(packState(v.state, v.meta));
        m_lightData.fill(static_cast<uint8_t>(((v.skyLight & 0x0Fu) << 4u) | (v.blockLight & 0x0Fu)));
    }

    std::array<voxel::BlockStateId, TOTAL_VOXELS> m_blockStates{}; // 11 664 bytes (state | meta << 12)
    std::array<uint8_t, TOTAL_VOXELS> m_lightData{};               //  5 832 bytes (sky << 4 | block)
    bool m_interiorEmpty{true};
};

} // namespace betacraft::streaming
