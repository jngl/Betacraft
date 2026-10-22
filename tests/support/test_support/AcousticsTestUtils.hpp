#pragma once

#include "acoustics/AcousticOcclusion.hpp"

#include <cstdint>
#include <unordered_set>

namespace betacraft::acoustics::test {

// Sparse voxel grid used as a deterministic world stand-in; counts queries to check the O(R) bound.
class FakeVoxelGrid final : public VoxelSolidityQuery {
public:
    void set(const core::BlockPos& p) { m_solid.insert(p); }
    void clear(const core::BlockPos& p) { m_solid.erase(p); }

    void fillBox(const core::BlockPos& lo, const core::BlockPos& hi) {
        for (int32_t x = lo.x; x <= hi.x; ++x) {
            for (int32_t y = lo.y; y <= hi.y; ++y) {
                for (int32_t z = lo.z; z <= hi.z; ++z) {
                    m_solid.insert({x, y, z});
                }
            }
        }
    }

    // Solid shell of a box (inclusive bounds) with a hollow interior.
    void hollowBox(const core::BlockPos& lo, const core::BlockPos& hi) {
        for (int32_t x = lo.x; x <= hi.x; ++x) {
            for (int32_t y = lo.y; y <= hi.y; ++y) {
                for (int32_t z = lo.z; z <= hi.z; ++z) {
                    const bool edge = x == lo.x || x == hi.x || y == lo.y || y == hi.y || z == lo.z || z == hi.z;
                    if (edge) {
                        m_solid.insert({x, y, z});
                    }
                }
            }
        }
    }

    [[nodiscard]] bool isSolid(const core::BlockPos& p) const noexcept override {
        ++m_queries;
        return m_solid.contains(p);
    }

    [[nodiscard]] uint64_t queries() const noexcept { return m_queries; }
    void resetQueries() noexcept { m_queries = 0; }

private:
    std::unordered_set<core::BlockPos> m_solid;
    mutable uint64_t m_queries{0};
};

} // namespace betacraft::acoustics::test
