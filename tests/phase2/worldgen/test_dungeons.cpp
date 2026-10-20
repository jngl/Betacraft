// S6.10: Beta 1.7.3 dungeons (WorldGenDungeons) placed in pass 1 inside one chunk: bounds,
// ceiling under the surface, 1..5 openings, the spawner and the loot chests, determinism, and a
// decoration plan left unchanged.
#include "WorldgenTestSupport.hpp"

#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "worldgen/CaveCarver.hpp"
#include "worldgen/DecorationFeatures.hpp"
#include "worldgen/Dungeons.hpp"
#include "worldgen/OreVeins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <set>
#include <vector>

using namespace betacraft;
using namespace betacraft::worldgen;
using voxel::BlockId;
using voxel::Chunk;

namespace {

constexpr uint64_t SEED = 0xD06E0ULL;

// Pass 1 up to the veins over the synthetic terrain (caves give the rooms their openings).
[[nodiscard]] std::unique_ptr<Chunk> passOneUpToVeins(core::ChunkCoord coord) {
    auto chunk = std::make_unique<Chunk>(coord);
    const test::SyntheticTerrain terrain;
    terrain.fillChunk(*chunk);
    const CaveCarver carver(SEED);
    (void)carver.carveChunk(*chunk, terrain);
    placeOreVeins(*chunk, SEED);
    return chunk;
}

struct Sample {
    std::unique_ptr<Chunk> before; // Up to the veins
    std::unique_ptr<Chunk> after;  // Dungeons placed
    std::vector<DungeonInfo> dungeons;
};

// The chunks of a fixed area that received at least one dungeon.
const std::vector<Sample>& samples() {
    static const std::vector<Sample> cache = [] {
        std::vector<Sample> out;
        const test::SyntheticTerrain terrain;
        for (int32_t cz = -12; cz < 12; ++cz) {
            for (int32_t cx = -12; cx < 12; ++cx) {
                Sample s;
                s.before = passOneUpToVeins({cx, cz});
                s.after = std::make_unique<Chunk>(*s.before);
                s.dungeons = placeDungeons(*s.after, SEED, terrain);
                if (!s.dungeons.empty()) out.push_back(std::move(s));
            }
        }
        return out;
    }();
    return cache;
}

[[nodiscard]] core::BlockPos local(const Chunk& chunk, const core::BlockPos& world) {
    return {world.x - chunk.getCoord().x * Chunk::WIDTH, world.y, world.z - chunk.getCoord().z * Chunk::DEPTH};
}

} // namespace

TEST(Dungeons, TheFixtureHasDungeons) {
    voxel::BlockRegistry::initialize();
    size_t rooms = 0;
    for (const Sample& s : samples()) rooms += s.dungeons.size();
    EXPECT_GE(rooms, 3U);
}

TEST(Dungeons, RoomsStayInsideTheirChunk) {
    for (const Sample& s : samples()) {
        for (const DungeonInfo& d : s.dungeons) {
            const core::BlockPos c = local(*s.after, d.spawner);
            EXPECT_GE(c.x, kDungeonMinOrigin);
            EXPECT_LE(c.x, kDungeonMaxOrigin);
            EXPECT_GE(c.z, kDungeonMinOrigin);
            EXPECT_LE(c.z, kDungeonMaxOrigin);
            EXPECT_TRUE(d.radiusX == 2 || d.radiusX == 3);
            EXPECT_TRUE(d.radiusZ == 2 || d.radiusZ == 3);
            EXPECT_GE(c.x - d.radiusX - 1, 0);
            EXPECT_LT(c.x + d.radiusX + 1, Chunk::WIDTH);
            EXPECT_GE(c.z - d.radiusZ - 1, 0);
            EXPECT_LT(c.z + d.radiusZ + 1, Chunk::DEPTH);
        }
        // Nothing outside the dungeons' boxes changed.
        for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
            for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                for (int32_t x = 0; x < Chunk::WIDTH; ++x) {
                    if (s.before->getBlock(x, y, z) == s.after->getBlock(x, y, z)) continue;
                    const bool inABox = std::any_of(s.dungeons.begin(), s.dungeons.end(), [&](const DungeonInfo& d) {
                        const core::BlockPos c = local(*s.after, d.spawner);
                        return std::abs(x - c.x) <= d.radiusX + 1 && std::abs(z - c.z) <= d.radiusZ + 1 &&
                               y >= c.y - 1 && y <= c.y + kDungeonInnerHeight;
                    });
                    ASSERT_TRUE(inABox) << x << "," << y << "," << z;
                }
    }
}

TEST(Dungeons, CeilingStaysThreeBlocksUnderTheSurface) {
    const test::SyntheticTerrain terrain;
    for (const Sample& s : samples()) {
        for (const DungeonInfo& d : s.dungeons) {
            const int32_t ceiling = d.spawner.y + kDungeonInnerHeight + 1;
            for (int32_t x = d.spawner.x - d.radiusX - 1; x <= d.spawner.x + d.radiusX + 1; ++x)
                for (int32_t z = d.spawner.z - d.radiusZ - 1; z <= d.spawner.z + d.radiusZ + 1; ++z)
                    EXPECT_LE(ceiling, terrain.sampleColumn(x, z).surfaceHeight - kDungeonSurfaceClearance);
        }
    }
}

TEST(Dungeons, OneToFiveOpeningsOnTheTerrainBefore) {
    for (const Sample& s : samples()) {
        for (const DungeonInfo& d : s.dungeons) {
            EXPECT_GE(d.openings, kDungeonMinOpenings);
            EXPECT_LE(d.openings, kDungeonMaxOpenings);
        }
        if (s.dungeons.size() != 1) continue; // A second room would see the first one
        const DungeonInfo& d = s.dungeons.front();
        const core::BlockPos c = local(*s.before, d.spawner);
        int32_t openings = 0;
        for (int32_t x = c.x - d.radiusX - 1; x <= c.x + d.radiusX + 1; ++x)
            for (int32_t z = c.z - d.radiusZ - 1; z <= c.z + d.radiusZ + 1; ++z) {
                const bool wall = std::abs(x - c.x) == d.radiusX + 1 || std::abs(z - c.z) == d.radiusZ + 1;
                if (wall && s.before->getBlock(x, c.y, z) == BlockId::Air && s.before->getBlock(x, c.y + 1, z) == BlockId::Air)
                    ++openings;
            }
        EXPECT_EQ(openings, d.openings);
    }
}

TEST(Dungeons, SpawnerChestsWallsAndFloor) {
    size_t chests = 0;
    for (const Sample& s : samples()) {
        for (const DungeonInfo& d : s.dungeons) {
            chests += d.chests.size();
            EXPECT_LE(d.chests.size(), static_cast<size_t>(kDungeonChests));
            if (s.dungeons.size() != 1) continue; // A later room may cut into an earlier one
            const core::BlockPos c = local(*s.after, d.spawner);
            EXPECT_EQ(s.after->getBlock(c.x, c.y, c.z), BlockId::MobSpawner);
            for (const core::BlockPos& chest : d.chests) {
                const core::BlockPos l = local(*s.after, chest);
                EXPECT_EQ(s.after->getBlock(l.x, l.y, l.z), BlockId::Chest);
                EXPECT_EQ(s.after->getMetadata(l.x, l.y, l.z), kChestLootPendingMeta);
                EXPECT_EQ(chest.y, d.spawner.y);
            }
            // Floor: cobblestone or mossy cobblestone (or air over a hole); inner air above.
            for (int32_t x = c.x - d.radiusX; x <= c.x + d.radiusX; ++x)
                for (int32_t z = c.z - d.radiusZ; z <= c.z + d.radiusZ; ++z) {
                    const BlockId floor = s.after->getBlock(x, c.y - 1, z);
                    EXPECT_TRUE(floor == BlockId::Cobblestone || floor == BlockId::MossyCobble || floor == BlockId::Air)
                        << static_cast<int>(floor);
                    for (int32_t y = c.y + 1; y < c.y + kDungeonInnerHeight; ++y)
                        EXPECT_EQ(s.after->getBlock(x, y, z), BlockId::Air);
                }
        }
    }
    EXPECT_GT(chests, 0U);
}

TEST(Dungeons, DeterministicPerChunk) {
    const test::SyntheticTerrain terrain;
    for (const Sample& s : samples()) {
        Chunk again(*s.before);
        const std::vector<DungeonInfo> rooms = placeDungeons(again, SEED, terrain);
        ASSERT_EQ(rooms.size(), s.dungeons.size());
        for (size_t i = 0; i < rooms.size(); ++i) EXPECT_EQ(rooms[i].spawner, s.dungeons[i].spawner);
        for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
            for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                for (int32_t x = 0; x < Chunk::WIDTH; ++x) ASSERT_EQ(again.getBlock(x, y, z), s.after->getBlock(x, y, z));
    }
}

TEST(Dungeons, DecorationPlanIsUnchanged) {
    const FeaturePlanner planner(SEED);
    for (const Sample& s : samples()) {
        EXPECT_EQ(planner.plan(*s.before), planner.plan(*s.after))
            << "chunk " << s.after->getCoord().x << "," << s.after->getCoord().z;
    }
}

TEST(DungeonLoot, DeterministicKnownItemsOnly) {
    const std::set<gameplay::ItemId> allowed{gameplay::items::IronIngot, gameplay::items::Bread, gameplay::items::Wheat,
                                             gameplay::items::Gunpowder, gameplay::items::String, gameplay::items::Bucket,
                                             gameplay::items::GoldenApple, gameplay::items::Redstone};
    size_t filled = 0;
    bool differs = false;
    const auto first = rollDungeonChestLoot(SEED, {10, 30, -4});
    EXPECT_EQ(first, rollDungeonChestLoot(SEED, {10, 30, -4}));
    for (int32_t i = 0; i < 200; ++i) {
        const core::BlockPos pos{i * 7 - 300, 20 + i % 30, 11 - i * 3};
        const auto slots = rollDungeonChestLoot(SEED, pos);
        differs |= slots != first;
        size_t nonEmpty = 0;
        for (const voxel::StoredStack& s : slots) {
            if (s.empty()) continue;
            ++nonEmpty;
            EXPECT_TRUE(allowed.contains(s.id)) << s.id;
            EXPECT_GE(s.count, 1);
            EXPECT_LE(s.count, 4);
            EXPECT_EQ(s.damage, 0);
        }
        EXPECT_LE(nonEmpty, static_cast<size_t>(kDungeonChestDraws));
        filled += nonEmpty;
    }
    EXPECT_TRUE(differs);
    // Beta: 6 to 7 of the 11 outcomes give an item; 8 draws (with slot collisions) per chest.
    EXPECT_GT(filled, 200U * 3U);
    EXPECT_LT(filled, 200U * 6U);
    EXPECT_NE(rollDungeonChestLoot(SEED + 1, {10, 30, -4}), first);
}
