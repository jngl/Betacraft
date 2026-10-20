#include "math/PerlinNoise.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace betacraft::math {

PerlinNoise::PerlinNoise(uint64_t seed) noexcept {
    reseed(seed);
}

void PerlinNoise::reseed(uint64_t seed) noexcept {
    std::array<uint8_t, 256> p{};
    std::iota(p.begin(), p.end(), static_cast<uint8_t>(0));

    std::mt19937_64 rng(seed);
    std::shuffle(p.begin(), p.end(), rng);

    for (size_t i = 0; i < 256; ++i) {
        m_perm[i] = p[i];
        m_perm[256 + i] = p[i];
    }
}

double PerlinNoise::noise2D(double x, double y) const noexcept {
    const int X = static_cast<int>(std::floor(x)) & 255;
    const int Y = static_cast<int>(std::floor(y)) & 255;

    x -= std::floor(x);
    y -= std::floor(y);

    const double u = fade(x);
    const double v = fade(y);

    const int A = m_perm[X] + Y;
    const int B = m_perm[X + 1] + Y;

    const double res = lerp(v,
        lerp(u, grad2D(m_perm[A], x, y), grad2D(m_perm[B], x - 1.0, y)),
        lerp(u, grad2D(m_perm[A + 1], x, y - 1.0), grad2D(m_perm[B + 1], x - 1.0, y - 1.0))
    );
    return std::clamp(res * 0.5, -1.0, 1.0);
}

double PerlinNoise::noise3D(double x, double y, double z) const noexcept {
    const int X = static_cast<int>(std::floor(x)) & 255;
    const int Y = static_cast<int>(std::floor(y)) & 255;
    const int Z = static_cast<int>(std::floor(z)) & 255;

    x -= std::floor(x);
    y -= std::floor(y);
    z -= std::floor(z);

    const double u = fade(x);
    const double v = fade(y);
    const double w = fade(z);

    const int A  = m_perm[X] + Y;
    const int AA = m_perm[A] + Z;
    const int AB = m_perm[A + 1] + Z;
    const int B  = m_perm[X + 1] + Y;
    const int BA = m_perm[B] + Z;
    const int BB = m_perm[B + 1] + Z;

    const double res = lerp(w,
        lerp(v,
            lerp(u, grad3D(m_perm[AA], x, y, z), grad3D(m_perm[BA], x - 1.0, y, z)),
            lerp(u, grad3D(m_perm[AB], x, y - 1.0, z), grad3D(m_perm[BB], x - 1.0, y - 1.0, z))
        ),
        lerp(v,
            lerp(u, grad3D(m_perm[AA + 1], x, y, z - 1.0), grad3D(m_perm[BA + 1], x - 1.0, y, z - 1.0)),
            lerp(u, grad3D(m_perm[AB + 1], x, y - 1.0, z - 1.0), grad3D(m_perm[BB + 1], x - 1.0, y - 1.0, z - 1.0))
        )
    );
    return std::clamp(res, -1.0, 1.0);
}

double PerlinNoise::fbm2D(double x, double y, int octaves, double lacunarity, double gain) const noexcept {
    double sum = 0.0;
    double amp = 1.0;
    double freq = 1.0;
    double norm = 0.0;

    for (int i = 0; i < octaves; ++i) {
        sum += noise2D(x * freq, y * freq) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return (norm > 0.0) ? (sum / norm) : 0.0;
}

double PerlinNoise::fbm3D(double x, double y, double z, int octaves, double lacunarity, double gain) const noexcept {
    double sum = 0.0;
    double amp = 1.0;
    double freq = 1.0;
    double norm = 0.0;

    for (int i = 0; i < octaves; ++i) {
        sum += noise3D(x * freq, y * freq, z * freq) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return (norm > 0.0) ? (sum / norm) : 0.0;
}

uint32_t PerlinNoise::hashCoords(int32_t x, int32_t y, int32_t z, uint64_t seed) noexcept {
    uint64_t h = seed;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(x)) * 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(y)) * 0xBF58476D1CE4E5B9ULL;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(z)) * 0x94D049BB133111EBULL;
    h = (h ^ (h >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    h = (h ^ (h >> 27U)) * 0x94D049BB133111EBULL;
    h = h ^ (h >> 31U);
    return static_cast<uint32_t>(h);
}

} // namespace betacraft::math
