// P3.4 - Deterministic per-chunk RNG derived from (world seed, chunk coordinate).
#include "worldgen/SeededRandom.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <vector>

using betacraft::core::ChunkCoord;
using betacraft::worldgen::SeededRandom;
using betacraft::worldgen::chunkSeed;
using betacraft::worldgen::columnHash;

TEST(SeededRandom, SameStateProducesSameSequence) {
    SeededRandom a(123ULL);
    SeededRandom b(123ULL);
    for (int i = 0; i < 1000; ++i) {
        ASSERT_EQ(a.next(), b.next());
    }
}

TEST(SeededRandom, DifferentSeedsDiverge) {
    SeededRandom a(1ULL);
    SeededRandom b(2ULL);
    int equal = 0;
    for (int i = 0; i < 1000; ++i) {
        if (a.next() == b.next()) ++equal;
    }
    EXPECT_EQ(equal, 0);
}

TEST(SeededRandom, BoundedValuesStayInRangeAndCoverIt) {
    SeededRandom r(77ULL);
    std::vector<int> hist(7, 0);
    for (int i = 0; i < 7000; ++i) {
        const uint32_t v = r.nextBounded(7U);
        ASSERT_LT(v, 7U);
        ++hist[v];
    }
    for (int h : hist) {
        EXPECT_GT(h, 800);
        EXPECT_LT(h, 1200);
    }
}

TEST(SeededRandom, BoundedZeroAndOneReturnZero) {
    SeededRandom r(5ULL);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(r.nextBounded(0U), 0U);
        EXPECT_EQ(r.nextBounded(1U), 0U);
    }
}

TEST(SeededRandom, UnitDoubleIsHalfOpen) {
    SeededRandom r(9ULL);
    double sum = 0.0;
    for (int i = 0; i < 10000; ++i) {
        const double d = r.nextDouble();
        ASSERT_GE(d, 0.0);
        ASSERT_LT(d, 1.0);
        sum += d;
    }
    EXPECT_NEAR(sum / 10000.0, 0.5, 0.02);
}

TEST(ChunkSeed, IsPureFunctionOfWorldSeedAndCoord) {
    EXPECT_EQ(chunkSeed(42ULL, ChunkCoord{3, -7}), chunkSeed(42ULL, ChunkCoord{3, -7}));
    EXPECT_NE(chunkSeed(42ULL, ChunkCoord{3, -7}), chunkSeed(43ULL, ChunkCoord{3, -7}));
}

TEST(ChunkSeed, NeighbouringAndMirroredCoordsAreDistinct) {
    // Mirrored / swapped coords are a classic weak-hash collision ((x,z) vs (z,x), (-x,-z)).
    std::set<uint64_t> seen;
    for (int32_t x = -16; x <= 16; ++x) {
        for (int32_t z = -16; z <= 16; ++z) {
            seen.insert(chunkSeed(173173173ULL, ChunkCoord{x, z}));
        }
    }
    EXPECT_EQ(seen.size(), 33U * 33U);
}

TEST(ChunkSeed, ExtremeCoordinatesAreHandled) {
    const uint64_t a = chunkSeed(1ULL, ChunkCoord{INT32_MIN, INT32_MAX});
    const uint64_t b = chunkSeed(1ULL, ChunkCoord{INT32_MAX, INT32_MIN});
    EXPECT_NE(a, b);
}

TEST(ColumnHash, IsDeterministicAndPositionSensitive) {
    EXPECT_EQ(columnHash(7ULL, 10, -20, 3U), columnHash(7ULL, 10, -20, 3U));
    EXPECT_NE(columnHash(7ULL, 10, -20, 3U), columnHash(7ULL, -20, 10, 3U));
    EXPECT_NE(columnHash(7ULL, 10, -20, 3U), columnHash(7ULL, 10, -20, 4U)); // salt = independent stream
}
