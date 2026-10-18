#pragma once

#include "core/Types.hpp"
#include "gameplay/SplitMix64.hpp"
#include "physics/AABB.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

namespace betacraft::gameplay {

// S6.5 Beta 1.7.3 Explosion.doExplosionA, as pure functions over callbacks so that tests need
// no World. GameSession::explode (S6.6) applies the result to the world and the entities.

struct ExplosionInput {
    glm::vec3 centre{0.0f};
    float power{0.0f}; // Beta explosionSize: TNT 4, creeper 3
    uint64_t seed{0};  // Feeds the per-ray intensity rolls: same input, same crater
};

// Beta: rays start from the surface cells of a 16^3 grid (16^3 - 14^3 = 1352 directions),
// march in steps of 0.3 blocks and lose (resistance + 0.3) * 0.3 in each non-air cell, then
// 0.3 * 0.75 per step.
inline constexpr int32_t kExplosionGrid = 16;
inline constexpr size_t kExplosionRayCount = 1352;
inline constexpr float kExplosionStep = 0.3f;
inline constexpr float kExplosionStepDecay = kExplosionStep * 0.75f;
inline constexpr float kExplosionMinIntensity = 0.7f;   // power x (0.7 + rand x 0.6)
inline constexpr float kExplosionIntensityRange = 0.6f;
// Entities within 2 x power of the centre are hurt (Beta doubles explosionSize there).
inline constexpr float kExplosionDamageRadiusFactor = 2.0f;

namespace detail {

// Unit directions of the 1352 rays, in Beta's loop order (x outer, then y, then z).
[[nodiscard]] const std::array<glm::vec3, kExplosionRayCount>& explosionRayDirections() noexcept;

struct BlockPosHash {
    size_t operator()(const core::BlockPos& p) const noexcept {
        return std::hash<core::BlockPos>{}(p);
    }
};

[[nodiscard]] inline int32_t floorToInt(float v) noexcept {
    return static_cast<int32_t>(std::floor(v));
}

} // namespace detail

// The cells the explosion destroys, sorted (x, then y, then z) and without duplicates.
// `resistance(x, y, z)` returns the cell's BlockDef::blastResistance, or a negative value for
// air (air takes nothing off a ray and is never listed).
template <class Resistance>
[[nodiscard]] std::vector<core::BlockPos> explosionBlocks(const ExplosionInput& in, Resistance&& resistance) {
    std::unordered_set<core::BlockPos, detail::BlockPosHash> hit;
    SplitMix64 rng(in.seed);
    for (const glm::vec3& dir : detail::explosionRayDirections()) {
        float intensity = in.power * (kExplosionMinIntensity + rng.nextFloat() * kExplosionIntensityRange);
        glm::vec3 p = in.centre;
        while (intensity > 0.0f) {
            const int32_t x = detail::floorToInt(p.x);
            const int32_t y = detail::floorToInt(p.y);
            const int32_t z = detail::floorToInt(p.z);
            const float r = resistance(x, y, z);
            if (r >= 0.0f) {
                intensity -= (r + kExplosionStep) * kExplosionStep;
                if (intensity > 0.0f) {
                    hit.insert({x, y, z});
                }
            }
            p += dir * kExplosionStep;
            intensity -= kExplosionStepDecay;
        }
    }
    std::vector<core::BlockPos> out(hit.begin(), hit.end());
    std::sort(out.begin(), out.end(), [](const core::BlockPos& a, const core::BlockPos& b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    });
    return out;
}

// Whether the segment from `from` to `to` crosses a cell for which `isSolid(x, y, z)` holds
// (Beta World.rayTraceBlocks for the exposure test). The cell holding `to` is not tested.
template <class IsSolid>
[[nodiscard]] bool segmentBlocked(const glm::vec3& from, const glm::vec3& to, IsSolid&& isSolid) {
    const glm::vec3 d = to - from;
    const float len = glm::length(d);
    int32_t x = detail::floorToInt(from.x);
    int32_t y = detail::floorToInt(from.y);
    int32_t z = detail::floorToInt(from.z);
    const int32_t ex = detail::floorToInt(to.x);
    const int32_t ey = detail::floorToInt(to.y);
    const int32_t ez = detail::floorToInt(to.z);
    if (len < 1e-6f) {
        return false;
    }
    const glm::vec3 dir = d / len;
    const auto axis = [](float o, float dv, int32_t cell, int32_t& step, float& tMax, float& tDelta) {
        if (dv > 0.0f) {
            step = 1;
            tDelta = 1.0f / dv;
            tMax = (static_cast<float>(cell + 1) - o) * tDelta;
        } else if (dv < 0.0f) {
            step = -1;
            tDelta = -1.0f / dv;
            tMax = (o - static_cast<float>(cell)) * tDelta;
        } else {
            step = 0;
            tDelta = tMax = std::numeric_limits<float>::infinity();
        }
    };
    int32_t sx = 0, sy = 0, sz = 0;
    float tmx = 0.0f, tmy = 0.0f, tmz = 0.0f, tdx = 0.0f, tdy = 0.0f, tdz = 0.0f;
    axis(from.x, dir.x, x, sx, tmx, tdx);
    axis(from.y, dir.y, y, sy, tmy, tdy);
    axis(from.z, dir.z, z, sz, tmz, tdz);
    // At most one cell per axis crossing, plus the start cell.
    const int32_t maxSteps = std::abs(ex - x) + std::abs(ey - y) + std::abs(ez - z) + 1;
    for (int32_t i = 0; i < maxSteps; ++i) {
        if (x == ex && y == ey && z == ez) {
            return false;
        }
        if (isSolid(x, y, z)) {
            return true;
        }
        if (tmx <= tmy && tmx <= tmz) {
            if (tmx > len) return false;
            x += sx;
            tmx += tdx;
        } else if (tmy <= tmz) {
            if (tmy > len) return false;
            y += sy;
            tmy += tdy;
        } else {
            if (tmz > len) return false;
            z += sz;
            tmz += tdz;
        }
    }
    return false;
}

// Beta World.getBlockDensity: the fraction of sample points of `box` (a grid spaced
// 1 / (size x 2 + 1) along each axis, corners included) that see `centre` without a solid
// cell in between. 1 in the open, 0 behind a wall.
[[nodiscard]] float exposure(const glm::vec3& centre, const physics::AABB& box, auto&& isSolid) {
    const glm::vec3 size = box.max - box.min;
    const glm::vec3 spacing(1.0f / (size.x * 2.0f + 1.0f), 1.0f / (size.y * 2.0f + 1.0f),
                            1.0f / (size.z * 2.0f + 1.0f));
    int32_t seen = 0;
    int32_t total = 0;
    // Integer counters: float accumulation would drop or add the last sample.
    const auto samples = [](float s) { return static_cast<int32_t>(std::floor(1.0f / s + 1e-4f)) + 1; };
    const int32_t nx = samples(spacing.x);
    const int32_t ny = samples(spacing.y);
    const int32_t nz = samples(spacing.z);
    for (int32_t i = 0; i < nx; ++i) {
        for (int32_t j = 0; j < ny; ++j) {
            for (int32_t k = 0; k < nz; ++k) {
                const glm::vec3 f(std::min(1.0f, static_cast<float>(i) * spacing.x),
                                  std::min(1.0f, static_cast<float>(j) * spacing.y),
                                  std::min(1.0f, static_cast<float>(k) * spacing.z));
                const glm::vec3 point = box.min + size * f;
                if (!segmentBlocked(point, centre, isSolid)) {
                    ++seen;
                }
                ++total;
            }
        }
    }
    return total > 0 ? static_cast<float>(seen) / static_cast<float>(total) : 0.0f;
}

// Beta Explosion entity damage: impact = (1 - distance / 2p) x exposure, then
// int((impact^2 + impact) / 2 x 8 x 2p + 1); 0 from 2p on.
[[nodiscard]] int32_t explosionDamage(float distance, float power, float exposure) noexcept;

// Beta knockback, in blocks per tick (Beta motion units: x 20 for blocks per second): the unit
// direction from the centre to the entity times the impact; zero from 2p on.
[[nodiscard]] glm::vec3 explosionKnockback(const glm::vec3& centre, const glm::vec3& entityPos, float power,
                                           float exposure) noexcept;

} // namespace betacraft::gameplay
