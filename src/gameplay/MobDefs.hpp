#pragma once

#include "gameplay/ItemIds.hpp"
#include "gameplay/SplitMix64.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace betacraft::gameplay {

// Mob kinds. Grows only at the end, before Count (S6.1 appended Skeleton, Spider, Creeper, Cow).
enum class MobKind : uint8_t { Pig, Sheep, Zombie, Skeleton, Spider, Creeper, Cow, Count };

enum class MobCategory : uint8_t { Passive, Hostile };

// `min`..`max` items of `item` on death (Beta EntityLiving.dropFewItems: rand(3) for 0..2).
// An unused slot has item == kNoItem.
struct MobDrop {
    ItemId item{kNoItem};
    int32_t min{0};
    int32_t max{0};
};

// S6.1 ranged attack (Beta EntitySkeleton.attackEntity): within `range` blocks and in line of
// sight the mob stops and shoots an arrow every `intervalTicks` at `speed` blocks per second,
// aimed `elevation` x distance higher, with Beta's `spread` (EntityArrow.setArrowHeading).
// intervalTicks == 0: no ranged attack.
struct RangedAttack {
    int32_t intervalTicks{0};
    float range{0.0f};
    float speed{0.0f};
    float elevation{0.0f};
    float spread{0.0f};
    int32_t damage{0}; // Arrow damage on hit (Beta EntityArrow: 4)

    [[nodiscard]] constexpr bool enabled() const noexcept { return intervalTicks > 0; }
};

// S6.1 suicide attack (Beta EntityCreeper): the fuse counts up while the target is closer than
// `igniteRange`, down again beyond `cancelRange`, and at `fuseTicks` the mob explodes with
// `power` (EntitySimulation::explode) and disappears without drops. fuseTicks == 0: none.
struct ExplodeAttack {
    int32_t fuseTicks{0};
    float power{0.0f};
    float igniteRange{0.0f};
    float cancelRange{0.0f};

    [[nodiscard]] constexpr bool enabled() const noexcept { return fuseTicks > 0; }
};

// Static Beta 1.7.3 data of one mob kind. `moveSpeed` is in blocks per second: Beta's
// EntityLiving.moveSpeed (0.7 by default, 0.5 for zombies) gives a steady ground speed of
// moveSpeed * 0.1 * 0.546 / (1 - 0.546) blocks per tick on ordinary blocks, i.e. about
// 2.4 * moveSpeed blocks per second.
struct MobDef {
    MobKind kind{MobKind::Pig};
    MobCategory category{MobCategory::Passive};
    int16_t maxHealth{0};
    float width{0.0f};
    float height{0.0f};
    float moveSpeed{0.0f};
    int32_t attackDamage{0};
    bool burnsInDaylight{false};
    std::array<MobDrop, 2> drops{};
    std::string_view name{};
    // S6.1. Natural spawning picks a kind of the category with these weights (Beta
    // BiomeGenBase spawn lists: 10 per hostile; sheep 12, pig 10, cow 8).
    int32_t spawnWeight{0};
    bool climbsWalls{false};       // Spider: climbs when blocked horizontally (S6.3)
    bool neutralInDaylight{false}; // Spider: loses its target in bright light (S6.3)
    RangedAttack ranged{};
    ExplodeAttack explode{};
    // S8.4 the box A* plans for (ai::NavAgent): width, height, depth in blocks. Beta's
    // PathFinder rounds the entity size up, so a zombie needs 1x2x1 cells, a spider 2x1x2.
    glm::vec3 navExtents{0.6f, 1.8f, 0.6f};
    bool canOpenDoors{false}; // No Beta 1.7.3 mob opens doors (zombies breaking them is out of scope)
};

// The definition of `kind`; kinds at or past Count fall back to the pig.
[[nodiscard]] const MobDef& mobDef(MobKind kind) noexcept;

// S6.1: a kind of `category` drawn with the spawnWeight of each kind (Beta
// WeightedRandom.getRandomItem over the biome spawn list).
[[nodiscard]] MobKind pickSpawnKind(MobCategory category, SplitMix64& rng) noexcept;

// The item a drop slot yields: a pig that dies burning drops cooked instead of raw porkchop
// (Beta EntityPig.getDropItemId).
[[nodiscard]] ItemId mobDropItem(const MobDrop& drop, bool burning) noexcept;

struct MobDropRoll {
    ItemId item{kNoItem};
    int32_t count{0};
};

// Rolls the death drops of `kind`: one entry per drop slot whose count, uniform in
// [min, max], is above zero.
[[nodiscard]] std::vector<MobDropRoll> rollMobDrops(MobKind kind, bool burning, SplitMix64& rng);

} // namespace betacraft::gameplay
