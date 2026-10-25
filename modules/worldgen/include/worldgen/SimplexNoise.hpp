#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace betacraft::worldgen {

// Seeded 2D / 3D Simplex noise (Gustavson / Perlin 2001 formulation).
// Chosen over the classic Perlin lattice for the caves because simplex has no
// axis-aligned artefacts and costs O(N) corners instead of O(2^N), which matters when
// every voxel of a 16x128x16 chunk is evaluated twice (spaghetti) plus once (cheese).
// The output is a pure function of (seed, coordinates): no hidden state, so evaluating
// the same world position from two different chunks always yields the same value.
class SimplexNoise {
public:
    // Octaves beyond this add nothing representable (2^-31 of the base amplitude at the
    // default gain) and would only push x * frequency out of the supported domain.
    static constexpr int MAX_FBM_OCTAVES = 31;
    // |coordinate| above this is outside the supported domain: the lattice index would no
    // longer fit int64 safely and doubles carry no fractional part anyway. Such inputs (and
    // NaN / inf) return 0.0 instead of reaching an undefined float -> int conversion.
    static constexpr double MAX_COORDINATE = 1.0e15;

    explicit SimplexNoise(uint64_t seed = 0ULL) noexcept;

    void reseed(uint64_t seed) noexcept;
    [[nodiscard]] uint64_t getSeed() const noexcept { return m_seed; }

    // Raw noise in [-1, 1]; both dimensions reach about +-0.98 / +-0.998 (scale factors are
    // calibrated to the gradient sets, the final clamp only guards rounding).
    [[nodiscard]] double noise2D(double x, double y) const noexcept;
    [[nodiscard]] double noise3D(double x, double y, double z) const noexcept;

    // Fractal sum normalised by the absolute amplitude total, so the result also stays in
    // [-1, 1]. octaves <= 0 yields 0.0 (empty sum); octaves are capped at MAX_FBM_OCTAVES and
    // any non-finite intermediate (huge lacunarity / gain) yields 0.0, never UB.
    [[nodiscard]] double fbm2D(double x, double y, int octaves,
                               double lacunarity = 2.0, double gain = 0.5) const noexcept;
    [[nodiscard]] double fbm3D(double x, double y, double z, int octaves,
                               double lacunarity = 2.0, double gain = 0.5) const noexcept;

private:
    [[nodiscard]] uint8_t perm(int64_t i) const noexcept {
        return m_perm[static_cast<size_t>(i & 511)];
    }

    uint64_t m_seed{0ULL};
    // Doubled table so that perm(i + perm(j)) never needs a second mask.
    std::array<uint8_t, 512> m_perm{};
};

} // namespace betacraft::worldgen
