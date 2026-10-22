#include "worldgen/SimplexNoise.hpp"
#include "worldgen/SeededRandom.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace betacraft::worldgen {

namespace {

// 12 edge-midpoint gradients of a cube: the canonical simplex 3D gradient set.
constexpr std::array<std::array<double, 3>, 12> GRAD3{{
    {{1, 1, 0}}, {{-1, 1, 0}}, {{1, -1, 0}}, {{-1, -1, 0}},
    {{1, 0, 1}}, {{-1, 0, 1}}, {{1, 0, -1}}, {{-1, 0, -1}},
    {{0, 1, 1}}, {{0, -1, 1}}, {{0, 1, -1}}, {{0, -1, -1}},
}};

// 8 unit-ish directions for 2D (axis + diagonal), avoids the bias of using GRAD3.xy.
constexpr std::array<std::array<double, 2>, 8> GRAD2{{
    {{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}},
    {{0.70710678118654752, 0.70710678118654752}}, {{-0.70710678118654752, 0.70710678118654752}},
    {{0.70710678118654752, -0.70710678118654752}}, {{-0.70710678118654752, -0.70710678118654752}},
}};

// Only called with |v| <= MAX_COORDINATE * (1 + skew), far inside the int64 range.
[[nodiscard]] inline int64_t fastFloor(double v) noexcept {
    return static_cast<int64_t>(std::floor(v));
}

// Also rejects NaN (every comparison with NaN is false).
[[nodiscard]] inline bool inDomain(double v) noexcept {
    return std::abs(v) <= SimplexNoise::MAX_COORDINATE;
}

// GRAD2 holds unit vectors, whereas Gustavson's factor 70 was calibrated for the
// length-sqrt(2) diagonals of GRAD3.xy: with unit gradients 70 only reaches +-0.706.
// 70 * sqrt(2) restores a peak of about +-0.998, so thresholds such as the aquifer's
// aq > 0.42 mean 42 % of the real amplitude.
constexpr double NOISE2D_SCALE = 70.0 * 1.41421356237309504880;
// GRAD3 + radius 0.6: 32 peaks at about +-0.98 (measured), no rescale needed.
constexpr double NOISE3D_SCALE = 32.0;

template <typename Sample>
[[nodiscard]] double fractalSum(int octaves, double lacunarity, double gain, Sample&& sample) noexcept {
    const int n = std::min(octaves, SimplexNoise::MAX_FBM_OCTAVES);
    double sum = 0.0;
    double norm = 0.0;
    double amp = 1.0;
    double freq = 1.0;
    for (int o = 0; o < n; ++o) {
        sum += amp * sample(freq);
        norm += std::abs(amp); // |amp|: a negative gain must not cancel the normaliser
        amp *= gain;
        freq *= lacunarity;
    }
    const double out = (norm > 0.0) ? sum / norm : 0.0;
    return std::isfinite(out) ? std::clamp(out, -1.0, 1.0) : 0.0;
}

} // namespace

SimplexNoise::SimplexNoise(uint64_t seed) noexcept {
    reseed(seed);
}

void SimplexNoise::reseed(uint64_t seed) noexcept {
    m_seed = seed;
    std::array<uint8_t, 256> base{};
    std::iota(base.begin(), base.end(), static_cast<uint8_t>(0));

    // Fisher-Yates driven by SeededRandom; deterministic on every platform (no std::shuffle,
    // whose algorithm is implementation-defined).
    SeededRandom rng(seed ^ 0x5DEECE66DULL);
    for (size_t i = base.size() - 1; i > 0; --i) {
        const size_t j = rng.nextBounded(static_cast<uint32_t>(i + 1));
        std::swap(base[i], base[j]);
    }
    for (size_t i = 0; i < m_perm.size(); ++i) {
        m_perm[i] = base[i & 255U];
    }
}

double SimplexNoise::noise2D(double xin, double yin) const noexcept {
    constexpr double F2 = 0.36602540378443864676; // 0.5 * (sqrt(3) - 1)
    constexpr double G2 = 0.21132486540518711775; // (3 - sqrt(3)) / 6
    if (!inDomain(xin) || !inDomain(yin)) return 0.0;

    const double s = (xin + yin) * F2;
    const int64_t i = fastFloor(xin + s);
    const int64_t j = fastFloor(yin + s);
    const double t = static_cast<double>(i + j) * G2;
    const double x0 = xin - (static_cast<double>(i) - t);
    const double y0 = yin - (static_cast<double>(j) - t);

    const int64_t i1 = (x0 > y0) ? 1 : 0;
    const int64_t j1 = (x0 > y0) ? 0 : 1;

    const double x1 = x0 - static_cast<double>(i1) + G2;
    const double y1 = y0 - static_cast<double>(j1) + G2;
    const double x2 = x0 - 1.0 + 2.0 * G2;
    const double y2 = y0 - 1.0 + 2.0 * G2;

    const int64_t ii = i & 255;
    const int64_t jj = j & 255;

    auto corner = [this](int64_t gi, double x, double y) noexcept -> double {
        double t0 = 0.5 - x * x - y * y;
        if (t0 < 0.0) return 0.0;
        const auto& g = GRAD2[static_cast<size_t>(perm(gi) & 7U)];
        t0 *= t0;
        return t0 * t0 * (g[0] * x + g[1] * y);
    };

    const double n0 = corner(ii + perm(jj), x0, y0);
    const double n1 = corner(ii + i1 + perm(jj + j1), x1, y1);
    const double n2 = corner(ii + 1 + perm(jj + 1), x2, y2);

    // The clamp only absorbs rounding: the scale keeps the true peak just below 1.
    return std::clamp(NOISE2D_SCALE * (n0 + n1 + n2), -1.0, 1.0);
}

double SimplexNoise::noise3D(double xin, double yin, double zin) const noexcept {
    constexpr double F3 = 1.0 / 3.0;
    constexpr double G3 = 1.0 / 6.0;
    if (!inDomain(xin) || !inDomain(yin) || !inDomain(zin)) return 0.0;

    const double s = (xin + yin + zin) * F3;
    const int64_t i = fastFloor(xin + s);
    const int64_t j = fastFloor(yin + s);
    const int64_t k = fastFloor(zin + s);
    const double t = static_cast<double>(i + j + k) * G3;
    const double x0 = xin - (static_cast<double>(i) - t);
    const double y0 = yin - (static_cast<double>(j) - t);
    const double z0 = zin - (static_cast<double>(k) - t);

    // Determine which of the six tetrahedra of the skewed cube we are in.
    int64_t i1 = 0, j1 = 0, k1 = 0, i2 = 0, j2 = 0, k2 = 0;
    if (x0 >= y0) {
        if (y0 >= z0)      { i1 = 1; i2 = 1; j2 = 1; }
        else if (x0 >= z0) { i1 = 1; i2 = 1; k2 = 1; }
        else               { k1 = 1; i2 = 1; k2 = 1; }
    } else {
        if (y0 < z0)       { k1 = 1; j2 = 1; k2 = 1; }
        else if (x0 < z0)  { j1 = 1; j2 = 1; k2 = 1; }
        else               { j1 = 1; i2 = 1; j2 = 1; }
    }

    const double x1 = x0 - static_cast<double>(i1) + G3;
    const double y1 = y0 - static_cast<double>(j1) + G3;
    const double z1 = z0 - static_cast<double>(k1) + G3;
    const double x2 = x0 - static_cast<double>(i2) + 2.0 * G3;
    const double y2 = y0 - static_cast<double>(j2) + 2.0 * G3;
    const double z2 = z0 - static_cast<double>(k2) + 2.0 * G3;
    const double x3 = x0 - 1.0 + 3.0 * G3;
    const double y3 = y0 - 1.0 + 3.0 * G3;
    const double z3 = z0 - 1.0 + 3.0 * G3;

    const int64_t ii = i & 255;
    const int64_t jj = j & 255;
    const int64_t kk = k & 255;

    auto corner = [this](int64_t gi, double x, double y, double z) noexcept -> double {
        double t0 = 0.6 - x * x - y * y - z * z;
        if (t0 < 0.0) return 0.0;
        const auto& g = GRAD3[static_cast<size_t>(perm(gi) % 12U)];
        t0 *= t0;
        return t0 * t0 * (g[0] * x + g[1] * y + g[2] * z);
    };

    const double n0 = corner(ii + perm(jj + perm(kk)), x0, y0, z0);
    const double n1 = corner(ii + i1 + perm(jj + j1 + perm(kk + k1)), x1, y1, z1);
    const double n2 = corner(ii + i2 + perm(jj + j2 + perm(kk + k2)), x2, y2, z2);
    const double n3 = corner(ii + 1 + perm(jj + 1 + perm(kk + 1)), x3, y3, z3);

    return std::clamp(NOISE3D_SCALE * (n0 + n1 + n2 + n3), -1.0, 1.0);
}

double SimplexNoise::fbm2D(double x, double y, int octaves, double lacunarity, double gain) const noexcept {
    return fractalSum(octaves, lacunarity, gain, [&](double f) noexcept { return noise2D(x * f, y * f); });
}

double SimplexNoise::fbm3D(double x, double y, double z, int octaves, double lacunarity, double gain) const noexcept {
    return fractalSum(octaves, lacunarity, gain, [&](double f) noexcept { return noise3D(x * f, y * f, z * f); });
}

} // namespace betacraft::worldgen
