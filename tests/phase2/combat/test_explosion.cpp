// S6.5: the pure Beta 1.7.3 explosion (gameplay::explosionBlocks, exposure, explosionDamage)
// over a small voxel lambda, without any World.
#include "gameplay/Explosion.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace betacraft;
using gameplay::ExplosionInput;
using world::BlockId;
using world::BlockRegistry;

namespace {

using Grid = std::function<BlockId(int32_t, int32_t, int32_t)>;

std::vector<core::BlockPos> blast(const Grid& grid, const glm::vec3& centre, float power, uint64_t seed = 7) {
    return gameplay::explosionBlocks(ExplosionInput{centre, power, seed}, [&](int32_t x, int32_t y, int32_t z) {
        const BlockId id = grid(x, y, z);
        return id == BlockId::Air ? -1.0f : BlockRegistry::get(id).blastResistance;
    });
}

// A block of `fill` everywhere but the centre cell (0, 0, 0), where the TNT stood.
Grid solidAround(BlockId fill) {
    return [fill](int32_t x, int32_t y, int32_t z) { return (x == 0 && y == 0 && z == 0) ? BlockId::Air : fill; };
}

float maxDistance(const std::vector<core::BlockPos>& cells, const glm::vec3& centre) {
    float best = 0.0f;
    for (const core::BlockPos& p : cells) {
        const glm::vec3 c(static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + 0.5f, static_cast<float>(p.z) + 0.5f);
        best = std::max(best, glm::length(c - centre));
    }
    return best;
}

const glm::vec3 kCentre{0.5f, 0.5f, 0.5f};

} // namespace

TEST(Explosion, RayCountIsTheShellOfTheGrid) {
    EXPECT_EQ(gameplay::detail::explosionRayDirections().size(), 16U * 16U * 16U - 14U * 14U * 14U);
    for (const glm::vec3& d : gameplay::detail::explosionRayDirections()) {
        EXPECT_NEAR(glm::length(d), 1.0f, 1e-5f);
    }
}

TEST(Explosion, BlastResistanceValues) {
    EXPECT_FLOAT_EQ(BlockRegistry::get(BlockId::Stone).blastResistance, 6.0f);
    EXPECT_FLOAT_EQ(BlockRegistry::get(BlockId::Cobblestone).blastResistance, 6.0f);
    EXPECT_FLOAT_EQ(BlockRegistry::get(BlockId::Bricks).blastResistance, 6.0f);
    EXPECT_FLOAT_EQ(BlockRegistry::get(BlockId::MossyCobble).blastResistance, 6.0f);
    EXPECT_FLOAT_EQ(BlockRegistry::get(BlockId::OakPlanks).blastResistance, 3.0f);
    EXPECT_FLOAT_EQ(BlockRegistry::get(BlockId::Obsidian).blastResistance, 1200.0f);
    EXPECT_FLOAT_EQ(BlockRegistry::get(BlockId::Bedrock).blastResistance, 3.6e6f);
    EXPECT_FLOAT_EQ(BlockRegistry::get(BlockId::Water).blastResistance, 100.0f);
    EXPECT_FLOAT_EQ(BlockRegistry::get(BlockId::StillLava).blastResistance, 100.0f);
    // Everything else defaults to its hardness.
    EXPECT_FLOAT_EQ(BlockRegistry::get(BlockId::Dirt).blastResistance, BlockRegistry::get(BlockId::Dirt).hardness);
    EXPECT_FLOAT_EQ(BlockRegistry::get(BlockId::TNT).blastResistance, 0.0f);
}

TEST(Explosion, OpenAirTouchesNoBlock) {
    const Grid air = [](int32_t, int32_t, int32_t) { return BlockId::Air; };
    EXPECT_TRUE(blast(air, kCentre, 4.0f).empty());
}

TEST(Explosion, PowerFourInStoneStaysWithinFourBlocks) {
    // Beta: (6 + 0.3) x 0.3 + 0.225 = 2.115 lost per 0.3-block step in stone, so TNT
    // (intensity 2.8..5.2) only bites into the cells next to it.
    const std::vector<core::BlockPos> stone = blast(solidAround(BlockId::Stone), kCentre, 4.0f);
    EXPECT_FALSE(stone.empty());
    EXPECT_LE(maxDistance(stone, kCentre), 4.0f);
    // Softer ground opens the 3 to 4 block crater of a Beta TNT.
    const std::vector<core::BlockPos> dirt = blast(solidAround(BlockId::Dirt), kCentre, 4.0f);
    EXPECT_GE(maxDistance(dirt, kCentre), 3.0f);
    EXPECT_LE(maxDistance(dirt, kCentre), 4.0f + 0.87f); // A cell centre within 4 blocks of reach
    EXPECT_GT(dirt.size(), stone.size());
}

TEST(Explosion, ObsidianAndBedrockAreNeverDestroyed) {
    // Dirt with a ring of obsidian and a bedrock floor: the dirt goes, they stay.
    const Grid grid = [](int32_t x, int32_t y, int32_t z) {
        if (x == 0 && y == 0 && z == 0) return BlockId::Air;
        if (y <= -2) return BlockId::Bedrock;
        if (std::abs(x) == 1 || std::abs(z) == 1) return BlockId::Obsidian;
        return BlockId::Dirt;
    };
    for (const float power : {4.0f, 8.0f}) {
        const std::vector<core::BlockPos> cells = blast(grid, kCentre, power);
        EXPECT_FALSE(cells.empty());
        for (const core::BlockPos& p : cells) {
            const BlockId id = grid(p.x, p.y, p.z);
            EXPECT_NE(id, BlockId::Obsidian) << p.x << ' ' << p.y << ' ' << p.z;
            EXPECT_NE(id, BlockId::Bedrock) << p.x << ' ' << p.y << ' ' << p.z;
        }
    }
    EXPECT_TRUE(blast(solidAround(BlockId::Obsidian), kCentre, 4.0f).empty());
}

TEST(Explosion, UnderWaterBreaksNothing) {
    // TNT in a lake: water all around and in the centre cell, a sand bed two cells down.
    const Grid lake = [](int32_t, int32_t y, int32_t) { return y <= -2 ? BlockId::Sand : BlockId::Water; };
    EXPECT_TRUE(blast(lake, kCentre, 4.0f).empty());
}

TEST(Explosion, SameInputSameCrater) {
    const Grid grid = solidAround(BlockId::Dirt);
    const auto a = blast(grid, kCentre, 4.0f, 99);
    const auto b = blast(grid, kCentre, 4.0f, 99);
    EXPECT_EQ(a, b);
    EXPECT_TRUE(std::is_sorted(a.begin(), a.end(), [](const core::BlockPos& l, const core::BlockPos& r) {
        return std::tie(l.x, l.y, l.z) < std::tie(r.x, r.y, r.z);
    }));
    EXPECT_NE(a, blast(grid, kCentre, 4.0f, 100)) << "the seed drives the ray intensities";
}

TEST(Explosion, DamageIsHighestAtTheCentreAndZeroAtTwicePower) {
    constexpr float power = 4.0f;
    const int32_t centre = gameplay::explosionDamage(0.0f, power, 1.0f);
    EXPECT_EQ(centre, 65); // (1 + 1) / 2 x 8 x 8 + 1
    int32_t previous = centre;
    for (float d = 0.5f; d < 2.0f * power; d += 0.5f) {
        const int32_t dmg = gameplay::explosionDamage(d, power, 1.0f);
        EXPECT_LE(dmg, previous) << d;
        EXPECT_GE(dmg, 1) << d;
        previous = dmg;
    }
    EXPECT_EQ(gameplay::explosionDamage(2.0f * power, power, 1.0f), 0);
    EXPECT_EQ(gameplay::explosionDamage(20.0f, power, 1.0f), 0);
    // Hidden entities still take Beta's flat 1.
    EXPECT_EQ(gameplay::explosionDamage(1.0f, power, 0.0f), 1);
    // Knockback points away from the centre and fades out.
    const glm::vec3 kb = gameplay::explosionKnockback(glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f), power, 1.0f);
    EXPECT_GT(kb.x, 0.0f);
    EXPECT_NEAR(kb.x, 0.75f, 1e-5f);
    EXPECT_EQ(gameplay::explosionKnockback(glm::vec3(0.0f), glm::vec3(9.0f, 0.0f, 0.0f), power, 1.0f), glm::vec3(0.0f));
}

TEST(Explosion, ExposureIsZeroBehindAWallAndOneInTheOpen) {
    const math::AABB mob{{4.7f, 0.0f, 0.2f}, {5.3f, 1.8f, 0.8f}};
    const glm::vec3 centre{0.5f, 0.5f, 0.5f};
    const auto open = [](int32_t, int32_t, int32_t) { return false; };
    EXPECT_FLOAT_EQ(gameplay::exposure(centre, mob, open), 1.0f);
    // A wall at x = 2, from y = -5 to 10 and z = -5 to 5.
    const auto wall = [](int32_t x, int32_t y, int32_t z) {
        return x == 2 && y >= -5 && y <= 10 && z >= -5 && z <= 5;
    };
    EXPECT_FLOAT_EQ(gameplay::exposure(centre, mob, wall), 0.0f);
    // A low wall hides the legs only from a blast at head height.
    const auto low = [](int32_t x, int32_t y, int32_t z) { return x == 2 && y <= 0 && std::abs(z) <= 5; };
    const float partial = gameplay::exposure({0.5f, 1.5f, 0.5f}, mob, low);
    EXPECT_GT(partial, 0.0f);
    EXPECT_LT(partial, 1.0f);
}
