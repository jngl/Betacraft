#pragma once

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace betacraft::core {

class NonCopyable {
protected:
    constexpr NonCopyable() = default;
    ~NonCopyable() = default;

    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

struct ChunkCoord {
    int32_t x{0};
    int32_t z{0};

    constexpr bool operator==(const ChunkCoord& other) const noexcept = default;
};

struct BlockPos {
    int32_t x{0};
    int32_t y{0};
    int32_t z{0};

    constexpr bool operator==(const BlockPos& other) const noexcept = default;

    constexpr BlockPos offset(int32_t dx, int32_t dy, int32_t dz) const noexcept {
        return {x + dx, y + dy, z + dz};
    }
};

enum class FaceDir : uint8_t {
    PosX = 0, // East (+X)
    NegX = 1, // West (-X)
    PosY = 2, // Top (+Y)
    NegY = 3, // Bottom (-Y)
    PosZ = 4, // South (+Z)
    NegZ = 5  // North (-Z)
};

constexpr glm::ivec3 faceNormal(FaceDir dir) noexcept {
    switch (dir) {
        case FaceDir::PosX: return { 1,  0,  0};
        case FaceDir::NegX: return {-1,  0,  0};
        case FaceDir::PosY: return { 0,  1,  0};
        case FaceDir::NegY: return { 0, -1,  0};
        case FaceDir::PosZ: return { 0,  0,  1};
        case FaceDir::NegZ: return { 0,  0, -1};
    }
    return {0, 0, 0};
}

constexpr int32_t floorDiv(int32_t a, int32_t b) noexcept {
    int32_t d = a / b;
    int32_t r = a % b;
    if ((r != 0) && ((r < 0) != (b < 0))) {
        --d;
    }
    return d;
}

constexpr int32_t floorMod(int32_t a, int32_t b) noexcept {
    int32_t r = a % b;
    if ((r != 0) && ((r < 0) != (b < 0))) {
        r += b;
    }
    return r;
}

} // namespace betacraft::core

namespace std {

template <>
struct hash<betacraft::core::ChunkCoord> {
    size_t operator()(const betacraft::core::ChunkCoord& c) const noexcept {
        const uint64_t ux = static_cast<uint32_t>(c.x);
        const uint64_t uz = static_cast<uint32_t>(c.z);
        return std::hash<uint64_t>{}((ux << 32ULL) ^ (uz * 0x9E3779B97F4A7C15ULL));
    }
};

template <>
struct hash<betacraft::core::BlockPos> {
    size_t operator()(const betacraft::core::BlockPos& p) const noexcept {
        size_t h1 = std::hash<int32_t>{}(p.x);
        size_t h2 = std::hash<int32_t>{}(p.y);
        size_t h3 = std::hash<int32_t>{}(p.z);
        return h1 ^ (h2 * 0x9E3779B9U + (h1 << 6U) + (h1 >> 2U)) ^ (h3 * 0x85EBCA6BU);
    }
};

} // namespace std
