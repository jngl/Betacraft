#pragma once

#include <array>
#include <cstdint>

namespace betacraft::math {

class PerlinNoise {
public:
    explicit PerlinNoise(uint64_t seed = 173173173ULL) noexcept;

    void reseed(uint64_t seed) noexcept;

    [[nodiscard]] double noise2D(double x, double y) const noexcept;
    [[nodiscard]] double noise3D(double x, double y, double z) const noexcept;

    [[nodiscard]] double fbm2D(double x, double y, int octaves, double lacunarity = 2.0, double gain = 0.5) const noexcept;
    [[nodiscard]] double fbm3D(double x, double y, double z, int octaves, double lacunarity = 2.0, double gain = 0.5) const noexcept;

    [[nodiscard]] static uint32_t hashCoords(int32_t x, int32_t y, int32_t z, uint64_t seed) noexcept;

private:
    static constexpr double fade(double t) noexcept {
        return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    }

    static constexpr double lerp(double t, double a, double b) noexcept {
        return a + t * (b - a);
    }

    static constexpr double grad2D(int hash, double x, double y) noexcept {
        const int h = hash & 7;
        const double u = (h < 4) ? x : y;
        const double v = (h < 4) ? y : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -2.0 * v : 2.0 * v);
    }

    static constexpr double grad3D(int hash, double x, double y, double z) noexcept {
        const int h = hash & 15;
        const double u = (h < 8) ? x : y;
        const double v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
        return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
    }

    std::array<uint8_t, 512> m_perm{};
};

} // namespace betacraft::math
