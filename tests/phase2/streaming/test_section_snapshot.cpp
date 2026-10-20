#include "streaming/SectionNeighborhoodSnapshot18.hpp"

#include "StreamingTestHelpers.hpp"

#include <gtest/gtest.h>

#include <latch>
#include <optional>
#include <random>
#include <thread>
#include <type_traits>
#include <vector>

using betacraft::streaming::kMissingVoxel;
using betacraft::streaming::SectionNeighborhoodSnapshot18;
using betacraft::streaming::VoxelSample;
using betacraft::voxel::SectionCoord;
using betacraft::voxel::BlockId;
using betacraft::voxel::BlockStateId;
using betacraft::world::World;
using Snap = SectionNeighborhoodSnapshot18;

namespace {

constexpr BlockStateId state(BlockId id) noexcept { return static_cast<BlockStateId>(id); }

// Unique, reversible synthetic content per local coordinate.
VoxelSample syntheticVoxel(int32_t lx, int32_t ly, int32_t lz) {
    const uint32_t h = static_cast<uint32_t>((lx + 1) * 324 + (ly + 1) * 18 + (lz + 1));
    return VoxelSample{static_cast<BlockStateId>(h % 1000u), static_cast<uint8_t>(h % 16u),
                       static_cast<uint8_t>((h / 16u) % 16u)};
}

} // namespace

// ---------------------------------------------------------------- layout ----

TEST(SectionSnapshot, GeometryConstants) {
    EXPECT_EQ(Snap::STRIDE, 18);
    EXPECT_EQ(Snap::TOTAL_VOXELS, 5832);
    EXPECT_EQ(Snap::MIN_LOCAL, -1);
    EXPECT_EQ(Snap::MAX_LOCAL, 16);
}

TEST(SectionSnapshot, FootprintIsAboutSeventeenKilobytes) {
    // 5832 * 2 (block states) + 5832 (packed light) = 17 496 bytes, plus alignment padding.
    EXPECT_GE(sizeof(Snap), 17496u);
    EXPECT_LE(sizeof(Snap), 17496u + 128u);
    EXPECT_EQ(alignof(Snap), 64u);
}

TEST(SectionSnapshot, VoxelIndexIsBijectiveOverPaddedCube) {
    std::vector<int> hits(Snap::TOTAL_VOXELS, 0);
    for (int y = -1; y <= 16; ++y) {
        for (int z = -1; z <= 16; ++z) {
            for (int x = -1; x <= 16; ++x) {
                const size_t idx = Snap::voxelIndex(x, y, z);
                ASSERT_LT(idx, static_cast<size_t>(Snap::TOTAL_VOXELS));
                ++hits[idx];
            }
        }
    }
    for (int h : hits) {
        EXPECT_EQ(h, 1);
    }
    EXPECT_EQ(Snap::voxelIndex(-1, -1, -1), 0u);
    EXPECT_EQ(Snap::voxelIndex(16, 16, 16), static_cast<size_t>(Snap::TOTAL_VOXELS - 1));
    // X is the fastest-varying axis (cache-friendly row scans in the mesher).
    EXPECT_EQ(Snap::voxelIndex(1, 0, 0) - Snap::voxelIndex(0, 0, 0), 1u);
    EXPECT_EQ(Snap::voxelIndex(0, 0, 1) - Snap::voxelIndex(0, 0, 0), 18u);
    EXPECT_EQ(Snap::voxelIndex(0, 1, 0) - Snap::voxelIndex(0, 0, 0), 324u);
}

TEST(SectionSnapshot, InRangeBoundaries) {
    EXPECT_TRUE(Snap::inRange(-1, -1, -1));
    EXPECT_TRUE(Snap::inRange(16, 16, 16));
    EXPECT_FALSE(Snap::inRange(-2, 0, 0));
    EXPECT_FALSE(Snap::inRange(0, 17, 0));
    EXPECT_FALSE(Snap::inRange(0, 0, -2));
}

// ------------------------------------------------------- generic capture ----

TEST(SectionSnapshot, DefaultConstructedIsAllMissingVoxel) {
    const Snap snap;
    for (int y = -1; y <= 16; ++y) {
        for (int z = -1; z <= 16; ++z) {
            for (int x = -1; x <= 16; ++x) {
                ASSERT_EQ(snap.getBlockState(x, y, z), kMissingVoxel.state);
                ASSERT_EQ(snap.getSkyLight(x, y, z), kMissingVoxel.skyLight);
                ASSERT_EQ(snap.getBlockLight(x, y, z), kMissingVoxel.blockLight);
            }
        }
    }
    EXPECT_TRUE(snap.isInteriorEmpty());
}

TEST(SectionSnapshot, CaptureFromSamplesEveryVoxelIncludingBorder) {
    int calls = 0;
    const auto snap = Snap::captureFrom([&](int32_t x, int32_t y, int32_t z) -> std::optional<VoxelSample> {
        ++calls;
        return syntheticVoxel(x, y, z);
    });
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(calls, Snap::TOTAL_VOXELS);
    for (int y = -1; y <= 16; ++y) {
        for (int z = -1; z <= 16; ++z) {
            for (int x = -1; x <= 16; ++x) {
                const VoxelSample v = syntheticVoxel(x, y, z);
                ASSERT_EQ(snap->getBlockState(x, y, z), v.state) << x << "," << y << "," << z;
                ASSERT_EQ(snap->getSkyLight(x, y, z), v.skyLight);
                ASSERT_EQ(snap->getBlockLight(x, y, z), v.blockLight);
            }
        }
    }
}

TEST(SectionSnapshot, AbsentSamplesUseDefinedFallback) {
    const auto snap = Snap::captureFrom([](int32_t x, int32_t, int32_t) -> std::optional<VoxelSample> {
        if (x < 0) {
            return std::nullopt; // e.g. west neighbour not loaded
        }
        return VoxelSample{state(BlockId::Stone), 3, 7};
    });
    EXPECT_EQ(snap->getBlock(-1, 5, 5), BlockId::Air);
    EXPECT_EQ(snap->getSkyLight(-1, 5, 5), 15);
    EXPECT_EQ(snap->getBlockLight(-1, 5, 5), 0);
    EXPECT_EQ(snap->getBlock(0, 5, 5), BlockId::Stone);
    EXPECT_EQ(snap->getSkyLight(0, 5, 5), 3);
    EXPECT_EQ(snap->getBlockLight(0, 5, 5), 7);
}

TEST(SectionSnapshot, LightValuesAreClampedToFourBits) {
    const auto snap = Snap::captureFrom([](int32_t, int32_t, int32_t) -> std::optional<VoxelSample> {
        return VoxelSample{state(BlockId::Air), 0xFF, 0x1F};
    });
    EXPECT_EQ(snap->getSkyLight(3, 3, 3), 15);
    EXPECT_EQ(snap->getBlockLight(3, 3, 3), 15);
}

TEST(SectionSnapshot, OutOfRangeReadsReturnFallbackNotGarbage) {
    const auto snap = Snap::captureFrom([](int32_t, int32_t, int32_t) -> std::optional<VoxelSample> {
        return VoxelSample{state(BlockId::Stone), 2, 9};
    });
    EXPECT_EQ(snap->getBlock(17, 0, 0), BlockId::Air);
    EXPECT_EQ(snap->getBlock(0, -2, 0), BlockId::Air);
    EXPECT_EQ(snap->getSkyLight(0, 0, 40), kMissingVoxel.skyLight);
    EXPECT_EQ(snap->getBlockLight(-5, 0, 0), kMissingVoxel.blockLight);
}

TEST(SectionSnapshot, InteriorEmptinessIgnoresBorder) {
    const auto borderOnly = Snap::captureFrom([](int32_t x, int32_t y, int32_t z) -> std::optional<VoxelSample> {
        const bool interior = x >= 0 && x < 16 && y >= 0 && y < 16 && z >= 0 && z < 16;
        return VoxelSample{interior ? state(BlockId::Air) : state(BlockId::Stone), 15, 0};
    });
    EXPECT_TRUE(borderOnly->isInteriorEmpty());

    const auto oneBlock = Snap::captureFrom([](int32_t x, int32_t y, int32_t z) -> std::optional<VoxelSample> {
        return VoxelSample{(x == 15 && y == 0 && z == 7) ? state(BlockId::Torch) : state(BlockId::Air), 15, 0};
    });
    EXPECT_FALSE(oneBlock->isInteriorEmpty());
}

// ----------------------------------------------------------- immutability ----

template <typename T>
concept CanMutateVoxel = requires(T& s) { s.setVoxelDirect(size_t{0}, BlockStateId{0}, uint8_t{0}, uint8_t{0}); };

TEST(SectionSnapshot, SharedHandleIsConstAndExposesNoMutators) {
    using Handle = decltype(Snap::captureFrom([](int32_t, int32_t, int32_t) -> std::optional<VoxelSample> {
        return std::nullopt;
    }));
    static_assert(std::is_same_v<Handle, std::shared_ptr<const Snap>>);
    static_assert(!CanMutateVoxel<const Snap>);
    static_assert(!CanMutateVoxel<Snap>, "mutation is private: only capture factories may write");
    static_assert(std::is_trivially_copyable_v<Snap>);
    SUCCEED();
}

TEST(SectionSnapshot, ConcurrentReadersSeeIdenticalData) {
    const auto snap = Snap::captureFrom([](int32_t x, int32_t y, int32_t z) -> std::optional<VoxelSample> {
        return syntheticVoxel(x, y, z);
    });
    constexpr int kReaders = 4;
    std::latch go(kReaders);
    std::vector<uint64_t> sums(kReaders, 0);
    {
        std::vector<std::jthread> readers;
        for (int r = 0; r < kReaders; ++r) {
            readers.emplace_back([&, r] {
                go.arrive_and_wait();
                uint64_t s = 0;
                for (int y = -1; y <= 16; ++y)
                    for (int z = -1; z <= 16; ++z)
                        for (int x = -1; x <= 16; ++x)
                            s += snap->getBlockState(x, y, z) * 31u + snap->getSkyLight(x, y, z) * 7u +
                                 snap->getBlockLight(x, y, z);
                sums[static_cast<size_t>(r)] = s;
            });
        }
    }
    for (int r = 1; r < kReaders; ++r) {
        EXPECT_EQ(sums[static_cast<size_t>(r)], sums[0]);
    }
}

// ---------------------------------------------------------- World capture ----

class SnapshotWorldCapture : public ::testing::Test {
protected:
    void SetUp() override {
        betacraft::voxel::BlockRegistry::initialize();
        m_world = std::make_unique<World>(1234ULL, m_dir.path());
    }

    // Fills chunk (cx, cz) with a deterministic pattern unique to each world voxel.
    void loadPatternChunk(int32_t cx, int32_t cz) {
        auto& chunk = m_world->ensureChunkLoaded({cx, cz});
        for (int y = 0; y < 128; ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) {
                    const int wx = cx * 16 + x;
                    const int wz = cz * 16 + z;
                    chunk.setBlock(x, y, z, patternBlock(wx, y, wz), false);
                    chunk.setSkyLight(x, y, z, static_cast<uint8_t>((wx + y) & 15));
                    chunk.setBlockLight(x, y, z, static_cast<uint8_t>((wz * 3 + y) & 15));
                }
    }

    static BlockId patternBlock(int wx, int wy, int wz) {
        static constexpr BlockId palette[] = {BlockId::Stone, BlockId::Dirt, BlockId::Air, BlockId::Glass,
                                              BlockId::Water, BlockId::OakPlanks, BlockId::Torch};
        // Multiply in unsigned arithmetic: signed overflow here would be UB.
        const unsigned h = (static_cast<unsigned>(wx) * 73856093u) ^ (static_cast<unsigned>(wy) * 19349663u) ^
                           (static_cast<unsigned>(wz) * 83492791u);
        return palette[h % 7u];
    }

    betacraft::streaming::test::ScopedTempDir m_dir;
    std::unique_ptr<World> m_world;
};

TEST_F(SnapshotWorldCapture, MatchesWorldGettersEverywhereWithAllNeighboursLoaded) {
    for (int cz = -1; cz <= 1; ++cz)
        for (int cx = -1; cx <= 1; ++cx)
            loadPatternChunk(cx, cz);

    for (int sy : {0, 3, 7}) {
        const SectionCoord sc{0, sy, 0};
        const auto snap = Snap::capture(*m_world, sc);
        ASSERT_NE(snap, nullptr);
        const glm::ivec3 o = sc.worldOrigin();
        for (int y = -1; y <= 16; ++y)
            for (int z = -1; z <= 16; ++z)
                for (int x = -1; x <= 16; ++x) {
                    const int wx = o.x + x, wy = o.y + y, wz = o.z + z;
                    ASSERT_EQ(snap->getBlock(x, y, z), m_world->getBlock(wx, wy, wz))
                        << "section " << sy << " local " << x << "," << y << "," << z;
                    ASSERT_EQ(snap->getSkyLight(x, y, z), m_world->getSkyLight(wx, wy, wz));
                    ASSERT_EQ(snap->getBlockLight(x, y, z), m_world->getBlockLight(wx, wy, wz));
                }
    }
}

TEST_F(SnapshotWorldCapture, BorderComesFromNeighbourChunks) {
    loadPatternChunk(0, 0);
    loadPatternChunk(1, 0);
    loadPatternChunk(0, -1);
    const auto snap = Snap::capture(*m_world, SectionCoord{0, 4, 0});
    // +X border reads chunk (1,0) local x = 0.
    EXPECT_EQ(snap->getBlock(16, 3, 5), patternBlock(16, 4 * 16 + 3, 5));
    // -Z border reads chunk (0,-1) local z = 15.
    EXPECT_EQ(snap->getBlock(5, 3, -1), patternBlock(5, 4 * 16 + 3, -1));
    // Vertical borders come from the adjacent sections of the same chunk.
    EXPECT_EQ(snap->getBlock(5, -1, 5), patternBlock(5, 4 * 16 - 1, 5));
    EXPECT_EQ(snap->getBlock(5, 16, 5), patternBlock(5, 5 * 16, 5));
}

TEST_F(SnapshotWorldCapture, MissingNeighbourChunksFallBackToAirAndOpenSky) {
    loadPatternChunk(0, 0); // no neighbours loaded
    const auto snap = Snap::capture(*m_world, SectionCoord{0, 2, 0});
    for (int y = -1; y <= 16; ++y)
        for (int z = -1; z <= 16; ++z) {
            EXPECT_EQ(snap->getBlock(-1, y, z), BlockId::Air);
            EXPECT_EQ(snap->getSkyLight(-1, y, z), 15);
            EXPECT_EQ(snap->getBlockLight(-1, y, z), 0);
            EXPECT_EQ(snap->getBlock(16, y, z), BlockId::Air);
        }
    // Diagonal corner column (-1,-1) is also absent.
    EXPECT_EQ(snap->getBlock(-1, 0, -1), BlockId::Air);
    EXPECT_EQ(snap->getSkyLight(16, 0, 16), 15);
}

TEST_F(SnapshotWorldCapture, WorldVerticalLimitsFollowWorldSemantics) {
    loadPatternChunk(0, 0);
    const auto top = Snap::capture(*m_world, SectionCoord{0, 7, 0});
    EXPECT_EQ(top->getBlock(4, 16, 4), BlockId::Air);  // y = 128 is above the world
    EXPECT_EQ(top->getSkyLight(4, 16, 4), 15);         // open sky above the build limit
    EXPECT_EQ(top->getBlockLight(4, 16, 4), 0);

    const auto bottom = Snap::capture(*m_world, SectionCoord{0, 0, 0});
    EXPECT_EQ(bottom->getBlock(4, -1, 4), BlockId::Air); // y = -1 is below the world
    EXPECT_EQ(bottom->getSkyLight(4, -1, 4), 0);
    EXPECT_EQ(bottom->getBlockLight(4, -1, 4), 0);
}

TEST_F(SnapshotWorldCapture, UnloadedCentreChunkYieldsEmptyInterior) {
    const auto snap = Snap::capture(*m_world, SectionCoord{5, 3, 5});
    ASSERT_NE(snap, nullptr);
    EXPECT_TRUE(snap->isInteriorEmpty());
    EXPECT_EQ(snap->getSkyLight(8, 8, 8), 15);
}

TEST_F(SnapshotWorldCapture, LaterWorldEditsDoNotAffectCapturedSnapshot) {
    loadPatternChunk(0, 0);
    loadPatternChunk(1, 0);
    const auto snap = Snap::capture(*m_world, SectionCoord{0, 4, 0});
    const BlockId interiorBefore = snap->getBlock(3, 3, 3);
    const BlockId borderBefore = snap->getBlock(16, 3, 3);
    const uint8_t skyBefore = snap->getSkyLight(3, 3, 3);

    auto* c0 = m_world->getChunkMutable({0, 0});
    auto* c1 = m_world->getChunkMutable({1, 0});
    c0->setBlock(3, 4 * 16 + 3, 3, interiorBefore == BlockId::Bedrock ? BlockId::Sand : BlockId::Bedrock, false);
    c0->setSkyLight(3, 4 * 16 + 3, 3, static_cast<uint8_t>((skyBefore + 1) & 15));
    c1->setBlock(0, 4 * 16 + 3, 3, borderBefore == BlockId::Bedrock ? BlockId::Sand : BlockId::Bedrock, false);

    EXPECT_EQ(snap->getBlock(3, 3, 3), interiorBefore);
    EXPECT_EQ(snap->getBlock(16, 3, 3), borderBefore);
    EXPECT_EQ(snap->getSkyLight(3, 3, 3), skyBefore);
}

TEST_F(SnapshotWorldCapture, RandomizedProbeAgainstWorldAtNegativeCoords) {
    for (int cz = -3; cz <= -1; ++cz)
        for (int cx = -3; cx <= -1; ++cx)
            loadPatternChunk(cx, cz);
    std::mt19937 rng(0xC0FFEEu);
    std::uniform_int_distribution<int> sec(0, 7);
    std::uniform_int_distribution<int> loc(-1, 16);
    const SectionCoord sc{-2, sec(rng), -2};
    const auto snap = Snap::capture(*m_world, sc);
    const glm::ivec3 o = sc.worldOrigin();
    for (int i = 0; i < 2000; ++i) {
        const int x = loc(rng), y = loc(rng), z = loc(rng);
        ASSERT_EQ(snap->getBlock(x, y, z), m_world->getBlock(o.x + x, o.y + y, o.z + z));
        ASSERT_EQ(snap->getSkyLight(x, y, z), m_world->getSkyLight(o.x + x, o.y + y, o.z + z));
        ASSERT_EQ(snap->getBlockLight(x, y, z), m_world->getBlockLight(o.x + x, o.y + y, o.z + z));
    }
}

TEST_F(SnapshotWorldCapture, VerticalLimitsTakePrecedenceOverMissingNeighbourColumns) {
    loadPatternChunk(0, 0); // (-1, 0) and (1, 0) columns are absent
    const auto bottom = Snap::capture(*m_world, SectionCoord{0, 0, 0});
    EXPECT_EQ(bottom->getSkyLight(-1, -1, 4), m_world->getSkyLight(-1, -1, 4));
    EXPECT_EQ(bottom->getSkyLight(-1, -1, 4), 0);
    EXPECT_EQ(bottom->getSkyLight(16, -1, -1), 0);
    const auto top = Snap::capture(*m_world, SectionCoord{0, 7, 0});
    EXPECT_EQ(top->getSkyLight(-1, 16, 4), 15);
}
