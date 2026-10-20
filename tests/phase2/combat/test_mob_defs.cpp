// S4.1: the Beta 1.7.3 mob table (gameplay::mobDef): health, box, attack, daylight burning
// and death drops of the pig, the sheep and the zombie; S6.1 skeleton, spider, creeper, cow.
#include "gameplay/ItemIds.hpp"
#include "gameplay/MobDefs.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>

#include <cstddef>

using namespace betacraft;
using gameplay::MobCategory;
using gameplay::MobKind;
using gameplay::mobDef;
namespace items = gameplay::items;

namespace {

constexpr size_t kKinds = static_cast<size_t>(MobKind::Count);

} // namespace

TEST(MobDefs, EveryKindHasHealthAndABox) {
    for (size_t i = 0; i < kKinds; ++i) {
        const auto kind = static_cast<MobKind>(i);
        const gameplay::MobDef& def = mobDef(kind);
        SCOPED_TRACE(def.name);
        EXPECT_EQ(def.kind, kind);
        EXPECT_GT(def.maxHealth, 0);
        EXPECT_GT(def.width, 0.0f);
        EXPECT_GT(def.height, 0.0f);
        EXPECT_GT(def.moveSpeed, 0.0f);
        EXPECT_FALSE(def.name.empty());
    }
}

TEST(MobDefs, BetaValues) {
    const gameplay::MobDef& zombie = mobDef(MobKind::Zombie);
    EXPECT_EQ(zombie.maxHealth, 20);
    EXPECT_EQ(zombie.attackDamage, 5);
    EXPECT_EQ(zombie.category, MobCategory::Hostile);
    EXPECT_TRUE(zombie.burnsInDaylight);
    EXPECT_FLOAT_EQ(zombie.width, 0.6f);
    EXPECT_FLOAT_EQ(zombie.height, 1.8f);
    EXPECT_EQ(zombie.drops[0].item, items::Feather);

    const gameplay::MobDef& pig = mobDef(MobKind::Pig);
    EXPECT_EQ(pig.maxHealth, 10);
    EXPECT_EQ(pig.category, MobCategory::Passive);
    EXPECT_FALSE(pig.burnsInDaylight);
    EXPECT_FLOAT_EQ(pig.width, 0.9f);
    EXPECT_FLOAT_EQ(pig.height, 0.9f);
    EXPECT_EQ(pig.drops[0].item, items::RawPorkchop);
    EXPECT_EQ(pig.drops[0].min, 0);
    EXPECT_EQ(pig.drops[0].max, 2);

    const gameplay::MobDef& sheep = mobDef(MobKind::Sheep);
    EXPECT_EQ(sheep.maxHealth, 8);
    EXPECT_FLOAT_EQ(sheep.height, 1.3f);
    EXPECT_EQ(sheep.drops[0].item, gameplay::toItemId(voxel::BlockId::Wool));
    EXPECT_EQ(sheep.drops[0].min, 1);
    EXPECT_EQ(sheep.drops[0].max, 1);

    EXPECT_LT(zombie.moveSpeed, pig.moveSpeed); // Beta moveSpeed 0.5 against 0.7
    EXPECT_EQ(items::Feather, 288);
}

TEST(MobDefs, DroppedItemsStack) {
    for (size_t i = 0; i < kKinds; ++i) {
        for (const gameplay::MobDrop& drop : mobDef(static_cast<MobKind>(i)).drops) {
            if (drop.item == gameplay::kNoItem) continue;
            EXPECT_GT(gameplay::maxStackSize(drop.item), 0) << drop.item;
            EXPECT_GT(gameplay::maxStackSize(gameplay::mobDropItem(drop, true)), 0) << drop.item;
            EXPECT_LE(drop.min, drop.max);
        }
    }
}

TEST(MobDefs, BurningPigDropsCookedPorkchop) {
    const gameplay::MobDrop& pork = mobDef(MobKind::Pig).drops[0];
    EXPECT_EQ(gameplay::mobDropItem(pork, false), items::RawPorkchop);
    EXPECT_EQ(gameplay::mobDropItem(pork, true), items::CookedPorkchop);
    EXPECT_EQ(gameplay::mobDropItem(mobDef(MobKind::Zombie).drops[0], true), items::Feather);
}

TEST(MobDefs, RolledDropsStayWithinBounds) {
    gameplay::SplitMix64 rng(42);
    int32_t seen[3] = {0, 0, 0};
    for (int i = 0; i < 600; ++i) {
        const auto rolls = gameplay::rollMobDrops(MobKind::Pig, false, rng);
        int32_t total = 0;
        for (const gameplay::MobDropRoll& r : rolls) {
            EXPECT_EQ(r.item, items::RawPorkchop);
            EXPECT_GT(r.count, 0);
            total += r.count;
        }
        ASSERT_LE(total, 2);
        ++seen[total];
    }
    EXPECT_GT(seen[0], 0); // 0..2 inclusive, each reachable
    EXPECT_GT(seen[1], 0);
    EXPECT_GT(seen[2], 0);

    const auto wool = gameplay::rollMobDrops(MobKind::Sheep, false, rng);
    ASSERT_EQ(wool.size(), 1U);
    EXPECT_EQ(wool[0].count, 1);
}

// S6.1: the four Beta 1.7.3 mobs appended to MobKind.
TEST(MobDefs, SkeletonSpiderCreeperCowBetaValues) {
    EXPECT_EQ(static_cast<int>(MobKind::Skeleton), 3);
    EXPECT_EQ(static_cast<int>(MobKind::Cow), 6);
    EXPECT_EQ(kKinds, 7U);

    const gameplay::MobDef& skeleton = mobDef(MobKind::Skeleton);
    EXPECT_EQ(skeleton.maxHealth, 20);
    EXPECT_FLOAT_EQ(skeleton.width, 0.6f);
    EXPECT_FLOAT_EQ(skeleton.height, 1.8f);
    EXPECT_EQ(skeleton.category, MobCategory::Hostile);
    EXPECT_TRUE(skeleton.burnsInDaylight);
    EXPECT_EQ(skeleton.drops[0].item, items::Arrow);
    EXPECT_EQ(skeleton.drops[1].item, items::Bone);
    for (const gameplay::MobDrop& d : skeleton.drops) {
        EXPECT_EQ(d.min, 0);
        EXPECT_EQ(d.max, 2);
    }
    ASSERT_TRUE(skeleton.ranged.enabled());
    EXPECT_EQ(skeleton.ranged.intervalTicks, 30);
    EXPECT_FLOAT_EQ(skeleton.ranged.range, 10.0f);
    EXPECT_FLOAT_EQ(skeleton.ranged.speed, 12.0f);
    EXPECT_FALSE(skeleton.explode.enabled());

    const gameplay::MobDef& spider = mobDef(MobKind::Spider);
    EXPECT_EQ(spider.maxHealth, 16);
    EXPECT_FLOAT_EQ(spider.width, 1.4f);
    EXPECT_FLOAT_EQ(spider.height, 0.9f);
    EXPECT_EQ(spider.attackDamage, 2);
    EXPECT_FALSE(spider.burnsInDaylight);
    EXPECT_TRUE(spider.climbsWalls);
    EXPECT_TRUE(spider.neutralInDaylight);
    EXPECT_EQ(spider.drops[0].item, items::String);
    EXPECT_EQ(spider.drops[0].max, 2);
    EXPECT_GT(spider.moveSpeed, mobDef(MobKind::Zombie).moveSpeed);

    const gameplay::MobDef& creeper = mobDef(MobKind::Creeper);
    EXPECT_EQ(creeper.maxHealth, 20);
    EXPECT_FLOAT_EQ(creeper.height, 1.8f);
    EXPECT_EQ(creeper.attackDamage, 0);
    EXPECT_FALSE(creeper.burnsInDaylight);
    EXPECT_EQ(creeper.drops[0].item, items::Gunpowder);
    ASSERT_TRUE(creeper.explode.enabled());
    EXPECT_EQ(creeper.explode.fuseTicks, 30);
    EXPECT_FLOAT_EQ(creeper.explode.power, 3.0f);
    EXPECT_FLOAT_EQ(creeper.explode.igniteRange, 3.0f);
    EXPECT_FLOAT_EQ(creeper.explode.cancelRange, 7.0f);
    EXPECT_FALSE(creeper.ranged.enabled());

    const gameplay::MobDef& cow = mobDef(MobKind::Cow);
    EXPECT_EQ(cow.maxHealth, 10);
    EXPECT_FLOAT_EQ(cow.width, 0.9f);
    EXPECT_FLOAT_EQ(cow.height, 1.3f);
    EXPECT_EQ(cow.category, MobCategory::Passive);
    EXPECT_EQ(cow.drops[0].item, items::Leather);
    EXPECT_EQ(cow.drops[0].max, 2);

    EXPECT_EQ(items::Arrow, 262);
    EXPECT_EQ(items::Bone, 352);
    EXPECT_EQ(items::String, 287);
    EXPECT_EQ(items::Gunpowder, 289);
    EXPECT_EQ(items::Leather, 334);
}

TEST(MobDefs, SpawnWeights) {
    for (const MobKind hostile : {MobKind::Zombie, MobKind::Skeleton, MobKind::Spider, MobKind::Creeper}) {
        EXPECT_EQ(mobDef(hostile).spawnWeight, 10) << mobDef(hostile).name;
    }
    EXPECT_EQ(mobDef(MobKind::Sheep).spawnWeight, 12);
    EXPECT_EQ(mobDef(MobKind::Pig).spawnWeight, 10);
    EXPECT_EQ(mobDef(MobKind::Cow).spawnWeight, 8);
    // The other kinds have none of the new abilities.
    for (const MobKind k : {MobKind::Pig, MobKind::Sheep, MobKind::Zombie, MobKind::Cow}) {
        EXPECT_FALSE(mobDef(k).climbsWalls);
        EXPECT_FALSE(mobDef(k).ranged.enabled());
        EXPECT_FALSE(mobDef(k).explode.enabled());
    }
}

TEST(MobDefs, PickSpawnKindFollowsTheWeights) {
    gameplay::SplitMix64 rng(1234);
    int32_t counts[kKinds] = {};
    constexpr int kTrials = 30'000;
    for (int i = 0; i < kTrials; ++i) {
        const MobKind hostile = gameplay::pickSpawnKind(MobCategory::Hostile, rng);
        ASSERT_EQ(mobDef(hostile).category, MobCategory::Hostile);
        ++counts[static_cast<size_t>(hostile)];
        const MobKind passive = gameplay::pickSpawnKind(MobCategory::Passive, rng);
        ASSERT_EQ(mobDef(passive).category, MobCategory::Passive);
        ++counts[static_cast<size_t>(passive)];
    }
    const auto share = [&](MobKind k) { return static_cast<double>(counts[static_cast<size_t>(k)]) / kTrials; };
    for (const MobKind hostile : {MobKind::Zombie, MobKind::Skeleton, MobKind::Spider, MobKind::Creeper}) {
        EXPECT_NEAR(share(hostile), 0.25, 0.02) << mobDef(hostile).name;
    }
    EXPECT_NEAR(share(MobKind::Sheep), 12.0 / 30.0, 0.02);
    EXPECT_NEAR(share(MobKind::Pig), 10.0 / 30.0, 0.02);
    EXPECT_NEAR(share(MobKind::Cow), 8.0 / 30.0, 0.02);
}

TEST(MobDefs, OutOfRangeKindFallsBackSafely) {
    EXPECT_GT(mobDef(MobKind::Count).maxHealth, 0);
}
