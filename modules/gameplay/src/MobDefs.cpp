#include "gameplay/MobDefs.hpp"

#include <cstddef>

namespace betacraft::gameplay {

namespace {

// Beta 1.7.3: EntityPig (10 HP, 0.9 x 0.9), EntitySheep (8 HP, 0.9 x 1.3, one wool block
// unless sheared), EntityZombie (20 HP, 0.6 x 1.8, attackStrength 5, moveSpeed 0.5, feathers).
// S6.1: EntitySkeleton (20 HP, 0.6 x 1.8, burns, arrows and bones, shoots every 30 ticks
// within 10 blocks), EntitySpider (16 HP, 1.4 x 0.9, moveSpeed 0.8, 2 damage, string),
// EntityCreeper (20 HP, 0.6 x 1.8, gunpowder, 30-tick fuse, power 3), EntityCow (10 HP,
// 0.9 x 1.3, leather). Every drop is rand(3), i.e. 0..2.
constexpr RangedAttack kSkeletonBow{.intervalTicks = 30, .range = 10.0f, .speed = 12.0f, .elevation = 0.2f,
                                    .spread = 12.0f, .damage = 4};
constexpr ExplodeAttack kCreeperFuse{.fuseTicks = 30, .power = 3.0f, .igniteRange = 3.0f, .cancelRange = 7.0f};

constexpr std::array<MobDef, static_cast<size_t>(MobKind::Count)> kMobDefs{{
    {MobKind::Pig, MobCategory::Passive, 10, 0.9f, 0.9f, 1.68f, 0, false,
     {{{items::RawPorkchop, 0, 2}, {}}}, "Pig", 10, false, false, {}, {}, {0.9f, 0.9f, 0.9f}},
    {MobKind::Sheep, MobCategory::Passive, 8, 0.9f, 1.3f, 1.68f, 0, false,
     {{{toItemId(voxel::BlockId::Wool), 1, 1}, {}}}, "Sheep", 12, false, false, {}, {}, {0.9f, 1.3f, 0.9f}},
    {MobKind::Zombie, MobCategory::Hostile, 20, 0.6f, 1.8f, 1.2f, 5, true,
     {{{items::Feather, 0, 2}, {}}}, "Zombie", 10, false, false, {}, {}, {0.6f, 1.8f, 0.6f}},
    {MobKind::Skeleton, MobCategory::Hostile, 20, 0.6f, 1.8f, 1.68f, 0, true,
     {{{items::Arrow, 0, 2}, {items::Bone, 0, 2}}}, "Skeleton", 10, false, false, kSkeletonBow, {},
     {0.6f, 1.8f, 0.6f}},
    {MobKind::Spider, MobCategory::Hostile, 16, 1.4f, 0.9f, 1.92f, 2, false,
     {{{items::String, 0, 2}, {}}}, "Spider", 10, true, true, {}, {}, {1.4f, 0.9f, 1.4f}},
    {MobKind::Creeper, MobCategory::Hostile, 20, 0.6f, 1.8f, 1.68f, 0, false,
     {{{items::Gunpowder, 0, 2}, {}}}, "Creeper", 10, false, false, {}, kCreeperFuse, {0.6f, 1.8f, 0.6f}},
    {MobKind::Cow, MobCategory::Passive, 10, 0.9f, 1.3f, 1.68f, 0, false,
     {{{items::Leather, 0, 2}, {}}}, "Cow", 8, false, false, {}, {}, {0.9f, 1.3f, 0.9f}},
}};

} // namespace

const MobDef& mobDef(MobKind kind) noexcept {
    const auto i = static_cast<size_t>(kind);
    return i < kMobDefs.size() ? kMobDefs[i] : kMobDefs[0];
}

MobKind pickSpawnKind(MobCategory category, SplitMix64& rng) noexcept {
    int32_t total = 0;
    for (const MobDef& def : kMobDefs) {
        total += def.category == category ? def.spawnWeight : 0;
    }
    int32_t roll = rng.nextInt(total);
    for (const MobDef& def : kMobDefs) {
        if (def.category != category || def.spawnWeight <= 0) {
            continue;
        }
        if (roll < def.spawnWeight) {
            return def.kind;
        }
        roll -= def.spawnWeight;
    }
    return category == MobCategory::Hostile ? MobKind::Zombie : MobKind::Pig;
}

ItemId mobDropItem(const MobDrop& drop, bool burning) noexcept {
    return burning && drop.item == items::RawPorkchop ? items::CookedPorkchop : drop.item;
}

std::vector<MobDropRoll> rollMobDrops(MobKind kind, bool burning, SplitMix64& rng) {
    std::vector<MobDropRoll> out;
    for (const MobDrop& drop : mobDef(kind).drops) {
        if (drop.item == kNoItem || drop.max <= 0) {
            continue;
        }
        const int32_t count = drop.min + rng.nextInt(drop.max - drop.min + 1);
        if (count > 0) {
            out.push_back({mobDropItem(drop, burning), count});
        }
    }
    return out;
}

} // namespace betacraft::gameplay
