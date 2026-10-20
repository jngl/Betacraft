#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <queue>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/Config.hpp"
#include "core/Types.hpp"
#include "physics/Player.hpp"
#include "render/GLHeaders.hpp"
#include "voxel/Block.hpp"

// Check for presence of production implementation headers
#if __has_include("voxel/PalettedSection.hpp")
#include "voxel/PalettedSection.hpp"
#define BETACRAFT_HAS_PROD_PALETTED_SECTION 1
#else
#define BETACRAFT_HAS_PROD_PALETTED_SECTION 0
#endif

#if __has_include("render/PackedChunkVertex.hpp")
#include "render/PackedChunkVertex.hpp"
#define BETACRAFT_HAS_PROD_PACKED_CHUNK_VERTEX 1
#else
#define BETACRAFT_HAS_PROD_PACKED_CHUNK_VERTEX 0
#endif

#if __has_include("world/DifferentialLightingEngine.hpp")
#include "world/DifferentialLightingEngine.hpp"
#define BETACRAFT_HAS_PROD_DIFF_LIGHTING 1
#else
#define BETACRAFT_HAS_PROD_DIFF_LIGHTING 0
#endif

#if __has_include("core/MpscRingBuffer.hpp")
#include "core/MpscRingBuffer.hpp"
#define BETACRAFT_HAS_PROD_MPSC_RING_BUFFER 1
#else
#define BETACRAFT_HAS_PROD_MPSC_RING_BUFFER 0
#endif

#if __has_include("ui/InputManager.hpp")
#include "ui/InputManager.hpp"
#define BETACRAFT_HAS_PROD_INPUT_MANAGER 1
#else
#define BETACRAFT_HAS_PROD_INPUT_MANAGER 0
#endif

#if __has_include("ui/ScreenState.hpp")
#include "ui/ScreenState.hpp"
#define BETACRAFT_HAS_PROD_SCREEN_STATE 1
#else
#define BETACRAFT_HAS_PROD_SCREEN_STATE 0
#endif

namespace betacraft::test_contracts {

// ============================================================================
// 1. PackedChunkVertex Contract & Specification Model (Fiche 1, P1.2)
// ============================================================================

struct alignas(8) ContractPackedChunkVertex {
    uint32_t word0{0};
    uint32_t word1{0};

    [[nodiscard]] static constexpr uint32_t packWord0(
        uint32_t x, uint32_t y, uint32_t z,
        uint32_t normalIdx, uint32_t ao, uint32_t uvCorner, uint32_t matLayer
    ) noexcept {
        return (x & 0x1Fu) |
               ((y & 0x1Fu) << 5u) |
               ((z & 0x1Fu) << 10u) |
               ((normalIdx & 0x07u) << 15u) |
               ((ao & 0x03u) << 18u) |
               ((uvCorner & 0x03u) << 20u) |
               ((matLayer & 0x3FFu) << 22u);
    }

    [[nodiscard]] static constexpr uint32_t packWord1(
        uint32_t skyLight, uint32_t blockLight, uint32_t biomeTint,
        uint32_t animFrame, bool sway, bool translucent, uint32_t reserved = 0
    ) noexcept {
        return (skyLight & 0x0Fu) |
               ((blockLight & 0x0Fu) << 4u) |
               ((biomeTint & 0xFFu) << 8u) |
               ((animFrame & 0x1Fu) << 16u) |
               ((static_cast<uint32_t>(sway) & 0x01u) << 21u) |
               ((static_cast<uint32_t>(translucent) & 0x01u) << 22u) |
               ((reserved & 0x1FFu) << 23u);
    }

    // Unpack helpers for validation
    [[nodiscard]] static constexpr uint32_t unpackX(uint32_t w0) noexcept { return w0 & 0x1Fu; }
    [[nodiscard]] static constexpr uint32_t unpackY(uint32_t w0) noexcept { return (w0 >> 5u) & 0x1Fu; }
    [[nodiscard]] static constexpr uint32_t unpackZ(uint32_t w0) noexcept { return (w0 >> 10u) & 0x1Fu; }
    [[nodiscard]] static constexpr uint32_t unpackNormal(uint32_t w0) noexcept { return (w0 >> 15u) & 0x07u; }
    [[nodiscard]] static constexpr uint32_t unpackAO(uint32_t w0) noexcept { return (w0 >> 18u) & 0x03u; }
    [[nodiscard]] static constexpr uint32_t unpackUVCorner(uint32_t w0) noexcept { return (w0 >> 20u) & 0x03u; }
    [[nodiscard]] static constexpr uint32_t unpackMatLayer(uint32_t w0) noexcept { return (w0 >> 22u) & 0x3FFu; }

    [[nodiscard]] static constexpr uint32_t unpackSkyLight(uint32_t w1) noexcept { return w1 & 0x0Fu; }
    [[nodiscard]] static constexpr uint32_t unpackBlockLight(uint32_t w1) noexcept { return (w1 >> 4u) & 0x0Fu; }
    [[nodiscard]] static constexpr uint32_t unpackBiomeTint(uint32_t w1) noexcept { return (w1 >> 8u) & 0xFFu; }
    [[nodiscard]] static constexpr uint32_t unpackAnimFrame(uint32_t w1) noexcept { return (w1 >> 16u) & 0x1Fu; }
    [[nodiscard]] static constexpr bool unpackSway(uint32_t w1) noexcept { return ((w1 >> 21u) & 0x01u) != 0; }
    [[nodiscard]] static constexpr bool unpackTranslucent(uint32_t w1) noexcept { return ((w1 >> 22u) & 0x01u) != 0; }
    [[nodiscard]] static constexpr uint32_t unpackReserved(uint32_t w1) noexcept { return (w1 >> 23u) & 0x1FFu; }
};

static_assert(sizeof(ContractPackedChunkVertex) == 8, "PackedChunkVertex must be exactly 8 bytes");
static_assert(alignof(ContractPackedChunkVertex) == 8, "PackedChunkVertex must be 8-byte aligned");
static_assert(std::is_trivially_copyable_v<ContractPackedChunkVertex>, "PackedChunkVertex must be trivially copyable");

// Static Shared EBO generation and validation
inline std::vector<uint32_t> generateSharedEboPattern(size_t quadCount) {
    std::vector<uint32_t> ebo;
    ebo.reserve(quadCount * 6);
    for (size_t q = 0; q < quadCount; ++q) {
        const uint32_t base = static_cast<uint32_t>(q * 4);
        ebo.push_back(base + 0);
        ebo.push_back(base + 1);
        ebo.push_back(base + 2);
        ebo.push_back(base + 0);
        ebo.push_back(base + 2);
        ebo.push_back(base + 3);
    }
    return ebo;
}

inline bool validateEboPattern(std::span<const uint32_t> indices, size_t quadCount) {
    if (indices.size() != quadCount * 6) return false;
    for (size_t q = 0; q < quadCount; ++q) {
        const uint32_t base = static_cast<uint32_t>(q * 4);
        const size_t offset = q * 6;
        if (indices[offset + 0] != base + 0 ||
            indices[offset + 1] != base + 1 ||
            indices[offset + 2] != base + 2 ||
            indices[offset + 3] != base + 0 ||
            indices[offset + 4] != base + 2 ||
            indices[offset + 5] != base + 3) {
            return false;
        }
    }
    return true;
}

// Fractal UV Tiling planar mapping function
inline glm::vec2 computePlanarUV(glm::vec3 worldPos, uint32_t normalIdx) {
    glm::vec2 planar(0.0f);
    if (normalIdx <= 1u) { // +Y or -Y
        planar = glm::vec2(worldPos.x, worldPos.z);
    } else if (normalIdx >= 4u) { // +X or -X
        planar = glm::vec2(worldPos.z, worldPos.y);
    } else { // +Z or -Z
        planar = glm::vec2(worldPos.x, worldPos.y);
    }
    // fract(p) = p - floor(p)
    return planar - glm::floor(planar);
}

// ============================================================================
// 2. PalettedSection & DenseLightArray Contract (Fiche 2, P3.1)
// ============================================================================

using BlockStateId = uint16_t;

struct alignas(32) ContractDenseLightArray {
    std::array<uint8_t, 4096> data;

    ContractDenseLightArray(uint8_t uniformSky = 15, uint8_t uniformBlock = 0) noexcept {
        const uint8_t packedVal = static_cast<uint8_t>(((uniformSky & 0x0F) << 4) | (uniformBlock & 0x0F));
        data.fill(packedVal);
    }
};

class alignas(64) ContractPalettedSection {
public:
    static constexpr int32_t SIZE = 16;
    static constexpr int32_t VOLUME = 4096;

    ContractPalettedSection() noexcept = default;

    ContractPalettedSection(const ContractPalettedSection& other)
        : m_bitsPerVoxel(other.m_bitsPerVoxel),
          m_uniformSkyLight(other.m_uniformSkyLight),
          m_uniformBlockLight(other.m_uniformBlockLight),
          m_flags(other.m_flags),
          m_singleBlock(other.m_singleBlock),
          m_reservedPadding(other.m_reservedPadding),
          m_palette(other.m_palette),
          m_packedData(other.m_packedData),
          m_lightData(other.m_lightData ? std::make_unique<ContractDenseLightArray>(*other.m_lightData) : nullptr) {}

    ContractPalettedSection& operator=(const ContractPalettedSection& other) {
        if (this != &other) {
            m_bitsPerVoxel = other.m_bitsPerVoxel;
            m_uniformSkyLight = other.m_uniformSkyLight;
            m_uniformBlockLight = other.m_uniformBlockLight;
            m_flags = other.m_flags;
            m_singleBlock = other.m_singleBlock;
            m_reservedPadding = other.m_reservedPadding;
            m_palette = other.m_palette;
            m_packedData = other.m_packedData;
            m_lightData = other.m_lightData ? std::make_unique<ContractDenseLightArray>(*other.m_lightData) : nullptr;
        }
        return *this;
    }

    [[nodiscard]] static constexpr size_t voxelIndex(int32_t x, int32_t y, int32_t z) noexcept {
        return static_cast<size_t>((y * SIZE + z) * SIZE + x);
    }

    [[nodiscard]] BlockStateId getBlock(int32_t lx, int32_t ly, int32_t lz) const noexcept {
        if (m_bitsPerVoxel == 0) return m_singleBlock;
        const size_t idx = voxelIndex(lx, ly, lz);
        if (m_bitsPerVoxel == 4) {
            const uint8_t byteVal = m_packedData[idx >> 1];
            const size_t palIdx = (idx & 1) ? ((byteVal >> 4) & 0x0F) : (byteVal & 0x0F);
            return palIdx < m_palette.size() ? m_palette[palIdx] : 0;
        } else if (m_bitsPerVoxel == 8) {
            const size_t palIdx = m_packedData[idx];
            return palIdx < m_palette.size() ? m_palette[palIdx] : 0;
        }
        const auto* raw16 = reinterpret_cast<const uint16_t*>(m_packedData.data());
        return raw16[idx];
    }

    void setBlock(int32_t lx, int32_t ly, int32_t lz, BlockStateId state) noexcept {
        const size_t idx = voxelIndex(lx, ly, lz);
        if (m_bitsPerVoxel == 0) {
            if (m_singleBlock == state) return;
            // Upgrade 0 -> 4 bits
            m_bitsPerVoxel = 4;
            m_palette = {m_singleBlock, state};
            m_packedData.assign(VOLUME / 2, 0); // All 0 except idx
            setPackedIndex(idx, 1);
            return;
        }

        // Find in existing palette
        for (size_t i = 0; i < m_palette.size(); ++i) {
            if (m_palette[i] == state) {
                setPackedIndex(idx, i);
                return;
            }
        }

        // Palette does not contain state; check if upgrade needed
        if (m_bitsPerVoxel == 4) {
            if (m_palette.size() < 16) {
                const size_t newPalIdx = m_palette.size();
                m_palette.push_back(state);
                setPackedIndex(idx, newPalIdx);
            } else {
                // Upgrade 4 -> 8 bits
                upgradeTo8Bits();
                const size_t newPalIdx = m_palette.size();
                m_palette.push_back(state);
                m_packedData[idx] = static_cast<uint8_t>(newPalIdx);
            }
        } else if (m_bitsPerVoxel == 8) {
            if (m_palette.size() < 256) {
                const size_t newPalIdx = m_palette.size();
                m_palette.push_back(state);
                m_packedData[idx] = static_cast<uint8_t>(newPalIdx);
            } else {
                // Upgrade 8 -> 16 bits (direct BlockStateId)
                upgradeTo16Bits();
                auto* raw16 = reinterpret_cast<uint16_t*>(m_packedData.data());
                raw16[idx] = state;
            }
        } else {
            // 16 bits direct
            auto* raw16 = reinterpret_cast<uint16_t*>(m_packedData.data());
            raw16[idx] = state;
        }
    }

    [[nodiscard]] uint8_t getSkyLight(int32_t lx, int32_t ly, int32_t lz) const noexcept {
        if (!m_lightData) return m_uniformSkyLight;
        return (m_lightData->data[voxelIndex(lx, ly, lz)] >> 4) & 0x0F;
    }

    [[nodiscard]] uint8_t getBlockLight(int32_t lx, int32_t ly, int32_t lz) const noexcept {
        if (!m_lightData) return m_uniformBlockLight;
        return m_lightData->data[voxelIndex(lx, ly, lz)] & 0x0F;
    }

    void setSkyLight(int32_t lx, int32_t ly, int32_t lz, uint8_t val) noexcept {
        const uint8_t clamped = val & 0x0F;
        if (!m_lightData) {
            if (clamped == m_uniformSkyLight) return;
            ensureLightDataAllocated();
        }
        const size_t idx = voxelIndex(lx, ly, lz);
        uint8_t& byteRef = m_lightData->data[idx];
        byteRef = static_cast<uint8_t>((byteRef & 0x0F) | (clamped << 4));
    }

    void setBlockLight(int32_t lx, int32_t ly, int32_t lz, uint8_t val) noexcept {
        const uint8_t clamped = val & 0x0F;
        if (!m_lightData) {
            if (clamped == m_uniformBlockLight) return;
            ensureLightDataAllocated();
        }
        const size_t idx = voxelIndex(lx, ly, lz);
        uint8_t& byteRef = m_lightData->data[idx];
        byteRef = static_cast<uint8_t>((byteRef & 0xF0) | clamped);
    }

    void setUniformLighting(uint8_t sky, uint8_t block) noexcept {
        m_uniformSkyLight = sky & 0x0F;
        m_uniformBlockLight = block & 0x0F;
        m_lightData.reset(); // frees 4096 bytes back to nullptr
    }

    [[nodiscard]] bool isHomogeneous() const noexcept {
        return m_bitsPerVoxel == 0 && m_lightData == nullptr;
    }

    [[nodiscard]] uint8_t getBitsPerVoxel() const noexcept { return m_bitsPerVoxel; }
    [[nodiscard]] bool hasLightData() const noexcept { return m_lightData != nullptr; }
    [[nodiscard]] size_t getPaletteSize() const noexcept { return m_palette.size(); }

    [[nodiscard]] size_t getMemoryFootprintBytes() const noexcept {
        size_t b = sizeof(ContractPalettedSection);
        b += m_palette.capacity() * sizeof(BlockStateId);
        b += m_packedData.capacity() * sizeof(uint8_t);
        if (m_lightData) b += sizeof(ContractDenseLightArray);
        return b;
    }

private:
    void ensureLightDataAllocated() noexcept {
        if (!m_lightData) {
            m_lightData = std::make_unique<ContractDenseLightArray>(m_uniformSkyLight, m_uniformBlockLight);
        }
    }

    void setPackedIndex(size_t idx, size_t palIdx) noexcept {
        if (m_bitsPerVoxel == 4) {
            const size_t byteIdx = idx >> 1;
            const uint8_t p = static_cast<uint8_t>(palIdx & 0x0F);
            if (idx & 1) {
                m_packedData[byteIdx] = static_cast<uint8_t>((m_packedData[byteIdx] & 0x0F) | (p << 4));
            } else {
                m_packedData[byteIdx] = static_cast<uint8_t>((m_packedData[byteIdx] & 0xF0) | p);
            }
        } else if (m_bitsPerVoxel == 8) {
            m_packedData[idx] = static_cast<uint8_t>(palIdx);
        }
    }

    void upgradeTo8Bits() noexcept {
        std::vector<uint8_t> newPacked(VOLUME);
        for (size_t i = 0; i < VOLUME; ++i) {
            const uint8_t byteVal = m_packedData[i >> 1];
            newPacked[i] = (i & 1) ? ((byteVal >> 4) & 0x0F) : (byteVal & 0x0F);
        }
        m_packedData = std::move(newPacked);
        m_bitsPerVoxel = 8;
    }

    void upgradeTo16Bits() noexcept {
        std::vector<uint8_t> newPacked(VOLUME * sizeof(uint16_t));
        auto* dest = reinterpret_cast<uint16_t*>(newPacked.data());
        for (size_t i = 0; i < VOLUME; ++i) {
            dest[i] = m_palette[m_packedData[i]];
        }
        m_packedData = std::move(newPacked);
        m_palette.clear();
        m_bitsPerVoxel = 16;
    }

    uint8_t m_bitsPerVoxel{0};                             // 1 byte
    uint8_t m_uniformSkyLight{15};                         // 1 byte
    uint8_t m_uniformBlockLight{0};                        // 1 byte
    uint8_t m_flags{0};                                    // 1 byte
    BlockStateId m_singleBlock{0};                         // 2 bytes
    uint16_t m_reservedPadding{0};                         // 2 bytes
    std::vector<BlockStateId> m_palette;                  // 24 bytes
    std::vector<uint8_t> m_packedData;                    // 24 bytes
    std::unique_ptr<ContractDenseLightArray> m_lightData{nullptr}; // 8 bytes
};

static_assert(sizeof(ContractPalettedSection) <= 64, "PalettedSection must be <= 64 bytes");
static_assert(alignof(ContractPalettedSection) == 64, "PalettedSection must be alignas(64)");

// ============================================================================
// 3. Certified BFS Differential Lighting Math Model (Fiche 2, P3.2)
// ============================================================================

// Mathematical analytical formula for discrete Manhattan sphere shell and volume:
// S(r) = 4*r^2 + 2  for r >= 1, S(0) = 1
// N(R) = (2R + 1) * (2R^2 + 2R + 3) / 3
inline constexpr size_t analyticalManhattanShell(size_t r) noexcept {
    if (r == 0) return 1;
    return 4 * r * r + 2;
}

inline constexpr size_t analyticalManhattanVolume(size_t R) noexcept {
    return ((2 * R + 1) * (2 * R * R + 2 * R + 3)) / 3;
}

static_assert(analyticalManhattanVolume(15) == 4991, "Manhattan volume for R=15 must be 4,991 voxels");
static_assert(analyticalManhattanVolume(0) == 1, "Manhattan volume for R=0 must be 1 voxel");
static_assert(analyticalManhattanVolume(1) == 7, "Manhattan volume for R=1 must be 7 voxels (center + 6 neighbors)");

// Discrete BFS lattice verification function on 3D grid
inline size_t computeDiscreteManhattanVolumeBFS(int maxRadius) {
    std::queue<glm::ivec3> queue;
    std::unordered_map<int64_t, int> visitedDist;

    auto encodeKey = [](const glm::ivec3& p) -> int64_t {
        // Shift to positive values assuming coordinates in [-32, 32]
        const int64_t x = static_cast<int64_t>(p.x + 100);
        const int64_t y = static_cast<int64_t>(p.y + 100);
        const int64_t z = static_cast<int64_t>(p.z + 100);
        return (x << 40) | (y << 20) | z;
    };

    const glm::ivec3 start(0, 0, 0);
    queue.push(start);
    visitedDist[encodeKey(start)] = 0;

    const std::array<glm::ivec3, 6> offsets = {
        glm::ivec3( 1, 0, 0), glm::ivec3(-1, 0, 0),
        glm::ivec3( 0, 1, 0), glm::ivec3( 0,-1, 0),
        glm::ivec3( 0, 0, 1), glm::ivec3( 0, 0,-1)
    };

    while (!queue.empty()) {
        const glm::ivec3 curr = queue.front();
        queue.pop();

        const int dist = visitedDist[encodeKey(curr)];
        if (dist >= maxRadius) continue;

        for (const auto& off : offsets) {
            const glm::ivec3 next = curr + off;
            const int64_t key = encodeKey(next);
            if (visitedDist.find(key) == visitedDist.end()) {
                visitedDist[key] = dist + 1;
                queue.push(next);
            }
        }
    }

    return visitedDist.size();
}

// ============================================================================
// 4. Lockless MpscRingBuffer Contract (Fiche 5, P4.1)
// ============================================================================

template<typename T, size_t Capacity = 512>
class ContractMpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr size_t BufferMask = Capacity - 1;

    struct Cell {
        std::atomic<size_t> sequence{0};
        T data{};
    };

public:
    ContractMpscRingBuffer() noexcept {
        for (size_t i = 0; i < Capacity; ++i) {
            m_buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] bool push(const T& item) noexcept {
        size_t tail = m_tail.load(std::memory_order_relaxed);
        while (true) {
            Cell& cell = m_buffer[tail & BufferMask];
            const size_t seq = cell.sequence.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(tail);

            if (dif == 0) {
                if (m_tail.compare_exchange_weak(tail, tail + 1, std::memory_order_relaxed)) {
                    cell.data = item;
                    cell.sequence.store(tail + 1, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                return false; // Buffer full
            } else {
                tail = m_tail.load(std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] bool pop(T& item) noexcept {
        Cell& cell = m_buffer[m_head & BufferMask];
        const size_t seq = cell.sequence.load(std::memory_order_acquire);
        const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(m_head + 1);

        if (dif == 0) {
            item = std::move(cell.data);
            cell.sequence.store(m_head + Capacity, std::memory_order_release);
            m_head = (m_head + 1);
            return true;
        }
        return false; // Buffer empty
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept { return Capacity; }

private:
    alignas(64) std::array<Cell, Capacity> m_buffer;
    alignas(64) std::atomic<size_t> m_tail{0};
    alignas(64) size_t m_head{0};
};

// ============================================================================
// 5. Fixed Timestep Physics Simulator (Fiedler P1.6)
// ============================================================================

struct PhysicsSimulationResult {
    glm::vec3 finalPosition{0.0f};
    glm::vec3 finalVelocity{0.0f};
    size_t tickCount{0};
};

inline PhysicsSimulationResult simulateFiedlerPhysics(
    float totalTime,
    float frameDt,
    float fixedDt = 1.0f / 60.0f,
    float maxAccumulator = 0.25f,
    glm::vec3 initialPos = glm::vec3(0.0f),
    glm::vec3 initialVel = glm::vec3(0.0f),
    glm::vec3 acceleration = glm::vec3(0.0f, -28.0f, 0.0f) // Minecraft gravity ~28.0 m/s^2
) {
    if (totalTime <= 0.0f || frameDt <= 0.0f) {
        return {initialPos, initialVel, 0};
    }
    float accumulator = 0.0f;
    glm::vec3 pos = initialPos;
    glm::vec3 vel = initialVel;
    size_t ticks = 0;

    const int totalFrames = static_cast<int>(std::round(totalTime / frameDt));
    for (int f = 0; f < totalFrames; ++f) {
        accumulator += frameDt;

        if (accumulator > maxAccumulator) {
            accumulator = maxAccumulator; // Prevent spiral of death
        }

        while (accumulator >= fixedDt - 1e-5f) {
            // Discrete Euler / Verlet tick
            vel += acceleration * fixedDt;
            pos += vel * fixedDt;
            accumulator -= fixedDt;
            if (accumulator < 0.0f) accumulator = 0.0f;
            ++ticks;
        }
    }

    return {pos, vel, ticks};
}

// ============================================================================
// 6. MSDF Font Math & Virtual GUI Scaling (P4.3)
// ============================================================================

inline float msdfMedian(float r, float g, float b) noexcept {
    return std::max(std::min(r, g), std::min(std::max(r, g), b));
}

inline float msdfEvaluateFragment(glm::vec3 msd, float pxRange = 4.0f) noexcept {
    const float sd = msdfMedian(msd.r, msd.g, msd.b) - 0.5f;
    const float screenPxDistance = pxRange * sd;
    return std::clamp(screenPxDistance + 0.5f, 0.0f, 1.0f);
}

enum class GuiScalePreference {
    Auto = 0,
    Scale1x = 1,
    Scale2x = 2,
    Scale3x = 3,
    Scale4x = 4
};

inline int calculateVirtualGuiScale(int windowWidth, int windowHeight, GuiScalePreference pref) noexcept {
    if (pref != GuiScalePreference::Auto) {
        return static_cast<int>(pref);
    }
    // Auto calculation based on resolution
    int scale = 1;
    while ((windowWidth / (scale + 1) >= 320) && (windowHeight / (scale + 1) >= 240)) {
        ++scale;
    }
    return std::max(1, scale);
}

// ============================================================================
// 7. Dynamic Key Rebinding & options.txt Model (P4.4)
// ============================================================================

enum class GameAction {
    Forward,
    Backward,
    Left,
    Right,
    Jump,
    Sneak,
    Inventory,
    Drop,
    Chat
};

struct KeyConfig {
    std::unordered_map<std::string, int> bindings = {
        {"key_forward", 26},   // SDL_SCANCODE_W = 26
        {"key_backward", 22},  // SDL_SCANCODE_S = 22
        {"key_left", 4},       // SDL_SCANCODE_A = 4
        {"key_right", 7},      // SDL_SCANCODE_D = 7
        {"key_jump", 44},      // SDL_SCANCODE_SPACE = 44
        {"key_sneak", 225},    // SDL_SCANCODE_LSHIFT = 225
        {"key_inventory", 8}   // SDL_SCANCODE_E = 8
    };
    int guiScale{0};           // 0 = Auto
    float fov{70.0f};
    float musicVolume{1.0f};
    float soundVolume{1.0f};
    int renderDistance{8};

    void saveToFile(const std::filesystem::path& path) const {
        std::ofstream out(path);
        for (const auto& [k, v] : bindings) {
            out << k << ":" << v << "\n";
        }
        out << "gui_scale:" << guiScale << "\n";
        out << "fov:" << fov << "\n";
        out << "music_volume:" << musicVolume << "\n";
        out << "sound_volume:" << soundVolume << "\n";
        out << "render_distance:" << renderDistance << "\n";
    }

    bool loadFromFile(const std::filesystem::path& path) {
        std::ifstream in(path);
        if (!in.is_open()) return false;
        std::string line;
        while (std::getline(in, line)) {
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            const std::string key = line.substr(0, colon);
            const std::string val = line.substr(colon + 1);
            if (key == "gui_scale") {
                guiScale = std::stoi(val);
            } else if (key == "fov") {
                fov = std::stof(val);
            } else if (key == "music_volume") {
                musicVolume = std::stof(val);
            } else if (key == "sound_volume") {
                soundVolume = std::stof(val);
            } else if (key == "render_distance") {
                renderDistance = std::stoi(val);
            } else if (key.rfind("key_", 0) == 0) {
                bindings[key] = std::stoi(val);
            }
        }
        return true;
    }
};

} // namespace betacraft::test_contracts
