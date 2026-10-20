#pragma once

#include "voxel/Block.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace betacraft::voxel {

using BlockStateId = uint16_t;

// Dense light array allocated lazily for sections with non-uniform lighting gradients
struct alignas(32) DenseLightArray {
    // 4096 bytes: Upper 4 bits = SkyLight (0..15), Lower 4 bits = BlockLight (0..15)
    std::array<uint8_t, 4096> data;

    DenseLightArray(uint8_t uniformSky, uint8_t uniformBlock) noexcept {
        const uint8_t packedVal = static_cast<uint8_t>(((uniformSky & 0x0FU) << 4U) | (uniformBlock & 0x0FU));
        data.fill(packedVal);
    }
};

// Dense 4-bit metadata (fluid level nibble), allocated lazily like DenseLightArray: almost
// every section has none. Even voxel index = low nibble, odd = high nibble.
struct DenseNibbleArray {
    std::array<uint8_t, 2048> data{};

    [[nodiscard]] uint8_t get(size_t idx) const noexcept {
        const uint8_t b = data[idx >> 1U];
        return (idx & 1U) ? static_cast<uint8_t>(b >> 4U) : static_cast<uint8_t>(b & 0x0FU);
    }

    void set(size_t idx, uint8_t nibble) noexcept {
        uint8_t& b = data[idx >> 1U];
        b = (idx & 1U) ? static_cast<uint8_t>((b & 0x0FU) | ((nibble & 0x0FU) << 4U))
                       : static_cast<uint8_t>((b & 0xF0U) | (nibble & 0x0FU));
    }
};

class alignas(64) PalettedSection {
public:
    static constexpr int32_t SIZE = 16;
    static constexpr int32_t VOLUME = SIZE * SIZE * SIZE; // 4096

    explicit PalettedSection(BlockId singleBlock = BlockId::Air, uint8_t uniformSky = 15, uint8_t uniformBlock = 0) noexcept;
    PalettedSection(BlockStateId singleBlock, uint8_t uniformSky, uint8_t uniformBlock) noexcept;

    PalettedSection(const PalettedSection& other);
    PalettedSection& operator=(const PalettedSection& other);

    PalettedSection(PalettedSection&& other) noexcept = default;
    PalettedSection& operator=(PalettedSection&& other) noexcept = default;

    ~PalettedSection() = default;

    [[nodiscard]] static constexpr bool inBounds(int32_t lx, int32_t ly, int32_t lz) noexcept {
        return lx >= 0 && lx < SIZE && ly >= 0 && ly < SIZE && lz >= 0 && lz < SIZE;
    }

    [[nodiscard]] static constexpr size_t voxelIndex(int32_t lx, int32_t ly, int32_t lz) noexcept {
        return static_cast<size_t>((ly * SIZE + lz) * SIZE + lx);
    }

    // Block access
    [[nodiscard]] BlockId getBlock(int32_t lx, int32_t ly, int32_t lz) const noexcept;
    [[nodiscard]] BlockStateId getBlockState(int32_t lx, int32_t ly, int32_t lz) const noexcept;

    void setBlock(int32_t lx, int32_t ly, int32_t lz, BlockId id) noexcept;
    void setBlock(int32_t lx, int32_t ly, int32_t lz, BlockStateId state) noexcept;

    // Light access
    [[nodiscard]] uint8_t getSkyLight(int32_t lx, int32_t ly, int32_t lz) const noexcept;
    [[nodiscard]] uint8_t getBlockLight(int32_t lx, int32_t ly, int32_t lz) const noexcept;

    void setSkyLight(int32_t lx, int32_t ly, int32_t lz, uint8_t val) noexcept;
    void setBlockLight(int32_t lx, int32_t ly, int32_t lz, uint8_t val) noexcept;

    void setUniformLighting(uint8_t sky, uint8_t block) noexcept;

    // Per-voxel 4-bit metadata (fluid level + falling flag). Reads 0 until a non-zero value is
    // written; changing a voxel's block resets its metadata to 0 (Beta Chunk.setBlockID).
    [[nodiscard]] uint8_t getMetadata(int32_t lx, int32_t ly, int32_t lz) const noexcept;
    void setMetadata(int32_t lx, int32_t ly, int32_t lz, uint8_t meta) noexcept;
    [[nodiscard]] bool hasMetadata() const noexcept;

    // Introspection
    [[nodiscard]] bool isHomogeneous() const noexcept;
    [[nodiscard]] bool hasLightData() const noexcept;
    [[nodiscard]] uint8_t getBitsPerVoxel() const noexcept;
    [[nodiscard]] size_t getPaletteSize() const noexcept;
    // Whether `pred(BlockId)` may hold for some voxel of the section: exact for a single-block
    // section, a superset for paletted ones (an entry can outlive its last voxel), always true
    // in direct 16-bit storage, which keeps no palette. Lets S5.1 random ticks skip sections.
    template <typename Pred>
    [[nodiscard]] bool mayContain(Pred&& pred) const {
        if (m_bitsPerVoxel == 0) return pred(static_cast<BlockId>(m_singleBlock));
        if (m_bitsPerVoxel == 16) return true;
        return std::any_of(m_palette.begin(), m_palette.end(),
                           [&](BlockStateId s) { return pred(static_cast<BlockId>(s)); });
    }
    [[nodiscard]] size_t getMemoryFootprintBytes() const noexcept;
    [[nodiscard]] uint8_t getUniformSkyLight() const noexcept { return m_uniformSkyLight; }
    [[nodiscard]] uint8_t getUniformBlockLight() const noexcept { return m_uniformBlockLight; }

private:
    // Packed storage size is fully determined by the bit width, so it is held in a plain
    // buffer (8 bytes) instead of a std::vector (24 bytes); that frees room for m_metadata.
    [[nodiscard]] static constexpr size_t packedBytesFor(uint8_t bitsPerVoxel) noexcept {
        return static_cast<size_t>(VOLUME) * bitsPerVoxel / 8U;
    }
    [[nodiscard]] static std::unique_ptr<uint8_t[]> allocatePacked(uint8_t bitsPerVoxel);
    [[nodiscard]] static std::unique_ptr<uint8_t[]> clonePacked(const uint8_t* src, uint8_t bitsPerVoxel);

    void setBlockStateRaw(int32_t lx, int32_t ly, int32_t lz, BlockStateId state) noexcept;
    void ensureLightDataAllocated() noexcept;
    void promote4To8() noexcept;
    void promote8To16() noexcept;

    // Memory layout: 56 bytes used, padded to one 64-byte L1 cache line
    uint8_t m_bitsPerVoxel{0};                             // 1 byte  (offset 0)
    uint8_t m_uniformSkyLight{15};                         // 1 byte  (offset 1)
    uint8_t m_uniformBlockLight{0};                        // 1 byte  (offset 2)
    uint8_t m_flags{0};                                    // 1 byte  (offset 3)
    BlockStateId m_singleBlock{0};                         // 2 bytes (offset 4)
    uint16_t m_reservedPadding{0};                         // 2 bytes (offset 6)
    std::vector<BlockStateId> m_palette;                     // 24 bytes (offset 8..31)
    std::unique_ptr<uint8_t[]> m_packedData{nullptr};         // 8 bytes  (offset 32..39)
    std::unique_ptr<DenseLightArray> m_lightData{nullptr};    // 8 bytes  (offset 40..47)
    std::unique_ptr<DenseNibbleArray> m_metadata{nullptr};    // 8 bytes  (offset 48..55)
};

static_assert(sizeof(PalettedSection) == 64, "PalettedSection must be exactly 64 bytes");
static_assert(sizeof(DenseNibbleArray) == PalettedSection::VOLUME / 2, "one nibble per voxel");
static_assert(alignof(PalettedSection) == 64, "PalettedSection must be aligned to 64 bytes");

} // namespace betacraft::voxel
