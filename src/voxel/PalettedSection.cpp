#include "voxel/PalettedSection.hpp"

namespace betacraft::world {

PalettedSection::PalettedSection(BlockId singleBlock, uint8_t uniformSky, uint8_t uniformBlock) noexcept
    : m_bitsPerVoxel(0),
      m_uniformSkyLight(uniformSky & 0x0FU),
      m_uniformBlockLight(uniformBlock & 0x0FU),
      m_flags(0),
      m_singleBlock(static_cast<BlockStateId>(singleBlock)),
      m_reservedPadding(0),
      m_palette(),
      m_packedData(nullptr),
      m_lightData(nullptr),
      m_metadata(nullptr) {}

PalettedSection::PalettedSection(BlockStateId singleBlock, uint8_t uniformSky, uint8_t uniformBlock) noexcept
    : m_bitsPerVoxel(0),
      m_uniformSkyLight(uniformSky & 0x0FU),
      m_uniformBlockLight(uniformBlock & 0x0FU),
      m_flags(0),
      m_singleBlock(singleBlock),
      m_reservedPadding(0),
      m_palette(),
      m_packedData(nullptr),
      m_lightData(nullptr),
      m_metadata(nullptr) {}

PalettedSection::PalettedSection(const PalettedSection& other)
    : m_bitsPerVoxel(other.m_bitsPerVoxel),
      m_uniformSkyLight(other.m_uniformSkyLight),
      m_uniformBlockLight(other.m_uniformBlockLight),
      m_flags(other.m_flags),
      m_singleBlock(other.m_singleBlock),
      m_reservedPadding(other.m_reservedPadding),
      m_palette(other.m_palette),
      m_packedData(clonePacked(other.m_packedData.get(), other.m_bitsPerVoxel)),
      m_lightData(other.m_lightData ? std::make_unique<DenseLightArray>(*other.m_lightData) : nullptr),
      m_metadata(other.m_metadata ? std::make_unique<DenseNibbleArray>(*other.m_metadata) : nullptr) {}

PalettedSection& PalettedSection::operator=(const PalettedSection& other) {
    if (this != &other) {
        m_bitsPerVoxel = other.m_bitsPerVoxel;
        m_uniformSkyLight = other.m_uniformSkyLight;
        m_uniformBlockLight = other.m_uniformBlockLight;
        m_flags = other.m_flags;
        m_singleBlock = other.m_singleBlock;
        m_reservedPadding = other.m_reservedPadding;
        m_palette = other.m_palette;
        m_packedData = clonePacked(other.m_packedData.get(), other.m_bitsPerVoxel);
        m_lightData = other.m_lightData ? std::make_unique<DenseLightArray>(*other.m_lightData) : nullptr;
        m_metadata = other.m_metadata ? std::make_unique<DenseNibbleArray>(*other.m_metadata) : nullptr;
    }
    return *this;
}

std::unique_ptr<uint8_t[]> PalettedSection::allocatePacked(uint8_t bitsPerVoxel) {
    return std::make_unique<uint8_t[]>(packedBytesFor(bitsPerVoxel)); // zero-filled
}

std::unique_ptr<uint8_t[]> PalettedSection::clonePacked(const uint8_t* src, uint8_t bitsPerVoxel) {
    if (src == nullptr || bitsPerVoxel == 0) {
        return nullptr;
    }
    auto copy = std::make_unique_for_overwrite<uint8_t[]>(packedBytesFor(bitsPerVoxel));
    std::copy_n(src, packedBytesFor(bitsPerVoxel), copy.get());
    return copy;
}

BlockId PalettedSection::getBlock(int32_t lx, int32_t ly, int32_t lz) const noexcept {
    return static_cast<BlockId>(getBlockState(lx, ly, lz));
}

BlockStateId PalettedSection::getBlockState(int32_t lx, int32_t ly, int32_t lz) const noexcept {
    if (!inBounds(lx, ly, lz)) {
        return static_cast<BlockStateId>(BlockId::Air);
    }
    if (m_bitsPerVoxel == 0) [[likely]] {
        return m_singleBlock;
    }
    const size_t idx = voxelIndex(lx, ly, lz);
    if (m_bitsPerVoxel == 4) {
        const uint8_t byteVal = m_packedData[idx >> 1U];
        const uint8_t palIdx = (idx & 1U) ? ((byteVal >> 4U) & 0x0FU) : (byteVal & 0x0FU);
        return m_palette[palIdx];
    }
    if (m_bitsPerVoxel == 8) {
        const uint8_t palIdx = m_packedData[idx];
        return m_palette[palIdx];
    }
    // 16-bit direct mode
    const auto* words = reinterpret_cast<const uint16_t*>(m_packedData.get());
    return words[idx];
}

void PalettedSection::setBlock(int32_t lx, int32_t ly, int32_t lz, BlockId id) noexcept {
    setBlock(lx, ly, lz, static_cast<BlockStateId>(id));
}

void PalettedSection::setBlock(int32_t lx, int32_t ly, int32_t lz, BlockStateId state) noexcept {
    if (!inBounds(lx, ly, lz)) {
        return;
    }
    if (m_metadata && getBlockState(lx, ly, lz) != state) {
        m_metadata->set(voxelIndex(lx, ly, lz), 0);
    }
    setBlockStateRaw(lx, ly, lz, state);
}

void PalettedSection::setBlockStateRaw(int32_t lx, int32_t ly, int32_t lz, BlockStateId state) noexcept {

    if (m_bitsPerVoxel == 0) {
        if (state == m_singleBlock) {
            return;
        }
        // Promote 0 -> 4 bits
        m_bitsPerVoxel = 4;
        m_palette.reserve(16);
        m_palette.push_back(m_singleBlock); // index 0
        m_palette.push_back(state);         // index 1
        m_packedData = allocatePacked(4); // 2048 bytes of 0 (all voxels point to index 0)

        const size_t idx = voxelIndex(lx, ly, lz);
        const size_t byteIdx = idx >> 1U;
        if ((idx & 1U) == 0) {
            m_packedData[byteIdx] = static_cast<uint8_t>((m_packedData[byteIdx] & 0xF0U) | 0x01U);
        } else {
            m_packedData[byteIdx] = static_cast<uint8_t>((m_packedData[byteIdx] & 0x0FU) | 0x10U);
        }
        return;
    }

    const size_t idx = voxelIndex(lx, ly, lz);

    if (m_bitsPerVoxel == 4) {
        const uint8_t currentByte = m_packedData[idx >> 1U];
        const uint8_t currentPalIdx = (idx & 1U) ? ((currentByte >> 4U) & 0x0FU) : (currentByte & 0x0FU);
        if (m_palette[currentPalIdx] == state) {
            return;
        }

        auto it = std::find(m_palette.begin(), m_palette.end(), state);
        uint8_t palIdx = 0;
        if (it != m_palette.end()) {
            palIdx = static_cast<uint8_t>(std::distance(m_palette.begin(), it));
        } else {
            if (m_palette.size() < 16) {
                palIdx = static_cast<uint8_t>(m_palette.size());
                m_palette.push_back(state);
            } else {
                promote4To8();
                // Now in 8-bit mode, add state
                palIdx = static_cast<uint8_t>(m_palette.size());
                m_palette.push_back(state);
                m_packedData[idx] = palIdx;
                return;
            }
        }

        const size_t byteIdx = idx >> 1U;
        if ((idx & 1U) == 0) {
            m_packedData[byteIdx] = static_cast<uint8_t>((m_packedData[byteIdx] & 0xF0U) | (palIdx & 0x0FU));
        } else {
            m_packedData[byteIdx] = static_cast<uint8_t>((m_packedData[byteIdx] & 0x0FU) | ((palIdx & 0x0FU) << 4U));
        }
        return;
    }

    if (m_bitsPerVoxel == 8) {
        const uint8_t currentPalIdx = m_packedData[idx];
        if (m_palette[currentPalIdx] == state) {
            return;
        }

        auto it = std::find(m_palette.begin(), m_palette.end(), state);
        uint8_t palIdx = 0;
        if (it != m_palette.end()) {
            palIdx = static_cast<uint8_t>(std::distance(m_palette.begin(), it));
            m_packedData[idx] = palIdx;
        } else {
            if (m_palette.size() < 256) {
                palIdx = static_cast<uint8_t>(m_palette.size());
                m_palette.push_back(state);
                m_packedData[idx] = palIdx;
            } else {
                promote8To16();
                auto* words = reinterpret_cast<uint16_t*>(m_packedData.get());
                words[idx] = state;
            }
        }
        return;
    }

    // 16-bit direct mode
    auto* words = reinterpret_cast<uint16_t*>(m_packedData.get());
    words[idx] = state;
}

void PalettedSection::promote4To8() noexcept {
    auto newPacked = allocatePacked(8);
    for (size_t i = 0; i < VOLUME; ++i) {
        const uint8_t byteVal = m_packedData[i >> 1U];
        newPacked[i] = (i & 1U) ? ((byteVal >> 4U) & 0x0FU) : (byteVal & 0x0FU);
    }
    m_packedData = std::move(newPacked);
    m_bitsPerVoxel = 8;
}

void PalettedSection::promote8To16() noexcept {
    auto newPacked = allocatePacked(16);
    auto* dest = reinterpret_cast<uint16_t*>(newPacked.get());
    for (size_t i = 0; i < VOLUME; ++i) {
        dest[i] = m_palette[m_packedData[i]];
    }
    m_packedData = std::move(newPacked);
    m_palette.clear();
    m_palette.shrink_to_fit();
    m_bitsPerVoxel = 16;
}

uint8_t PalettedSection::getSkyLight(int32_t lx, int32_t ly, int32_t lz) const noexcept {
    if (!inBounds(lx, ly, lz)) return 0;
    if (!m_lightData) [[likely]] {
        return m_uniformSkyLight;
    }
    return static_cast<uint8_t>((m_lightData->data[voxelIndex(lx, ly, lz)] >> 4U) & 0x0FU);
}

uint8_t PalettedSection::getBlockLight(int32_t lx, int32_t ly, int32_t lz) const noexcept {
    if (!inBounds(lx, ly, lz)) return 0;
    if (!m_lightData) [[likely]] {
        return m_uniformBlockLight;
    }
    return static_cast<uint8_t>(m_lightData->data[voxelIndex(lx, ly, lz)] & 0x0FU);
}

void PalettedSection::setSkyLight(int32_t lx, int32_t ly, int32_t lz, uint8_t val) noexcept {
    if (!inBounds(lx, ly, lz)) return;
    const uint8_t clamped = val & 0x0FU;
    if (!m_lightData) {
        if (clamped == m_uniformSkyLight) return;
        ensureLightDataAllocated();
    }
    const size_t idx = voxelIndex(lx, ly, lz);
    uint8_t& b = m_lightData->data[idx];
    b = static_cast<uint8_t>((b & 0x0FU) | (clamped << 4U));
}

void PalettedSection::setBlockLight(int32_t lx, int32_t ly, int32_t lz, uint8_t val) noexcept {
    if (!inBounds(lx, ly, lz)) return;
    const uint8_t clamped = val & 0x0FU;
    if (!m_lightData) {
        if (clamped == m_uniformBlockLight) return;
        ensureLightDataAllocated();
    }
    const size_t idx = voxelIndex(lx, ly, lz);
    uint8_t& b = m_lightData->data[idx];
    b = static_cast<uint8_t>((b & 0xF0U) | clamped);
}

void PalettedSection::setUniformLighting(uint8_t sky, uint8_t block) noexcept {
    m_uniformSkyLight = sky & 0x0FU;
    m_uniformBlockLight = block & 0x0FU;
    m_lightData.reset();
}

void PalettedSection::ensureLightDataAllocated() noexcept {
    if (!m_lightData) {
        m_lightData = std::make_unique<DenseLightArray>(m_uniformSkyLight, m_uniformBlockLight);
    }
}

uint8_t PalettedSection::getMetadata(int32_t lx, int32_t ly, int32_t lz) const noexcept {
    if (!inBounds(lx, ly, lz) || !m_metadata) [[likely]] {
        return 0;
    }
    return m_metadata->get(voxelIndex(lx, ly, lz));
}

void PalettedSection::setMetadata(int32_t lx, int32_t ly, int32_t lz, uint8_t meta) noexcept {
    if (!inBounds(lx, ly, lz)) {
        return;
    }
    const uint8_t nibble = meta & 0x0FU;
    if (!m_metadata) {
        if (nibble == 0) {
            return;
        }
        m_metadata = std::make_unique<DenseNibbleArray>();
    }
    m_metadata->set(voxelIndex(lx, ly, lz), nibble);
}

bool PalettedSection::hasMetadata() const noexcept {
    return m_metadata != nullptr;
}

bool PalettedSection::isHomogeneous() const noexcept {
    return m_bitsPerVoxel == 0 && m_lightData == nullptr && m_metadata == nullptr;
}

bool PalettedSection::hasLightData() const noexcept {
    return m_lightData != nullptr;
}

uint8_t PalettedSection::getBitsPerVoxel() const noexcept {
    return m_bitsPerVoxel;
}

size_t PalettedSection::getPaletteSize() const noexcept {
    return m_palette.size();
}

size_t PalettedSection::getMemoryFootprintBytes() const noexcept {
    size_t b = sizeof(PalettedSection);
    b += m_palette.capacity() * sizeof(BlockStateId);
    if (m_packedData) {
        b += packedBytesFor(m_bitsPerVoxel);
    }
    if (m_lightData) {
        b += sizeof(DenseLightArray);
    }
    if (m_metadata) {
        b += sizeof(DenseNibbleArray);
    }
    return b;
}

} // namespace betacraft::world
