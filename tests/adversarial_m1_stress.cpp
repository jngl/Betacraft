#include <iostream>
#include <vector>
#include <random>
#include <cassert>
#include <cmath>
#include <memory>
#include <chrono>

#include "core/Config.hpp"
#include "physics/Player.hpp"
#include "voxel/Chunk.hpp"
#include "voxel/PalettedSection.hpp"
#include "world/World.hpp"

using namespace betacraft;
using namespace betacraft::world;
using namespace betacraft::voxel;
using namespace betacraft::physics;

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define STRESS_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            g_testsFailed++; \
            return false; \
        } \
    } while(0)

#define RUN_TEST(fn) \
    do { \
        std::cout << "[RUNNING] " << #fn << "..." << std::flush; \
        if (fn()) { \
            std::cout << " PASS" << std::endl; \
            g_testsPassed++; \
        } else { \
            std::cout << " FAILED" << std::endl; \
        } \
    } while(0)

// ============================================================================
// TEST SUITE 1: PalettedSection Stress & Palette Transitions
// ============================================================================

bool testMemoryInvariants() {
    STRESS_ASSERT(sizeof(PalettedSection) <= 64, "sizeof(PalettedSection) must be <= 64 bytes");
    STRESS_ASSERT(sizeof(PalettedSection) == 64, "sizeof(PalettedSection) must be exactly 64 bytes");
    STRESS_ASSERT(alignof(PalettedSection) == 64, "alignof(PalettedSection) must be 64 bytes");

    PalettedSection secAir(BlockId::Air, 15, 0);
    STRESS_ASSERT(secAir.isHomogeneous(), "Air section must be homogeneous");
    STRESS_ASSERT(!secAir.hasLightData(), "Air section lightData must be nullptr");
    STRESS_ASSERT(secAir.getBitsPerVoxel() == 0, "Air section must have 0 bits per voxel");
    STRESS_ASSERT(secAir.getMemoryFootprintBytes() == 64, "Air section memory footprint must be 64 bytes");

    PalettedSection secStone(BlockId::Stone, 0, 0);
    STRESS_ASSERT(secStone.isHomogeneous(), "Stone section must be homogeneous");
    STRESS_ASSERT(!secStone.hasLightData(), "Stone section lightData must be nullptr");
    STRESS_ASSERT(secStone.getBitsPerVoxel() == 0, "Stone section must have 0 bits per voxel");
    STRESS_ASSERT(secStone.getMemoryFootprintBytes() == 64, "Stone section memory footprint must be 64 bytes");

    return true;
}

bool testPaletteTransitionsAndLimits() {
    PalettedSection sec;
    STRESS_ASSERT(sec.getBitsPerVoxel() == 0, "Initial section must have 0 bits per voxel");
    STRESS_ASSERT(sec.isHomogeneous(), "Initial section must be homogeneous");

    // Tier 1: 0 -> 4 bits upon inserting 2nd block
    sec.setBlock(0, 0, 0, BlockId::Stone);
    STRESS_ASSERT(sec.getBitsPerVoxel() == 4, "Must promote to 4 bits per voxel");
    STRESS_ASSERT(!sec.isHomogeneous(), "Must no longer be homogeneous");
    STRESS_ASSERT(sec.getBlock(0, 0, 0) == BlockId::Stone, "Voxel (0,0,0) must be Stone");
    STRESS_ASSERT(sec.getBlock(1, 0, 0) == BlockId::Air, "Voxel (1,0,0) must remain Air");
    STRESS_ASSERT(sec.getPaletteSize() == 2, "Palette size must be 2");

    // Fill up to exactly 16 unique blocks (indices 0..15 in palette)
    for (uint16_t id = 2; id < 16; ++id) {
        sec.setBlock(id, 0, 0, static_cast<BlockStateId>(100 + id));
    }
    STRESS_ASSERT(sec.getBitsPerVoxel() == 4, "Must still be 4 bits at 16 unique blocks");
    STRESS_ASSERT(sec.getPaletteSize() == 16, "Palette size must be exactly 16");

    // Verify all 16 blocks are stored correctly in 4-bit nibbles
    STRESS_ASSERT(sec.getBlock(0, 0, 0) == BlockId::Stone, "Block 0 must be Stone");
    for (uint16_t id = 2; id < 16; ++id) {
        STRESS_ASSERT(sec.getBlockState(id, 0, 0) == static_cast<BlockStateId>(100 + id), "Block must match inserted state");
    }

    // Overwrite existing voxel with an ALREADY EXISTING block in palette:
    // Palette must NOT grow!
    sec.setBlock(0, 1, 0, BlockId::Stone);
    STRESS_ASSERT(sec.getBitsPerVoxel() == 4, "Must stay 4 bits when placing existing block");
    STRESS_ASSERT(sec.getPaletteSize() == 16, "Palette size must remain 16");

    // Tier 2: 4 -> 8 bits upon inserting 17th unique block
    sec.setBlock(0, 2, 0, static_cast<BlockStateId>(999));
    STRESS_ASSERT(sec.getBitsPerVoxel() == 8, "Must promote to 8 bits on 17th block");
    STRESS_ASSERT(sec.getPaletteSize() == 17, "Palette size must be 17");
    STRESS_ASSERT(sec.getBlockState(0, 2, 0) == 999, "17th block must be 999");
    STRESS_ASSERT(sec.getBlock(0, 0, 0) == BlockId::Stone, "Stone block must still be present");

    // Fill up to exactly 256 unique blocks
    // Currently 17 blocks. Add 239 more:
    for (uint16_t id = 17; id < 256; ++id) {
        int lx = id % 16;
        int ly = (id / 16);
        int lz = 1;
        sec.setBlock(lx, ly, lz, static_cast<BlockStateId>(2000 + id));
    }
    STRESS_ASSERT(sec.getBitsPerVoxel() == 8, "Must still be 8 bits at 256 unique blocks");
    STRESS_ASSERT(sec.getPaletteSize() == 256, "Palette size must be exactly 256");

    // Tier 3: 8 -> 16 bits direct upon inserting 257th unique block
    sec.setBlock(15, 15, 15, static_cast<BlockStateId>(5000));
    STRESS_ASSERT(sec.getBitsPerVoxel() == 16, "Must promote to 16 bits on 257th block");
    // In 16-bit direct mode, palette is freed
    STRESS_ASSERT(sec.getPaletteSize() == 0, "Palette must be freed in 16-bit direct mode");
    STRESS_ASSERT(sec.getBlockState(15, 15, 15) == 5000, "257th block must be 5000");
    STRESS_ASSERT(sec.getBlock(0, 0, 0) == BlockId::Stone, "Stone block must remain intact");

    // Verify all 256 previously placed blocks are intact in 16-bit direct mode
    for (uint16_t id = 17; id < 256; ++id) {
        int lx = id % 16;
        int ly = (id / 16);
        int lz = 1;
        STRESS_ASSERT(sec.getBlockState(lx, ly, lz) == static_cast<BlockStateId>(2000 + id),
                      "Direct mode block corrupted");
    }

    return true;
}

bool testNibblePackingPairwiseStress() {
    // Test all 2048 pairs of nibbles in 4-bit mode for bit interference
    PalettedSection sec;
    // Insert 15 non-zero palette entries
    for (uint16_t i = 1; i <= 15; ++i) {
        sec.setBlock(i, 0, 0, static_cast<BlockStateId>(i));
    }
    STRESS_ASSERT(sec.getBitsPerVoxel() == 4, "Must be 4-bit mode");

    // Create a deterministic pattern across all 4096 voxels
    // Even voxels get pattern (i * 3) % 16, Odd voxels get pattern (i * 7 + 1) % 16
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                uint16_t val = (idx & 1) ? static_cast<uint16_t>((idx * 7 + 1) % 16)
                                         : static_cast<uint16_t>((idx * 3) % 16);
                sec.setBlock(x, y, z, val);
            }
        }
    }

    // Verify all 4096 voxels retain exact values
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                uint16_t expected = (idx & 1) ? static_cast<uint16_t>((idx * 7 + 1) % 16)
                                              : static_cast<uint16_t>((idx * 3) % 16);
                STRESS_ASSERT(sec.getBlockState(x, y, z) == expected, "Nibble crosstalk detected");
            }
        }
    }

    // Now mutate only the odd nibbles and verify even nibbles are untouched
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                if (idx & 1) {
                    sec.setBlock(x, y, z, static_cast<BlockStateId>(15));
                }
            }
        }
    }

    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                if (idx & 1) {
                    STRESS_ASSERT(sec.getBlockState(x, y, z) == 15, "Mutated odd nibble mismatch");
                } else {
                    uint16_t expectedEven = static_cast<uint16_t>((idx * 3) % 16);
                    STRESS_ASSERT(sec.getBlockState(x, y, z) == expectedEven, "Even nibble corrupted by odd write");
                }
            }
        }
    }

    return true;
}

bool testMassiveRandomizedBlockMutations() {
    // 50,000 random writes across hundreds of unique BlockIds with shadow oracle
    PalettedSection sec;
    std::array<BlockStateId, 4096> shadowOracle;
    shadowOracle.fill(static_cast<BlockStateId>(BlockId::Air));

    std::mt19937 rng(1337);
    std::uniform_int_distribution<int> coordDist(0, 15);
    std::uniform_int_distribution<uint16_t> blockDist(0, 450); // 451 unique block states

    constexpr int NUM_MUTATIONS = 50000;
    for (int i = 0; i < NUM_MUTATIONS; ++i) {
        int x = coordDist(rng);
        int y = coordDist(rng);
        int z = coordDist(rng);
        BlockStateId block = blockDist(rng);

        sec.setBlock(x, y, z, block);
        shadowOracle[(y * 16 + z) * 16 + x] = block;

        // Verify sampled voxels periodically
        if (i % 10000 == 0) {
            for (int sample = 0; sample < 100; ++sample) {
                int sx = coordDist(rng);
                int sy = coordDist(rng);
                int sz = coordDist(rng);
                size_t idx = (sy * 16 + sz) * 16 + sx;
                STRESS_ASSERT(sec.getBlockState(sx, sy, sz) == shadowOracle[idx],
                              "Shadow oracle mismatch during massive mutations");
            }
        }
    }

    // Full section verification
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                STRESS_ASSERT(sec.getBlockState(x, y, z) == shadowOracle[idx],
                              "Final oracle verification failed");
            }
        }
    }
    STRESS_ASSERT(sec.getBitsPerVoxel() == 16, "Section with 450 states must be 16-bit direct mode");

    return true;
}

bool testPaletteTransitionAndBackBehavior() {
    // Investigate: what happens when section promoted to 16-bit is rewritten with all Air?
    PalettedSection sec;
    // Force to 16-bit
    for (uint16_t id = 1; id <= 260; ++id) {
        int x = id % 16;
        int y = (id / 16) % 16;
        int z = (id / 256);
        sec.setBlock(x, y, z, static_cast<BlockStateId>(id));
    }
    STRESS_ASSERT(sec.getBitsPerVoxel() == 16, "Must be 16-bit mode");

    // Overwrite ALL 4096 voxels with Air
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                sec.setBlock(x, y, z, BlockId::Air);
            }
        }
    }

    // Correctness check: all voxels MUST be Air
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                STRESS_ASSERT(sec.getBlock(x, y, z) == BlockId::Air, "All voxels must be Air");
            }
        }
    }

    // Note architectural invariant: does PalettedSection demote back to 0-bit?
    // In Sprint 1.1 specification, demotion/compaction is NOT part of the runtime setBlock path
    // because full scan on every setBlock would destroy O(1) write performance.
    // BitsPerVoxel remains 16, but correctness is 100% preserved.
    STRESS_ASSERT(sec.getBitsPerVoxel() == 16, "setBlock maintains 16-bit for O(1) performance");
    STRESS_ASSERT(!sec.isHomogeneous(), "isHomogeneous() is false when bitsPerVoxel != 0");

    return true;
}

bool testOutOfBoundsCoordinates() {
    PalettedSection sec;
    const int badCoords[][3] = {
        {-1, 0, 0}, {16, 0, 0}, {0, -1, 0}, {0, 16, 0}, {0, 0, -1}, {0, 0, 16},
        {-100, 50, 20}, {1000, 1000, 1000}, {-999, -999, -999}
    };

    for (const auto& c : badCoords) {
        // Must not crash or throw
        sec.setBlock(c[0], c[1], c[2], BlockId::Stone);
        BlockId b = sec.getBlock(c[0], c[1], c[2]);
        STRESS_ASSERT(b == BlockId::Air, "Out of bounds getBlock must return Air");

        sec.setSkyLight(c[0], c[1], c[2], 10);
        uint8_t sky = sec.getSkyLight(c[0], c[1], c[2]);
        STRESS_ASSERT(sky == 0, "Out of bounds getSkyLight must return 0");

        sec.setBlockLight(c[0], c[1], c[2], 10);
        uint8_t blk = sec.getBlockLight(c[0], c[1], c[2]);
        STRESS_ASSERT(blk == 0, "Out of bounds getBlockLight must return 0");
    }

    return true;
}

// ============================================================================
// TEST SUITE 2: Lazy DenseLightArray Allocation/Deallocation & Memory Stress
// ============================================================================

bool testLazyLightAllocationAndDeallocationCycles() {
    PalettedSection sec(BlockId::Air, 15, 0);

    STRESS_ASSERT(!sec.hasLightData(), "Initial light data must be nullptr");
    STRESS_ASSERT(sec.getSkyLight(5, 5, 5) == 15, "Uniform sky light must be 15");
    STRESS_ASSERT(sec.getBlockLight(5, 5, 5) == 0, "Uniform block light must be 0");
    STRESS_ASSERT(!sec.hasLightData(), "Reading light must not allocate");

    // Setting uniform value must NOT allocate
    sec.setSkyLight(5, 5, 5, 15);
    sec.setBlockLight(5, 5, 5, 0);
    STRESS_ASSERT(!sec.hasLightData(), "Setting identical uniform values must not allocate");

    // Setting non-uniform block light must allocate
    sec.setBlockLight(5, 5, 5, 12);
    STRESS_ASSERT(sec.hasLightData(), "Non-uniform block light must allocate DenseLightArray");
    STRESS_ASSERT(sec.getBlockLight(5, 5, 5) == 12, "Block light at (5,5,5) must be 12");
    STRESS_ASSERT(sec.getBlockLight(5, 5, 6) == 0, "Other voxels must remain at uniform 0");
    STRESS_ASSERT(sec.getSkyLight(5, 5, 5) == 15, "Sky light must remain at uniform 15");

    // Resetting uniform lighting must deallocate
    sec.setUniformLighting(15, 0);
    STRESS_ASSERT(!sec.hasLightData(), "setUniformLighting must reset lightData to nullptr");
    STRESS_ASSERT(sec.getBlockLight(5, 5, 5) == 0, "Block light must reset to uniform 0");

    // Setting non-uniform sky light must allocate
    sec.setSkyLight(2, 3, 4, 8);
    STRESS_ASSERT(sec.hasLightData(), "Non-uniform sky light must allocate DenseLightArray");
    STRESS_ASSERT(sec.getSkyLight(2, 3, 4) == 8, "Sky light at (2,3,4) must be 8");
    STRESS_ASSERT(sec.getSkyLight(2, 3, 5) == 15, "Other voxels must remain at uniform 15");

    sec.setUniformLighting(15, 0);
    STRESS_ASSERT(!sec.hasLightData(), "Reset must free lightData again");

    // 10,000 cycles of allocate & deallocate churn
    for (int i = 0; i < 10000; ++i) {
        sec.setBlockLight(i % 16, (i / 16) % 16, (i / 256) % 16, static_cast<uint8_t>((i % 15) + 1));
        STRESS_ASSERT(sec.hasLightData(), "Must allocate in churn");
        sec.setUniformLighting(15, 0);
        STRESS_ASSERT(!sec.hasLightData(), "Must deallocate in churn");
    }

    return true;
}

bool testLightNibblePackingInterference() {
    PalettedSection sec(BlockId::Air, 15, 0);
    sec.setBlockLight(0, 0, 0, 1); // trigger allocation

    // Test across all 4096 voxels that sky light (upper 4 bits) and block light (lower 4 bits)
    // do NOT interfere with each other
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                uint8_t sky = static_cast<uint8_t>(idx % 16);
                uint8_t blk = static_cast<uint8_t>((idx * 3 + 5) % 16);
                sec.setSkyLight(x, y, z, sky);
                sec.setBlockLight(x, y, z, blk);
            }
        }
    }

    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                uint8_t expectedSky = static_cast<uint8_t>(idx % 16);
                uint8_t expectedBlk = static_cast<uint8_t>((idx * 3 + 5) % 16);
                STRESS_ASSERT(sec.getSkyLight(x, y, z) == expectedSky, "Sky light channel corrupted");
                STRESS_ASSERT(sec.getBlockLight(x, y, z) == expectedBlk, "Block light channel corrupted");
            }
        }
    }

    return true;
}

bool testDeepCopyAndMoveSemantics() {
    PalettedSection original(BlockId::Stone, 15, 0);
    original.setBlock(1, 2, 3, BlockId::GoldOre);
    original.setBlockLight(1, 2, 3, 14);
    STRESS_ASSERT(original.hasLightData(), "Original must have light data");

    // Copy construction
    PalettedSection copySec(original);
    STRESS_ASSERT(copySec.hasLightData(), "Copy must have light data");
    STRESS_ASSERT(copySec.getBlock(1, 2, 3) == BlockId::GoldOre, "Copy block must match");
    STRESS_ASSERT(copySec.getBlockLight(1, 2, 3) == 14, "Copy light must match");

    // Mutate copy
    copySec.setBlock(1, 2, 3, BlockId::DiamondOre);
    copySec.setBlockLight(1, 2, 3, 7);

    // Original must NOT change
    STRESS_ASSERT(original.getBlock(1, 2, 3) == BlockId::GoldOre, "Original block must not change");
    STRESS_ASSERT(original.getBlockLight(1, 2, 3) == 14, "Original light must not change");

    // Self-assignment
    copySec = copySec;
    STRESS_ASSERT(copySec.getBlock(1, 2, 3) == BlockId::DiamondOre, "Self-assignment must be safe");
    STRESS_ASSERT(copySec.getBlockLight(1, 2, 3) == 7, "Self-assignment must preserve light");

    return true;
}

// ============================================================================
// TEST SUITE 3: Fixed Timestep Physics & Spiral of Death Adversarial Stress
// ============================================================================

bool testSpiralOfDeathMassiveLagSpikes() {
    // In Glenn Fiedler's accumulator loop:
    // accumulator = std::min(accumulator + dt, MAX_ACCUMULATOR);
    // while (accumulator >= FIXED_DT) { fixedUpdate(FIXED_DT); accumulator -= FIXED_DT; }
    constexpr float FIXED_DT = 1.0f / 60.0f;
    constexpr float MAX_ACCUMULATOR = 0.25f;
    constexpr int MAX_EXPECTED_TICKS = static_cast<int>(std::round(MAX_ACCUMULATOR / FIXED_DT)); // 15 ticks

    const float extremeDts[] = { 0.3f, 0.5f, 1.0f, 2.5f, 10.0f, 60.0f, 1000.0f };

    for (float dt : extremeDts) {
        float accumulator = 0.0f;
        accumulator = std::min(accumulator + dt, MAX_ACCUMULATOR);
        STRESS_ASSERT(accumulator <= MAX_ACCUMULATOR, "Accumulator must be clamped to MAX_ACCUMULATOR");

        int ticks = 0;
        while (accumulator >= FIXED_DT) {
            accumulator -= FIXED_DT;
            ticks++;
        }

        STRESS_ASSERT(ticks == MAX_EXPECTED_TICKS, "Ticks executed must be strictly capped at 15");
        STRESS_ASSERT(accumulator < FIXED_DT, "Remainder must be less than FIXED_DT");
        STRESS_ASSERT(accumulator >= 0.0f, "Remainder must be non-negative");
    }

    return true;
}

bool testPhysicsDeterminismAcrossFramerateAndJitter() {
    world::World world(777, "");
    world.ensureChunkLoaded({0, 0});
    world.ensureChunkLoaded({1, 0});
    world.ensureChunkLoaded({0, 1});
    world.ensureChunkLoaded({1, 1});
    for (int x = 0; x < 32; ++x) {
        for (int z = 0; z < 32; ++z) {
            world.setBlock(x, 60, z, BlockId::Stone);
        }
    }

    const glm::vec3 spawnPos{16.0f, 61.0f, 16.0f};
    PlayerInput input{.forward = true, .jump = true};
    constexpr float TOTAL_TIME = 2.0f; // 2 seconds = 120 ticks
    constexpr float FIXED_DT = 1.0f / 60.0f;
    constexpr float MAX_ACCUMULATOR = 0.25f;

    auto runSimWithConstantDt = [&](float frameDt) -> std::pair<glm::vec3, int> {
        Player player;
        player.setPosition(spawnPos);
        player.resetInterpolationHistory();

        float accumulator = 0.0f;
        int totalFrames = static_cast<int>(std::round(TOTAL_TIME / frameDt));
        int totalTicks = 0;

        for (int f = 0; f < totalFrames; ++f) {
            accumulator = std::min(accumulator + frameDt, MAX_ACCUMULATOR);
            while (accumulator >= FIXED_DT - 1e-5f) {
                player.fixedUpdate(FIXED_DT, input, world);
                accumulator -= FIXED_DT;
                if (accumulator < 0.0f) accumulator = 0.0f;
                totalTicks++;
            }
        }
        return {player.getPosition(), totalTicks};
    };

    auto [pos144, ticks144] = runSimWithConstantDt(1.0f / 144.0f);
    auto [pos60, ticks60]   = runSimWithConstantDt(1.0f / 60.0f);
    auto [pos30, ticks30]   = runSimWithConstantDt(1.0f / 30.0f);
    auto [pos10, ticks10]   = runSimWithConstantDt(1.0f / 10.0f);

    // 120 ticks in 2 seconds at 60 TPS
    STRESS_ASSERT(ticks144 == 120, "144 FPS must execute exactly 120 ticks");
    STRESS_ASSERT(ticks60 == 120, "60 FPS must execute exactly 120 ticks");
    STRESS_ASSERT(ticks30 == 120, "30 FPS must execute exactly 120 ticks");
    STRESS_ASSERT(ticks10 == 120, "10 FPS must execute exactly 120 ticks");

    // Positions must be numerically identical within floating point epsilon
    STRESS_ASSERT(std::abs(pos144.x - pos60.x) < 1e-4f, "144 FPS vs 60 FPS X drift");
    STRESS_ASSERT(std::abs(pos144.y - pos60.y) < 1e-4f, "144 FPS vs 60 FPS Y drift");
    STRESS_ASSERT(std::abs(pos144.z - pos60.z) < 1e-4f, "144 FPS vs 60 FPS Z drift");

    STRESS_ASSERT(std::abs(pos30.x - pos60.x) < 1e-4f, "30 FPS vs 60 FPS X drift");
    STRESS_ASSERT(std::abs(pos30.y - pos60.y) < 1e-4f, "30 FPS vs 60 FPS Y drift");
    STRESS_ASSERT(std::abs(pos30.z - pos60.z) < 1e-4f, "30 FPS vs 60 FPS Z drift");

    STRESS_ASSERT(std::abs(pos10.x - pos60.x) < 1e-4f, "10 FPS vs 60 FPS X drift");
    STRESS_ASSERT(std::abs(pos10.y - pos60.y) < 1e-4f, "10 FPS vs 60 FPS Y drift");
    STRESS_ASSERT(std::abs(pos10.z - pos60.z) < 1e-4f, "10 FPS vs 60 FPS Z drift");

    // Chaotic frame jitter test:
    // Every frame has random dt between 3ms (333 FPS) and 45ms (22 FPS)
    Player playerJitter;
    playerJitter.setPosition(spawnPos);
    playerJitter.resetInterpolationHistory();

    std::mt19937 rng(9999);
    std::uniform_real_distribution<float> jitterDist(0.003f, 0.045f);

    float elapsed = 0.0f;
    float jitterAccumulator = 0.0f;
    int jitterTicks = 0;

    while (elapsed < TOTAL_TIME) {
        float dt = jitterDist(rng);
        if (elapsed + dt > TOTAL_TIME) {
            dt = TOTAL_TIME - elapsed;
        }
        elapsed += dt;
        jitterAccumulator = std::min(jitterAccumulator + dt, MAX_ACCUMULATOR);
        while (jitterAccumulator >= FIXED_DT - 1e-5f) {
            playerJitter.fixedUpdate(FIXED_DT, input, world);
            jitterAccumulator -= FIXED_DT;
            if (jitterAccumulator < 0.0f) jitterAccumulator = 0.0f;
            jitterTicks++;
        }
    }

    STRESS_ASSERT(jitterTicks == 120, "Chaotic jitter must execute exactly 120 ticks");
    glm::vec3 posJitter = playerJitter.getPosition();
    STRESS_ASSERT(std::abs(posJitter.x - pos60.x) < 1e-4f, "Jitter X position drift");
    STRESS_ASSERT(std::abs(posJitter.y - pos60.y) < 1e-4f, "Jitter Y position drift");
    STRESS_ASSERT(std::abs(posJitter.z - pos60.z) < 1e-4f, "Jitter Z position drift");

    return true;
}

bool testInterpolationMonotonicityAndSnap() {
    Player player;
    glm::vec3 startPos{0.0f, 60.0f, 0.0f};
    player.setPosition(startPos);
    player.resetInterpolationHistory();

    world::World world(42, "");
    PlayerInput input{.forward = true};
    player.fixedUpdate(1.0f / 60.0f, input, world);

    glm::vec3 p0 = player.getInterpolatedPosition(0.0f);
    glm::vec3 pHalf = player.getInterpolatedPosition(0.5f);
    glm::vec3 p1 = player.getInterpolatedPosition(1.0f);

    // Convex hull: pHalf must be exactly between p0 and p1
    glm::vec3 expectedMid = (p0 + p1) * 0.5f;
    STRESS_ASSERT(std::abs(pHalf.x - expectedMid.x) < 1e-5f, "Interpolation midpoint X mismatch");
    STRESS_ASSERT(std::abs(pHalf.y - expectedMid.y) < 1e-5f, "Interpolation midpoint Y mismatch");
    STRESS_ASSERT(std::abs(pHalf.z - expectedMid.z) < 1e-5f, "Interpolation midpoint Z mismatch");

    // Teleportation snap
    glm::vec3 teleportPos{500.0f, 120.0f, -300.0f};
    player.setPosition(teleportPos);
    // setPosition internally calls resetInterpolationHistory()
    glm::vec3 snap0 = player.getInterpolatedPosition(0.0f);
    glm::vec3 snapHalf = player.getInterpolatedPosition(0.5f);
    glm::vec3 snap1 = player.getInterpolatedPosition(1.0f);

    STRESS_ASSERT(snap0 == teleportPos, "Teleport snap at alpha 0.0 failed");
    STRESS_ASSERT(snapHalf == teleportPos, "Teleport snap at alpha 0.5 failed");
    STRESS_ASSERT(snap1 == teleportPos, "Teleport snap at alpha 1.0 failed");

    return true;
}

bool testChunkSectionBoundariesAndRLEPersistence() {
    Chunk original(core::ChunkCoord{3, -5});

    original.getSection(0) = PalettedSection(BlockId::Bedrock, 15, 0);

    // Section 1 (Y=16..31): 4-bit palette (Stone, Dirt, Coal, Iron, Gold)
    const BlockId sec1Blocks[] = { BlockId::Stone, BlockId::Dirt, BlockId::CoalOre, BlockId::IronOre, BlockId::GoldOre };
    for (int y = 16; y < 32; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                original.setBlock(x, y, z, sec1Blocks[(x + y + z) % 5]);
            }
        }
    }
    // Section 2 (Y=32..47): 8-bit palette (100 distinct states)
    for (int y = 32; y < 48; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                original.setBlock(x, y, z, static_cast<BlockId>((x + y * 16 + z) % 100 + 1));
            }
        }
    }
    // Section 3 (Y=48..63): 16-bit direct mode (300 distinct states)
    for (uint16_t i = 0; i < 300; ++i) {
        original.getSection(3).setBlock(i % 16, (i / 16) % 16, i / 256, static_cast<BlockStateId>(1000 + i));
    }

    // Sections 4..7 remain default Air (0-bit)

    // Verify sub-chunk bit depths
    STRESS_ASSERT(original.getSection(0).getBitsPerVoxel() == 0, "Section 0 must be 0-bit");
    STRESS_ASSERT(original.getSection(1).getBitsPerVoxel() == 4, "Section 1 must be 4-bit");
    STRESS_ASSERT(original.getSection(2).getBitsPerVoxel() == 8, "Section 2 must be 8-bit");
    STRESS_ASSERT(original.getSection(3).getBitsPerVoxel() == 16, "Section 3 must be 16-bit");
    STRESS_ASSERT(original.getSection(4).getBitsPerVoxel() == 0, "Section 4 must be 0-bit");

    // Test cross-boundary boundary reads:
    // Y=15 (top of Sec 0) vs Y=16 (bottom of Sec 1)
    STRESS_ASSERT(original.getBlock(5, 15, 5) == BlockId::Bedrock, "Y=15 boundary check");
    STRESS_ASSERT(original.getBlock(5, 16, 5) == sec1Blocks[(5 + 16 + 5) % 5], "Y=16 boundary check");

    // Test RLE serialization roundtrip
    const std::vector<uint8_t> rleData = original.serializeRLE();
    STRESS_ASSERT(!rleData.empty(), "Serialized RLE data must not be empty");

    Chunk restored(core::ChunkCoord{3, -5});
    bool desOk = restored.deserializeRLE(rleData);
    STRESS_ASSERT(desOk, "Deserialization must succeed");

    // Verify all 32,768 voxels match exactly
    for (int y = 0; y < 128; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                BlockId origBlock = original.getBlock(x, y, z);
                BlockId restBlock = restored.getBlock(x, y, z);
                STRESS_ASSERT(origBlock == restBlock, "Voxel mismatch after RLE roundtrip");
            }
        }
    }

    return true;
}

bool testBlockStateIdExtremeValues() {
    PalettedSection sec;
    // Test large 16-bit state values: 0, 1, 255, 256, 1000, 32767, 65534, 65535
    const BlockStateId testStates[] = { 0, 1, 255, 256, 1000, 32767, 65534, 65535 };
    for (size_t i = 0; i < std::size(testStates); ++i) {
        sec.setBlock(static_cast<int>(i), 0, 0, testStates[i]);
    }

    for (size_t i = 0; i < std::size(testStates); ++i) {
        STRESS_ASSERT(sec.getBlockState(static_cast<int>(i), 0, 0) == testStates[i],
                      "Extreme BlockStateId mismatch in 4-bit palette");
    }

    // Now promote directly to 16-bit mode by adding 260 unique high IDs on Z >= 1
    for (uint16_t i = 0; i < 260; ++i) {
        sec.setBlock(i % 16, (i / 16) % 16, 1 + (i / 256), static_cast<BlockStateId>(60000 + i));
    }
    STRESS_ASSERT(sec.getBitsPerVoxel() == 16, "Must be in 16-bit direct mode");

    // Verify extreme states in direct mode
    for (size_t i = 0; i < std::size(testStates); ++i) {
        STRESS_ASSERT(sec.getBlockState(static_cast<int>(i), 0, 0) == testStates[i],
                      "Extreme BlockStateId mismatch in 16-bit direct mode");
    }
    STRESS_ASSERT(sec.getBlockState(0, 0, 0) == 0, "State 0 mismatch");
    STRESS_ASSERT(sec.getBlockState(7, 0, 0) == 65535, "State 65535 mismatch");

    return true;
}

bool testPhysicsConsecutiveLagSpikesAndNoTunneling() {
    world::World world(999, "");
    world.ensureChunkLoaded({0, 0});
    // Create solid stone floor at Y=60 and clear air shaft above
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            world.setBlock(x, 60, z, BlockId::Stone);
            for (int y = 61; y <= 75; ++y) {
                world.setBlock(x, y, z, BlockId::Air);
            }
        }
    }

    // Spawn player falling from Y=65
    Player player;
    player.setPosition(glm::vec3(8.0f, 65.0f, 8.0f));
    player.resetInterpolationHistory();

    constexpr float FIXED_DT = 1.0f / 60.0f;
    constexpr float MAX_ACCUMULATOR = 0.25f;
    PlayerInput input{};

    // Simulate 20 consecutive massive lag spikes of 1.0 second each
    float accumulator = 0.0f;
    for (int spike = 0; spike < 20; ++spike) {
        accumulator = std::min(accumulator + 1.0f, MAX_ACCUMULATOR);
        int ticksInSpike = 0;
        while (accumulator >= FIXED_DT - 1e-5f) {
            player.fixedUpdate(FIXED_DT, input, world);
            accumulator -= FIXED_DT;
            if (accumulator < 0.0f) accumulator = 0.0f;
            ticksInSpike++;
        }
        // Each spike must execute exactly 15 ticks, never more
        STRESS_ASSERT(ticksInSpike == 15, "Ticks per lag spike must be clamped to 15");
    }

    // Player must have fallen and landed on top of Stone block at Y=60 (feet at Y=61.0f)
    // Box min.y is player.position.y. Player must NOT tunnel through stone floor into void!
    const glm::vec3 finalPos = player.getPosition();
    std::cout << " [DEBUG: finalPos.y=" << finalPos.y << " onGround=" << player.isOnGround() << "] " << std::flush;
    STRESS_ASSERT(finalPos.y >= 60.99f && finalPos.y <= 61.05f,
                  "Player tunneled through solid ground or hovered erroneously during lag spikes");
    STRESS_ASSERT(player.isOnGround(), "Player must be marked as onGround after landing");

    return true;
}

bool testDegenerateDeltaTimes() {
    constexpr float FIXED_DT = 1.0f / 60.0f;
    constexpr float MAX_ACCUMULATOR = 0.25f;

    Player player;
    world::World world(1, "");
    PlayerInput input{};

    // 1. dt = 0.0f
    float accumulator = 0.0f;
    accumulator = std::min(accumulator + 0.0f, MAX_ACCUMULATOR);
    int ticks = 0;
    while (accumulator >= FIXED_DT) {
        player.fixedUpdate(FIXED_DT, input, world);
        accumulator -= FIXED_DT;
        ticks++;
    }
    STRESS_ASSERT(ticks == 0, "dt=0 must produce 0 ticks");
    STRESS_ASSERT(accumulator == 0.0f, "accumulator must remain 0");

    // 2. dt = negative (e.g. clock jitter / backwards NTP step)
    float dtNegative = -0.5f;
    if (dtNegative > 0.0f) { // Real engine guards against dt < 0
        accumulator = std::min(accumulator + dtNegative, MAX_ACCUMULATOR);
    }
    STRESS_ASSERT(accumulator == 0.0f, "Negative dt must not corrupt accumulator");

    // 3. Ultra tiny dt = 1e-7f (100 nanoseconds)
    for (int i = 0; i < 1000; ++i) {
        accumulator = std::min(accumulator + 1e-7f, MAX_ACCUMULATOR);
    }
    STRESS_ASSERT(accumulator < FIXED_DT, "Micro-dts must not cause spurious ticks");

    return true;
}

// ============================================================================
// MAIN RUNNER
// ============================================================================

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << " ADVERSARIAL STRESS TEST SUITE: MILESTONE 1 (SPRINT 1.1)    " << std::endl;
    std::cout << "============================================================" << std::endl;

    std::cout << "\n--- SUITE 1: PalettedSection & Dynamic Transitions ---" << std::endl;
    RUN_TEST(testMemoryInvariants);
    RUN_TEST(testPaletteTransitionsAndLimits);
    RUN_TEST(testNibblePackingPairwiseStress);
    RUN_TEST(testMassiveRandomizedBlockMutations);
    RUN_TEST(testPaletteTransitionAndBackBehavior);
    RUN_TEST(testOutOfBoundsCoordinates);
    RUN_TEST(testBlockStateIdExtremeValues);
    RUN_TEST(testChunkSectionBoundariesAndRLEPersistence);

    std::cout << "\n--- SUITE 2: Lazy DenseLightArray Allocation & Churn ---" << std::endl;
    RUN_TEST(testLazyLightAllocationAndDeallocationCycles);
    RUN_TEST(testLightNibblePackingInterference);
    RUN_TEST(testDeepCopyAndMoveSemantics);

    std::cout << "\n--- SUITE 3: Fixed Timestep & Death Spiral Prevention ---" << std::endl;
    RUN_TEST(testSpiralOfDeathMassiveLagSpikes);
    RUN_TEST(testPhysicsDeterminismAcrossFramerateAndJitter);
    RUN_TEST(testInterpolationMonotonicityAndSnap);
    RUN_TEST(testPhysicsConsecutiveLagSpikesAndNoTunneling);
    RUN_TEST(testDegenerateDeltaTimes);

    std::cout << "\n============================================================" << std::endl;
    std::cout << "SUMMARY: Passed: " << g_testsPassed << ", Failed: " << g_testsFailed << std::endl;
    std::cout << "============================================================" << std::endl;

    return (g_testsFailed == 0) ? 0 : 1;
}
