#pragma once

#include "core/Types.hpp"
#include "gameplay/ItemStack.hpp"
#include "voxel/Block.hpp"

#include <cstdint>
#include <limits>
#include <optional>

namespace betacraft::gameplay {

inline constexpr int32_t kTicksPerSecond = 20;
inline constexpr ItemId kGlowstoneDustItem = items::GlowstoneDust; // Vanilla Beta item id 348
inline constexpr int32_t kUnbreakableTicks = std::numeric_limits<int32_t>::max();

// Beta Block.blockStrength divisors: progress per tick is speed / hardness / 30 when the
// held item can harvest the block, 1 / hardness / 100 otherwise (the "x1.5 s vs x5 s" rule).
inline constexpr double kHarvestableDivisor = 30.0;
inline constexpr double kUnharvestableDivisor = 100.0;
inline constexpr float kSwordDigSpeed = 1.5f;
// Beta PlayerControllerSP.blockHitDelay: ticks skipped after a held break before the
// next block starts taking damage (stops hold-to-mine from razing a column instantly).
inline constexpr int32_t kBlockHitDelayTicks = 5;
inline constexpr float kEnvironmentPenalty = 5.0f; // Under water / not on ground (harvestable only)

// Mining-relevant properties of a world block. Hardness is read from voxel::BlockRegistry;
// tool requirements are this module's own table because BlockDef has no such data.
struct BlockHarvestInfo {
    float hardness{-1.0f};                  // < 0: unbreakable (bedrock, liquids, non-blocks)
    ToolType effectiveTool{ToolType::None}; // Tool whose material efficiency applies
    ToolType requiredTool{ToolType::None};  // None: drops when mined by hand
    int32_t minHarvestLevel{0};             // Only meaningful when requiredTool != None

    [[nodiscard]] constexpr bool breakable() const noexcept { return hardness >= 0.0f; }
};

struct DigConditions {
    bool inWater{false}; // Player's head inside water
    bool onGround{true};
};

struct DigResult {
    bool breakable{false};
    bool dropsItems{false};      // False: the block is destroyed but yields nothing
    bool instant{false};         // Broken on the initial click (strength >= 1)
    // Fraction of the block broken per game tick (Beta blockStrength). Double so that summing
    // it over thousands of ticks (obsidian by hand) cannot drift by a tick.
    double progressPerTick{0.0};
    int32_t ticks{kUnbreakableTicks}; // Hold-to-break duration (0 when instant)
    float seconds{0.0f};
};

[[nodiscard]] BlockHarvestInfo harvestInfo(voxel::BlockId block) noexcept;

// Beta EntityPlayer.canHarvestBlock: toolless blocks always drop; otherwise the held item
// must be the required tool type with a sufficient harvest level.
[[nodiscard]] bool canHarvest(const BlockHarvestInfo& block, const ItemStack& held) noexcept;

// Beta Item.getStrVsBlock: material efficiency on effective blocks, 1.5 for swords, else 1.
[[nodiscard]] float toolSpeed(const BlockHarvestInfo& block, const ItemStack& held) noexcept;

[[nodiscard]] DigResult computeDig(const BlockHarvestInfo& block, const ItemStack& held,
                                   DigConditions conditions = {}) noexcept;
[[nodiscard]] DigResult computeDig(voxel::BlockId block, const ItemStack& held,
                                   DigConditions conditions = {}) noexcept;

struct BlockBreakOutcome {
    bool broken{false};             // False for unbreakable blocks (nothing happened)
    std::optional<ItemStack> drop{}; // BlockDef::dropId x1 when harvested, nullopt otherwise
    // Second, independent roll: leaves give an apple 1 time in 200; the seeds of crops.
    std::optional<ItemStack> bonusDrop{};
    WearResult toolWear{WearResult::Unchanged};
    // Beta BlockIce.harvestBlock: the caller places flowing water in the broken cell when the
    // block below is solid or liquid (this module has no world access).
    bool meltsIntoWater{false};
};

// Applies the consequences of a completed dig (call it on the tick MiningProgress reports a
// break or click() returns true): decides the drop and wears the held tool. Beta wears the
// tool even when the block yields nothing (e.g. stone pickaxe on diamond ore). Drops follow
// BlockDef::dropId x1 except where Beta differs (ice, glass and bookshelves drop nothing,
// glowstone 2-4 dust, diamond ore a diamond, redstone ore 4-5 redstone, snow blocks 4
// snowballs, leaves a sapling 1 time in 20 and, as a bonus roll, an apple 1 time in 200 -
// not in Beta 1.7.3, but the only food before mobs); `dropSeed` feeds those random rolls
// deterministically, so pass a fresh value from the world RNG per broken block.
// Tall grass gives seeds 1 time in 8, a door or bed half its item, crops (by
// their stage `meta`) wheat when ripe as `drop` and 0..3 seeds as `bonusDrop`.
// Gravel gives flint 1 time in kGravelFlintOneIn, gravel otherwise.
BlockBreakOutcome breakBlockWith(voxel::BlockId block, ItemStack& held, uint64_t dropSeed = 0,
                                 uint8_t meta = 0) noexcept;

// Beta 1.7.3 BlockGravel.idDropped: rand(10) == 0 gives flint.
inline constexpr int32_t kGravelFlintOneIn = 10;

// What a block destroyed by an explosion drops when its 0.3 chance comes up (Beta
// Block.dropBlockAsItemWithChance): the same items as a harvest, without the tool check
// (stone gives cobblestone, obsidian would give obsidian); nothing for air and liquids, even
// for unbreakable blocks the caller let through.
[[nodiscard]] BlockBreakOutcome explosionDrops(voxel::BlockId block, uint64_t dropSeed = 0, uint8_t meta = 0) noexcept;

// Hold-to-mine accumulator (Beta PlayerControllerSP.curBlockDamage). Each held tick adds the
// *current* DigResult::progressPerTick, so changing the held item, jumping or entering water
// mid-dig keeps the progress already made and only mines the remainder at the new rate. Under
// constant conditions the break lands exactly on DigResult::ticks. Progress restarts whenever
// the targeted block changes; after each held break the next kBlockHitDelayTicks advances do
// nothing (Beta blockHitDelay).
class MiningProgress {
public:
    // Advances one held game tick on `target`. Returns true on the tick the block breaks.
    bool advance(const core::BlockPos& target, const DigResult& dig) noexcept;
    // Initial button press on `target` (Beta PlayerControllerSP.clickBlock). Breaks blocks
    // whose strength is >= 1 at once, ignoring any pending hit delay and without starting a
    // new one; returns false for every other block (the held ticks that follow mine it).
    bool click(const core::BlockPos& target, const DigResult& dig) noexcept;
    // Drops the current progress and the pending hit delay (button released / target lost,
    // Beta resetBlockRemoving).
    void reset() noexcept;

    [[nodiscard]] int32_t elapsedTicks() const noexcept { return m_elapsed; }
    // Held ticks still to be swallowed before the next block takes damage.
    [[nodiscard]] int32_t cooldownTicks() const noexcept { return m_cooldown; }
    [[nodiscard]] float fraction() const noexcept;
    // Crack overlay stage 0..9 (Beta: (int)(damage * 10)), -1 when nothing is being mined.
    [[nodiscard]] int32_t crackStage() const noexcept;

private:
    void clearProgress() noexcept; // Keeps the hit delay

    core::BlockPos m_target{};
    double m_progress{0.0}; // 0..1 fraction of the block broken
    int32_t m_elapsed{0};
    int32_t m_cooldown{0};
    bool m_active{false};
};

} // namespace betacraft::gameplay
