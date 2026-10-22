#pragma once

#include <cstdint>

namespace betacraft::gameplay {

// Tiny seeded generator shared by the drop code. Used instead of <random> distributions so a
// given seed yields the same drops on every standard library (replays, tests, networking).
class SplitMix64 {
public:
    explicit constexpr SplitMix64(uint64_t seed) noexcept : m_state(seed) {}

    [[nodiscard]] constexpr uint64_t next() noexcept {
        uint64_t z = (m_state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    // Uniform in [0, 1) with 24 bits of precision, like java.util.Random.nextFloat.
    [[nodiscard]] constexpr float nextFloat() noexcept {
        return static_cast<float>(next() >> 40U) * (1.0f / static_cast<float>(1U << 24U));
    }

    // Uniform in [0, bound) for bound > 0 (java.util.Random.nextInt); 0 otherwise. The modulo
    // bias is below 2^-32 for the small bounds used here.
    [[nodiscard]] constexpr int32_t nextInt(int32_t bound) noexcept {
        return bound > 0 ? static_cast<int32_t>(next() % static_cast<uint64_t>(bound)) : 0;
    }

private:
    uint64_t m_state;
};

} // namespace betacraft::gameplay
