#pragma once

#include "core/Types.hpp"
#include "worldgen/CaveCarver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace betacraft::worldgen {

// Adapts any callable `ColumnContext(int32_t wx, int32_t wz)` to ColumnSampler, so WorldGen can
// hand its analytic column function to the carver without inheriting from an interface.
// The callable must be a pure function of (wx, wz) - that is what keeps carving seamless - and
// must not throw (sampleColumn is noexcept).
template <typename Fn>
class FunctionColumnSampler final : public ColumnSampler {
public:
    explicit FunctionColumnSampler(Fn fn) noexcept(std::is_nothrow_move_constructible_v<Fn>)
        : m_fn(std::move(fn)) {}

    [[nodiscard]] ColumnContext sampleColumn(int32_t wx, int32_t wz) const noexcept override {
        return m_fn(wx, wz);
    }

private:
    Fn m_fn;
};

// Per-chunk column cache. CaveCarver::carveChunk resolves each column together with its 4 face
// neighbours (lateral aquitard guard), i.e. 5 source lookups per column. When the source is
// expensive (WorldGen scanning its density function top-down), sampling the chunk plus its
// 1-block face ring once up front cuts that to 320 lookups per chunk (1.25 per column).
// Queries outside the cached area fall through to the source, so results never differ from
// using the source directly.
class ChunkColumnCache final : public ColumnSampler {
public:
    static constexpr int32_t MARGIN = 1; // resolveColumn reads face neighbours only
    static constexpr int32_t SPAN = 16 + 2 * MARGIN;

    // `source` must outlive the cache: out-of-ring queries are forwarded to it.
    ChunkColumnCache(const ColumnSampler& source, core::ChunkCoord coord) noexcept;
    // A temporary source would dangle on the first out-of-ring query: refuse it at compile time.
    ChunkColumnCache(const ColumnSampler&& source, core::ChunkCoord coord) = delete;

    ChunkColumnCache(const ChunkColumnCache&) = delete;
    ChunkColumnCache& operator=(const ChunkColumnCache&) = delete;

    [[nodiscard]] core::ChunkCoord getCoord() const noexcept { return m_coord; }

    // Chunk columns plus the face ring; the 4 ring corners are excluded (never read).
    [[nodiscard]] bool contains(int32_t wx, int32_t wz) const noexcept;

    [[nodiscard]] ColumnContext sampleColumn(int32_t wx, int32_t wz) const noexcept override;

private:
    [[nodiscard]] static constexpr size_t slot(int64_t dx, int64_t dz) noexcept {
        return static_cast<size_t>((dz + MARGIN) * SPAN + (dx + MARGIN));
    }

    const ColumnSampler& m_source;
    core::ChunkCoord m_coord;
    int64_t m_baseX;
    int64_t m_baseZ;
    std::array<ColumnContext, static_cast<size_t>(SPAN * SPAN)> m_columns{};
};

} // namespace betacraft::worldgen
