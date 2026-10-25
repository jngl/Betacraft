#pragma once

#include <cstdint>
#include <concepts>
#include <span>
#include <type_traits>
#include <vector>
#include <glm/glm.hpp>

namespace betacraft::streaming {

/**
 * @brief Packed 64-bit (8-byte) chunk vertex format for high performance voxel rendering.
 *
 * Bit layout:
 * Word 0 (32 bits):
 *   - Local X       : 5 bits  [0..16]   (bits 0..4)   mask 0x1F
 *   - Local Y       : 5 bits  [0..16]   (bits 5..9)   mask 0x1F
 *   - Local Z       : 5 bits  [0..16]   (bits 10..14) mask 0x1F
 *   - Normal Index  : 3 bits  [0..5]    (bits 15..17) mask 0x07 (0=+Y, 1=-Y, 2=+Z, 3=-Z, 4=+X, 5=-X)
 *   - AO Level      : 2 bits  [0..3]    (bits 18..19) mask 0x03 (0=0.50, 1=0.66, 2=0.82, 3=1.00)
 *   - UV Corner     : 2 bits  [0..3]    (bits 20..21) mask 0x03 ((0,0), (1,0), (1,1), (0,1))
 *   - MaterialLayer : 10 bits [0..1023] (bits 22..31) mask 0x3FF
 *   Total Word 0: 5 + 5 + 5 + 3 + 2 + 2 + 10 = 32 bits.
 *
 * Word 1 (32 bits):
 *   - Sky Light     : 4 bits  [0..15]   (bits 0..3)   mask 0x0F
 *   - Block Light   : 4 bits  [0..15]   (bits 4..7)   mask 0x0F
 *   - Biome Tint    : 8 bits  [0..255]  (bits 8..15)  mask 0xFF
 *   - Anim Frame    : 5 bits  [0..31]   (bits 16..20) mask 0x1F
 *   - Sway Flag     : 1 bit   {0, 1}    (bit 21)      mask 0x01
 *   - Translucent   : 1 bit   {0, 1}    (bit 22)      mask 0x01
 *   - Reserved/LOD  : 9 bits  [0..511]  (bits 23..31) mask 0x1FF (stores sectionY index 0..7 or LOD)
 *   Total Word 1: 4 + 4 + 8 + 5 + 1 + 1 + 9 = 32 bits.
 *
 * Fine offsets (shapes: ladders, doors, fences, stairs...): a vertex may sit 0..15
 * sixteenths below its integer position on each axis. They live in bits no mesher used:
 *   - offX : word1 bits 26..29 (reserved, above the 3-bit section index)
 *   - offY : word1 bits 16..19 (anim frame; only read while the sway flag is clear)
 *   - offZ : word1 bits 30..31 (low) and word0 bits 30..31 (high; the atlas has 256 tiles,
 *            so the shader reads the material layer as 8 bits)
 * Position = integer position - offset / 16. Vertices without offsets are unchanged.
 */
struct alignas(8) PackedChunkVertex {
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

    static constexpr uint32_t kFineUnits = 16;

    // Sets the fine offsets (0..15 sixteenths each, subtracted from the integer position).
    // The vertex must not sway.
    constexpr void setFineOffset(uint32_t offX, uint32_t offY, uint32_t offZ) noexcept {
        word1 = (word1 & ~((0xFu << 26u) | (0xFu << 16u) | (0x3u << 30u))) |
                ((offX & 0xFu) << 26u) | ((offY & 0xFu) << 16u) | ((offZ & 0x3u) << 30u);
        word0 = (word0 & ~(0x3u << 30u)) | (((offZ >> 2u) & 0x3u) << 30u);
    }

    [[nodiscard]] constexpr glm::uvec3 getFineOffset() const noexcept {
        const uint32_t offY = getSway() ? 0u : ((word1 >> 16u) & 0xFu);
        return {(word1 >> 26u) & 0xFu, offY, ((word1 >> 30u) & 0x3u) | (((word0 >> 30u) & 0x3u) << 2u)};
    }

    // The atlas tile the shader samples (the material layer's low 8 bits).
    [[nodiscard]] constexpr uint32_t getAtlasTile() const noexcept { return getMatLayer() & 0xFFu; }

    // Static unpack helpers
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

    // Instance unpack getters
    [[nodiscard]] constexpr uint32_t getX() const noexcept { return unpackX(word0); }
    [[nodiscard]] constexpr uint32_t getY() const noexcept { return unpackY(word0); }
    [[nodiscard]] constexpr uint32_t getZ() const noexcept { return unpackZ(word0); }
    [[nodiscard]] constexpr uint32_t getNormal() const noexcept { return unpackNormal(word0); }
    [[nodiscard]] constexpr uint32_t getAO() const noexcept { return unpackAO(word0); }
    [[nodiscard]] constexpr uint32_t getUVCorner() const noexcept { return unpackUVCorner(word0); }
    [[nodiscard]] constexpr uint32_t getMatLayer() const noexcept { return unpackMatLayer(word0); }

    [[nodiscard]] constexpr uint32_t getSkyLight() const noexcept { return unpackSkyLight(word1); }
    [[nodiscard]] constexpr uint32_t getBlockLight() const noexcept { return unpackBlockLight(word1); }
    [[nodiscard]] constexpr uint32_t getBiomeTint() const noexcept { return unpackBiomeTint(word1); }
    [[nodiscard]] constexpr uint32_t getAnimFrame() const noexcept { return unpackAnimFrame(word1); }
    [[nodiscard]] constexpr bool getSway() const noexcept { return unpackSway(word1); }
    [[nodiscard]] constexpr bool getTranslucent() const noexcept { return unpackTranslucent(word1); }
    [[nodiscard]] constexpr uint32_t getReserved() const noexcept { return unpackReserved(word1); }

    [[nodiscard]] glm::vec3 getLocalPosition() const noexcept {
        const uint32_t secY = getReserved() & 0x7u;
        const glm::uvec3 fine = getFineOffset();
        return glm::vec3(
            static_cast<float>(getX()),
            static_cast<float>(secY * 16u + getY()),
            static_cast<float>(getZ())
        ) - glm::vec3(fine) / static_cast<float>(kFineUnits);
    }

    [[nodiscard]] glm::vec2 getLight() const noexcept {
        return glm::vec2(
            static_cast<float>(getSkyLight()) / 15.0f,
            static_cast<float>(getBlockLight()) / 15.0f
        );
    }

    // Planar UV projection function matching fractal UV shader
    [[nodiscard]] static glm::vec2 computePlanarUV(glm::vec3 worldPos, uint32_t normalIdx) noexcept {
        glm::vec2 planar(0.0f);
        if (normalIdx <= 1u) { // +Y or -Y
            planar = glm::vec2(worldPos.x, worldPos.z);
        } else if (normalIdx >= 4u) { // +X or -X
            planar = glm::vec2(worldPos.z, worldPos.y);
        } else { // +Z or -Z
            planar = glm::vec2(worldPos.x, worldPos.y);
        }
        return planar - glm::floor(planar);
    }
};

static_assert(sizeof(PackedChunkVertex) == 8, "PackedChunkVertex must be exactly 8 bytes");
static_assert(alignof(PackedChunkVertex) == 8, "PackedChunkVertex must be 8-byte aligned");
static_assert(std::is_trivially_copyable_v<PackedChunkVertex>, "PackedChunkVertex must be trivially copyable");

// Static Shared EBO pattern generation and validation
[[nodiscard]] inline std::vector<uint32_t> generateSharedEboPattern(size_t quadCount) {
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

[[nodiscard]] inline bool validateEboPattern(std::span<const uint32_t> indices, size_t quadCount) {
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

} // namespace betacraft::streaming
