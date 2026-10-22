#include "voxel/Chunk.hpp"

#include <algorithm>
#include <queue>

namespace betacraft::voxel {

Chunk::Chunk(core::ChunkCoord coord) noexcept
    : m_coord(coord) {
    for (auto& sec : m_sections) {
        sec = PalettedSection(BlockId::Air, 15, 0);
    }
    m_heightMap.fill(0);
    m_biomes.fill(BiomeId::Plains);
    m_grassTint.fill(glm::vec3(0.54f, 0.78f, 0.34f));
    m_foliageTint.fill(glm::vec3(0.45f, 0.72f, 0.27f));
}

BlockId Chunk::getBlock(int32_t x, int32_t y, int32_t z) const noexcept {
    if (!inBounds(x, y, z)) {
        return BlockId::Air;
    }
    const size_t secIdx = static_cast<size_t>(y >> 4);
    return m_sections[secIdx].getBlock(x, y & 15, z);
}

void Chunk::setBlock(int32_t x, int32_t y, int32_t z, BlockId id, bool markModified) noexcept {
    if (!inBounds(x, y, z)) {
        return;
    }
    const size_t secIdx = static_cast<size_t>(y >> 4);
    const int32_t ly = y & 15;
    if (m_sections[secIdx].getBlock(x, ly, z) != id) {
        m_sections[secIdx].setBlock(x, ly, z, id);
        markSectionsAround(y);
        if (markModified) {
            m_modified = true;
        }
    }
}

uint8_t Chunk::getSkyLight(int32_t x, int32_t y, int32_t z) const noexcept {
    if (y >= HEIGHT) return 15;
    if (!inBounds(x, y, z)) return 0;
    const size_t secIdx = static_cast<size_t>(y >> 4);
    return m_sections[secIdx].getSkyLight(x, y & 15, z);
}

uint8_t Chunk::getBlockLight(int32_t x, int32_t y, int32_t z) const noexcept {
    if (!inBounds(x, y, z)) return 0;
    const size_t secIdx = static_cast<size_t>(y >> 4);
    return m_sections[secIdx].getBlockLight(x, y & 15, z);
}

void Chunk::setSkyLight(int32_t x, int32_t y, int32_t z, uint8_t val) noexcept {
    if (!inBounds(x, y, z)) return;
    const size_t secIdx = static_cast<size_t>(y >> 4);
    m_sections[secIdx].setSkyLight(x, y & 15, z, val);
    markSectionsAround(y); // the mesher bakes light (and smooth-lighting corners) into vertices
}

void Chunk::setBlockLight(int32_t x, int32_t y, int32_t z, uint8_t val) noexcept {
    if (!inBounds(x, y, z)) return;
    const size_t secIdx = static_cast<size_t>(y >> 4);
    m_sections[secIdx].setBlockLight(x, y & 15, z, val);
    markSectionsAround(y);
}

void Chunk::markSectionsAround(int32_t y) noexcept {
    const int32_t lo = std::max(y - 1, 0) >> 4;
    const int32_t hi = std::min(y + 1, HEIGHT - 1) >> 4;
    for (int32_t s = lo; s <= hi; ++s) {
        markSectionDirty(static_cast<size_t>(s));
    }
}

uint8_t Chunk::getMetadata(int32_t x, int32_t y, int32_t z) const noexcept {
    if (!inBounds(x, y, z)) return 0;
    return m_sections[static_cast<size_t>(y >> 4)].getMetadata(x, y & 15, z);
}

void Chunk::setMetadata(int32_t x, int32_t y, int32_t z, uint8_t meta, bool markModified) noexcept {
    if (!inBounds(x, y, z)) return;
    PalettedSection& sec = m_sections[static_cast<size_t>(y >> 4)];
    const uint8_t nibble = meta & 0x0FU;
    if (sec.getMetadata(x, y & 15, z) == nibble) return;
    sec.setMetadata(x, y & 15, z, nibble);
    // No re-mesh: neither the section snapshot nor the meshers read metadata yet (liquids are
    // drawn as full cubes), so a level change cannot alter any geometry. Mark the sections
    // here once level-aware liquid meshing lands.
    if (markModified) {
        m_modified = true;
    }
}

uint8_t Chunk::getHeightAt(int32_t x, int32_t z) const noexcept {
    if (x < 0 || x >= WIDTH || z < 0 || z >= DEPTH) return 0;
    return m_heightMap[columnIndex(x, z)];
}

void Chunk::setColumnBiome(int32_t x, int32_t z, BiomeId biome, const glm::vec3& grassTint, const glm::vec3& foliageTint) noexcept {
    if (x < 0 || x >= WIDTH || z < 0 || z >= DEPTH) return;
    const size_t col = columnIndex(x, z);
    m_biomes[col] = biome;
    m_grassTint[col] = grassTint;
    m_foliageTint[col] = foliageTint;
}

BiomeId Chunk::getColumnBiome(int32_t x, int32_t z) const noexcept {
    if (x < 0 || x >= WIDTH || z < 0 || z >= DEPTH) return BiomeId::Plains;
    return m_biomes[columnIndex(x, z)];
}

glm::vec3 Chunk::getGrassTint(int32_t x, int32_t z) const noexcept {
    x = std::clamp(x, 0, WIDTH - 1);
    z = std::clamp(z, 0, DEPTH - 1);
    return m_grassTint[columnIndex(x, z)];
}

glm::vec3 Chunk::getFoliageTint(int32_t x, int32_t z) const noexcept {
    x = std::clamp(x, 0, WIDTH - 1);
    z = std::clamp(z, 0, DEPTH - 1);
    return m_foliageTint[columnIndex(x, z)];
}

void Chunk::recalculateLighting() noexcept {
    struct LightNode {
        int8_t x;
        int8_t y;
        int8_t z;
    };

    std::queue<LightNode> skyQueue;
    std::queue<LightNode> blockQueue;

    for (auto& sec : m_sections) {
        sec.setUniformLighting(0, 0);
    }

    // 1. Vertical sunlight cast & heightmap computation
    for (int32_t z = 0; z < DEPTH; ++z) {
        for (int32_t x = 0; x < WIDTH; ++x) {
            int32_t skyLight = 15;
            uint8_t highestOpaque = 0;
            for (int32_t y = HEIGHT - 1; y >= 0; --y) {
                const BlockId id = getBlock(x, y, z);
                const BlockDef& def = BlockRegistry::get(id);

                if (def.opaque && highestOpaque == 0) {
                    highestOpaque = static_cast<uint8_t>(y);
                }

                if (def.lightOpacity >= 15) {
                    skyLight = 0;
                } else if (def.lightOpacity > 0) {
                    skyLight = std::max(0, skyLight - static_cast<int32_t>(def.lightOpacity));
                }

                setSkyLight(x, y, z, static_cast<uint8_t>(skyLight));
                if (skyLight > 0) {
                    skyQueue.push({static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<int8_t>(z)});
                }

                if (def.lightEmission > 0) {
                    setBlockLight(x, y, z, def.lightEmission);
                    blockQueue.push({static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<int8_t>(z)});
                }
            }
            m_heightMap[columnIndex(x, z)] = highestOpaque;
        }
    }

    static constexpr int32_t dx[6] = {1, -1, 0, 0, 0, 0};
    static constexpr int32_t dy[6] = {0, 0, 1, -1, 0, 0};
    static constexpr int32_t dz[6] = {0, 0, 0, 0, 1, -1};

    // 2. Flood-fill Sky Light into overhangs and cave entrances
    while (!skyQueue.empty()) {
        const LightNode node = skyQueue.front();
        skyQueue.pop();

        const uint8_t currentLight = getSkyLight(node.x, node.y, node.z);
        if (currentLight <= 1) continue;

        for (int dir = 0; dir < 6; ++dir) {
            const int32_t nx = node.x + dx[dir];
            const int32_t ny = node.y + dy[dir];
            const int32_t nz = node.z + dz[dir];
            if (!inBounds(nx, ny, nz)) continue;

            const BlockDef& neighborDef = BlockRegistry::get(getBlock(nx, ny, nz));
            if (neighborDef.lightOpacity >= 15) continue;

            const int32_t stepCost = std::max<int32_t>(1, neighborDef.lightOpacity);
            const int32_t nextLight = static_cast<int32_t>(currentLight) - stepCost;
            if (nextLight > static_cast<int32_t>(getSkyLight(nx, ny, nz))) {
                setSkyLight(nx, ny, nz, static_cast<uint8_t>(nextLight));
                skyQueue.push({static_cast<int8_t>(nx), static_cast<int8_t>(ny), static_cast<int8_t>(nz)});
            }
        }
    }

    // 3. Flood-fill Block Light from Torches, Glowstone, Redstone Ore
    while (!blockQueue.empty()) {
        const LightNode node = blockQueue.front();
        blockQueue.pop();

        const uint8_t currentLight = getBlockLight(node.x, node.y, node.z);
        if (currentLight <= 1) continue;

        for (int dir = 0; dir < 6; ++dir) {
            const int32_t nx = node.x + dx[dir];
            const int32_t ny = node.y + dy[dir];
            const int32_t nz = node.z + dz[dir];
            if (!inBounds(nx, ny, nz)) continue;

            const BlockDef& neighborDef = BlockRegistry::get(getBlock(nx, ny, nz));
            if (neighborDef.lightOpacity >= 15) continue;

            const int32_t stepCost = std::max<int32_t>(1, neighborDef.lightOpacity);
            const int32_t nextLight = static_cast<int32_t>(currentLight) - stepCost;
            if (nextLight > static_cast<int32_t>(getBlockLight(nx, ny, nz))) {
                setBlockLight(nx, ny, nz, static_cast<uint8_t>(nextLight));
                blockQueue.push({static_cast<int8_t>(nx), static_cast<int8_t>(ny), static_cast<int8_t>(nz)});
            }
        }
    }

    setMeshDirty(true);
}

void Chunk::spreadBlockLight(int32_t x, int32_t y, int32_t z, uint8_t light) noexcept {
    if (!inBounds(x, y, z)) return;
    const BlockDef& initialDef = BlockRegistry::get(getBlock(x, y, z));
    if (initialDef.lightOpacity >= 15) return;
    if (light <= getBlockLight(x, y, z)) return;

    struct LightNode {
        int8_t x;
        int8_t y;
        int8_t z;
    };

    std::queue<LightNode> queue;
    setBlockLight(x, y, z, light);
    queue.push({static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<int8_t>(z)});

    static constexpr int32_t dx[6] = {1, -1, 0, 0, 0, 0};
    static constexpr int32_t dy[6] = {0, 0, 1, -1, 0, 0};
    static constexpr int32_t dz[6] = {0, 0, 0, 0, 1, -1};

    while (!queue.empty()) {
        const LightNode node = queue.front();
        queue.pop();

        const uint8_t currentLight = getBlockLight(node.x, node.y, node.z);
        if (currentLight <= 1) continue;

        for (int dir = 0; dir < 6; ++dir) {
            const int32_t nx = node.x + dx[dir];
            const int32_t ny = node.y + dy[dir];
            const int32_t nz = node.z + dz[dir];
            if (!inBounds(nx, ny, nz)) continue;

            const BlockDef& neighborDef = BlockRegistry::get(getBlock(nx, ny, nz));
            if (neighborDef.lightOpacity >= 15) continue;

            const int32_t stepCost = std::max<int32_t>(1, neighborDef.lightOpacity);
            const int32_t nextLight = static_cast<int32_t>(currentLight) - stepCost;
            if (nextLight > static_cast<int32_t>(getBlockLight(nx, ny, nz))) {
                setBlockLight(nx, ny, nz, static_cast<uint8_t>(nextLight));
                queue.push({static_cast<int8_t>(nx), static_cast<int8_t>(ny), static_cast<int8_t>(nz)});
            }
        }
    }
    setMeshDirty(true);
}

void Chunk::spreadSkyLight(int32_t x, int32_t y, int32_t z, uint8_t light) noexcept {
    if (!inBounds(x, y, z)) return;
    const BlockDef& initialDef = BlockRegistry::get(getBlock(x, y, z));
    if (initialDef.lightOpacity >= 15) return;
    if (light <= getSkyLight(x, y, z)) return;

    struct LightNode {
        int8_t x;
        int8_t y;
        int8_t z;
    };

    std::queue<LightNode> queue;
    setSkyLight(x, y, z, light);
    queue.push({static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<int8_t>(z)});

    static constexpr int32_t dx[6] = {1, -1, 0, 0, 0, 0};
    static constexpr int32_t dy[6] = {0, 0, 1, -1, 0, 0};
    static constexpr int32_t dz[6] = {0, 0, 0, 0, 1, -1};

    while (!queue.empty()) {
        const LightNode node = queue.front();
        queue.pop();

        const uint8_t currentLight = getSkyLight(node.x, node.y, node.z);
        if (currentLight <= 1) continue;

        for (int dir = 0; dir < 6; ++dir) {
            const int32_t nx = node.x + dx[dir];
            const int32_t ny = node.y + dy[dir];
            const int32_t nz = node.z + dz[dir];
            if (!inBounds(nx, ny, nz)) continue;

            const BlockDef& neighborDef = BlockRegistry::get(getBlock(nx, ny, nz));
            if (neighborDef.lightOpacity >= 15) continue;

            const int32_t stepCost = std::max<int32_t>(1, neighborDef.lightOpacity);
            const int32_t nextLight = static_cast<int32_t>(currentLight) - stepCost;
            if (nextLight > static_cast<int32_t>(getSkyLight(nx, ny, nz))) {
                setSkyLight(nx, ny, nz, static_cast<uint8_t>(nextLight));
                queue.push({static_cast<int8_t>(nx), static_cast<int8_t>(ny), static_cast<int8_t>(nz)});
            }
        }
    }
    setMeshDirty(true);
}

namespace {

// Format: "BC17" + version byte, then (value, runLength u16 LE) runs over the 32768 voxels in
// x-fastest, z, y-slowest order. Version 1 holds block ids only; version 2 appends a second
// run stream of metadata nibbles. Chunks without metadata keep writing version 1, so the
// common case stays byte-identical to saves made before fluid levels existed.
constexpr uint8_t RLE_VERSION_BLOCKS = 1;
constexpr uint8_t RLE_VERSION_METADATA = 2;
constexpr size_t RLE_HEADER_SIZE = 5;

struct LinearPos {
    int32_t x;
    int32_t y;
    int32_t z;
};

constexpr LinearPos linearToPos(size_t idx) noexcept {
    const size_t temp = idx / Chunk::WIDTH;
    return {static_cast<int32_t>(idx % Chunk::WIDTH), static_cast<int32_t>(temp / Chunk::DEPTH),
            static_cast<int32_t>(temp % Chunk::DEPTH)};
}

template <typename ValueAt>
void appendRuns(std::vector<uint8_t>& out, ValueAt&& valueAt) {
    size_t i = 0;
    while (i < static_cast<size_t>(Chunk::VOLUME)) {
        const uint8_t current = valueAt(i);
        uint16_t runLength = 1;
        while (i + runLength < static_cast<size_t>(Chunk::VOLUME) && valueAt(i + runLength) == current &&
               runLength < 65535U) {
            ++runLength;
        }
        out.push_back(current);
        out.push_back(static_cast<uint8_t>(runLength & 0xFFU));
        out.push_back(static_cast<uint8_t>((runLength >> 8U) & 0xFFU));
        i += runLength;
    }
}

// Decodes one complete run stream starting at `pos`; false on malformed input. `store` is
// called once per run and returns false to reject the value.
template <typename StoreRun>
bool readRuns(std::span<const uint8_t> data, size_t& pos, StoreRun&& store) {
    size_t voxel = 0;
    while (pos + 2 < data.size() && voxel < static_cast<size_t>(Chunk::VOLUME)) {
        const uint8_t value = data[pos];
        const uint16_t runLength = static_cast<uint16_t>(data[pos + 1]) |
                                   static_cast<uint16_t>(static_cast<uint16_t>(data[pos + 2]) << 8U);
        pos += 3;
        if (runLength == 0 || voxel + runLength > static_cast<size_t>(Chunk::VOLUME)) {
            return false;
        }
        if (!store(voxel, runLength, value)) {
            return false;
        }
        voxel += runLength;
    }
    return voxel == static_cast<size_t>(Chunk::VOLUME);
}

} // namespace

std::vector<uint8_t> Chunk::serializeRLE() const {
    const bool withMetadata =
        std::any_of(m_sections.begin(), m_sections.end(), [](const PalettedSection& s) { return s.hasMetadata(); });

    std::vector<uint8_t> out;
    out.reserve(4096);
    out.push_back('B');
    out.push_back('C');
    out.push_back('1');
    out.push_back('7');
    out.push_back(withMetadata ? RLE_VERSION_METADATA : RLE_VERSION_BLOCKS);

    appendRuns(out, [this](size_t idx) {
        const LinearPos p = linearToPos(idx);
        return static_cast<uint8_t>(getBlock(p.x, p.y, p.z));
    });
    if (withMetadata) {
        appendRuns(out, [this](size_t idx) {
            const LinearPos p = linearToPos(idx);
            return getMetadata(p.x, p.y, p.z);
        });
    }
    return out;
}

bool Chunk::deserializeRLE(std::span<const uint8_t> data) {
    if (data.size() < 8) return false;
    if (data[0] != 'B' || data[1] != 'C' || data[2] != '1' || data[3] != '7') {
        return false;
    }
    const uint8_t version = data[4];
    if (version != RLE_VERSION_BLOCKS && version != RLE_VERSION_METADATA) {
        return false;
    }

    for (auto& sec : m_sections) {
        sec = PalettedSection(BlockId::Air, 15, 0);
    }

    size_t pos = RLE_HEADER_SIZE;
    const bool blocksOk = readRuns(data, pos, [this](size_t first, uint16_t count, uint8_t value) {
        const auto id = static_cast<BlockId>(value);
        for (size_t k = first; k < first + count; ++k) {
            const LinearPos p = linearToPos(k);
            setBlock(p.x, p.y, p.z, id, false);
        }
        return true;
    });
    if (!blocksOk) {
        return false;
    }

    if (version == RLE_VERSION_METADATA) {
        const bool metaOk = readRuns(data, pos, [this](size_t first, uint16_t count, uint8_t value) {
            if (value > 0x0FU) {
                return false;
            }
            if (value != 0) { // zero is implicit: keeps the lazy arrays unallocated
                for (size_t k = first; k < first + count; ++k) {
                    const LinearPos p = linearToPos(k);
                    setMetadata(p.x, p.y, p.z, value, false);
                }
            }
            return true;
        });
        if (!metaOk) {
            return false;
        }
    }

    recalculateLighting();
    m_modified = false;
    m_playerTouched = true; // Only edited chunks are ever saved
    setMeshDirty(true);
    return true;
}

} // namespace betacraft::voxel
