#pragma once

#include "core/Types.hpp"

#include <cstdint>

namespace betacraft::worldgen {

// SplitMix64 finaliser: full-avalanche 64-bit mix (every input bit flips ~half the output).
[[nodiscard]] constexpr uint64_t mix64(uint64_t z) noexcept {
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

// Small, fast, deterministic PRNG (SplitMix64 stream). Platform-independent by
// construction - unlike std::mt19937 + std::uniform_int_distribution, whose distribution
// algorithms are implementation-defined - so a seed reproduces the same world everywhere.
class SeededRandom {
public:
    explicit constexpr SeededRandom(uint64_t seed) noexcept : m_state(seed) {}

    [[nodiscard]] constexpr uint64_t next() noexcept {
        m_state += 0x9E3779B97F4A7C15ULL;
        return mix64(m_state);
    }

    // Uniform value in [0, bound). Multiply-shift (Lemire) avoids the modulo bias of
    // `next() % bound` without a rejection loop; bound 0 or 1 yields 0.
    [[nodiscard]] constexpr uint32_t nextBounded(uint32_t bound) noexcept {
        if (bound <= 1U) return 0U;
        const uint64_t r = next() >> 32U;
        return static_cast<uint32_t>((r * static_cast<uint64_t>(bound)) >> 32U);
    }

    // Uniform double in [0, 1) built from the top 53 bits.
    [[nodiscard]] constexpr double nextDouble() noexcept {
        return static_cast<double>(next() >> 11U) * (1.0 / 9007199254740992.0);
    }

private:
    uint64_t m_state;
};

// Per-chunk RNG seed: pure function of (world seed, chunk coordinate), so the decoration
// of a chunk never depends on the order in which chunks were generated or loaded.
[[nodiscard]] constexpr uint64_t chunkSeed(uint64_t worldSeed, core::ChunkCoord coord) noexcept {
    const uint64_t ux = static_cast<uint32_t>(coord.x);
    const uint64_t uz = static_cast<uint32_t>(coord.z);
    // Two mixing rounds so that x and z cannot cancel (prevents (x,z)/(z,x) collisions).
    return mix64(mix64(worldSeed ^ (ux * 0xD1B54A32D192ED03ULL)) ^ (uz * 0xABC98388FB8FAC03ULL));
}

// Stateless per-column hash, salted so independent decisions use independent streams.
[[nodiscard]] constexpr uint64_t columnHash(uint64_t worldSeed, int32_t wx, int32_t wz, uint32_t salt) noexcept {
    const uint64_t ux = static_cast<uint32_t>(wx);
    const uint64_t uz = static_cast<uint32_t>(wz);
    return mix64(mix64(mix64(worldSeed ^ (ux * 0xD1B54A32D192ED03ULL)) ^ (uz * 0xABC98388FB8FAC03ULL)) ^
                 (static_cast<uint64_t>(salt) * 0x8CB92BA72F3D8DD7ULL));
}

} // namespace betacraft::worldgen
