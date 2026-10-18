#include "gameplay/Harvesting.hpp"

#include "gameplay/Crops.hpp"
#include "gameplay/SplitMix64.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::gameplay {

namespace {

using world::BlockId;

// Hardness is stored as float (0.6f * 30 = 18.0000007): tolerate representation error, in
// ticks, so exact multiples do not round up to an extra tick.
constexpr double kTickEpsilon = 1e-4;

struct ToolRule {
    ToolType effective{ToolType::None};
    ToolType required{ToolType::None};
    int32_t level{0};
};

// Beta 1.7.3 ItemPickaxe / ItemAxe / ItemSpade "blocksEffectiveAgainst" lists and
// canHarvestBlock overrides, restricted to the blocks this engine defines.
[[nodiscard]] constexpr ToolRule toolRule(BlockId id) noexcept {
    constexpr ToolType P = ToolType::Pickaxe;
    constexpr ToolType A = ToolType::Axe;
    constexpr ToolType S = ToolType::Shovel;
    constexpr ToolType N = ToolType::None;
    switch (id) {
        // Rock: pickaxe-effective and pickaxe-required.
        case BlockId::Stone:
        case BlockId::Cobblestone:
        case BlockId::MossyCobble:
        case BlockId::Sandstone:
        case BlockId::CobbleSlab:
        case BlockId::CoalOre:     return {P, P, 0};
        case BlockId::IronOre:
        case BlockId::LapisOre:    return {P, P, 1};
        case BlockId::GoldOre:
        case BlockId::DiamondOre:  return {P, P, 2};
        // Rock material but missing from Beta's effective list: pickaxe needed, no speed-up.
        case BlockId::Bricks:
        case BlockId::CobbleStairs: // Material.rock, missing from ItemPickaxe's list
        case BlockId::Furnace:
        case BlockId::LitFurnace:
        case BlockId::MobSpawner:  return {N, P, 0};
        case BlockId::RedstoneOre: return {N, P, 2};
        // S8.5: Material.rock / Material.iron, not in ItemPickaxe's list.
        case BlockId::StonePressurePlate:
        case BlockId::IronDoor:    return {N, P, 0};
        case BlockId::Obsidian:    return {N, P, 3};
        // Ice is in the pickaxe list but its material is harvestable by hand.
        case BlockId::Ice:         return {P, N, 0};

        case BlockId::OakLog:
        case BlockId::BirchLog:
        case BlockId::PineLog:
        case BlockId::OakPlanks:
        case BlockId::Bookshelf:
        case BlockId::Chest:       return {A, N, 0};

        case BlockId::Grass:
        case BlockId::SnowGrass:
        case BlockId::Dirt:
        case BlockId::Sand:
        case BlockId::Gravel:
        case BlockId::Clay:
        case BlockId::Farmland:    return {S, N, 0}; // Block.tilledField is in ItemSpade's list
        case BlockId::SnowBlock:   return {S, S, 0}; // Material.builtSnow is setNoHarvest()

        default:                   return {};
    }
}

// What a harvested block yields: `item` x [minCount, maxCount], with probability 1/oneIn.
struct DropRule {
    ItemId item{kNoItem};
    int32_t minCount{0};
    int32_t maxCount{0};
    int32_t oneIn{1};
};

// BlockDef::dropId is a simplification shared with the renderer/inventory; these are the
// blocks whose Beta 1.7.3 quantityDropped / idDropped differ from "dropId x1".
[[nodiscard]] DropRule dropRule(BlockId id) noexcept {
    switch (id) {
        case BlockId::Ice:         // quantityDropped 0 (melts instead)
        case BlockId::Glass:       // quantityDropped 0
        case BlockId::Bookshelf:   return {};
        case BlockId::Glowstone:   return {kGlowstoneDustItem, 2, 4, 1};
        case BlockId::Leaves:      return {toItemId(BlockId::Sapling), 1, 1, 20};
        case BlockId::DiamondOre:  return {items::Diamond, 1, 1, 1};
        case BlockId::RedstoneOre: return {items::Redstone, 4, 5, 1}; // 4 + nextInt(2)
        case BlockId::SnowBlock:   return {items::Snowball, 4, 4, 1};
        case BlockId::TallGrass:   return {items::Seeds, 1, 1, 8}; // BlockTallGrass.idDropped
        // One item per door or bed: whichever half is broken, the other goes without a drop.
        case BlockId::WoodDoor:    return {items::WoodenDoor, 1, 1, 1};
        case BlockId::Bed:         return {items::Bed, 1, 1, 1};
        case BlockId::IronDoor:    return {items::IronDoor, 1, 1, 1};
        case BlockId::RedstoneWire: return {items::Redstone, 1, 1, 1};
        case BlockId::Crops:       return {}; // rollCropHarvest, from the stage
        default: {
            const BlockId drop = world::BlockRegistry::get(id).dropId;
            // canonicalize: coal ore and dead bushes name the legacy item BlockIds.
            return drop == BlockId::Air ? DropRule{} : DropRule{canonicalize(toItemId(drop)), 1, 1, 1};
        }
    }
}

// Drops rolled on top of dropRule, independently of it (S2.9). Apples from leaves are a
// deliberate deviation from Beta 1.7.3: there is no other food before the mobs.
[[nodiscard]] std::optional<DropRule> bonusDropRule(BlockId id) noexcept {
    if (id == BlockId::Leaves) {
        return DropRule{items::Apple, 1, 1, 200};
    }
    return std::nullopt;
}

// Decorrelates the bonus roll from the main one drawn from the same seed.
constexpr uint64_t kBonusSeedSalt = 0xA99E5EEDB0A05ULL;

[[nodiscard]] std::optional<ItemStack> rollDrop(const DropRule& rule, uint64_t seed) noexcept {
    if (rule.item == kNoItem || rule.maxCount <= 0) {
        return std::nullopt;
    }
    SplitMix64 rng(seed);
    if (rule.oneIn > 1 && rng.nextInt(rule.oneIn) != 0) {
        return std::nullopt;
    }
    const int32_t count = rule.minCount + rng.nextInt(rule.maxCount - rule.minCount + 1);
    const ItemStack stack = makeStack(rule.item, count);
    return stack.empty() ? std::nullopt : std::optional<ItemStack>{stack};
}

} // namespace

BlockHarvestInfo harvestInfo(world::BlockId block) noexcept {
    const world::BlockDef& def = world::BlockRegistry::get(block);
    // BlockRegistry fills unregistered slots with a default def still named "Air"; item-only
    // ids (tools, sticks) never exist in the world. Neither can be mined.
    const bool unregistered = def.id != block || def.name == world::BlockDef{}.name;
    if (block == BlockId::Air || unregistered || def.isItemOnly) {
        return {};
    }
    const ToolRule rule = toolRule(block);
    return {def.hardness, rule.effective, rule.required, rule.level};
}

bool canHarvest(const BlockHarvestInfo& block, const ItemStack& held) noexcept {
    if (block.requiredTool == ToolType::None) {
        return true;
    }
    const auto tool = toolOf(held);
    return tool && tool->type == block.requiredTool &&
           materialStats(tool->material).harvestLevel >= block.minHarvestLevel;
}

float toolSpeed(const BlockHarvestInfo& block, const ItemStack& held) noexcept {
    const auto tool = toolOf(held);
    if (!tool) {
        return 1.0f;
    }
    if (tool->type == ToolType::Sword) {
        return kSwordDigSpeed;
    }
    if (tool->type != ToolType::None && tool->type == block.effectiveTool) {
        return materialStats(tool->material).efficiency;
    }
    return 1.0f;
}

DigResult computeDig(const BlockHarvestInfo& block, const ItemStack& held,
                     DigConditions conditions) noexcept {
    DigResult result{};
    if (!block.breakable()) {
        return result;
    }
    result.breakable = true;
    result.dropsItems = canHarvest(block, held);

    // Beta's unharvestable branch (1 / hardness / 100) never consults the player's dig
    // strength, so both the tool speed and the water / airborne penalties only apply when
    // the held item can harvest the block.
    double speed = 1.0;
    if (result.dropsItems) {
        speed = static_cast<double>(toolSpeed(block, held));
        if (conditions.inWater) {
            speed /= kEnvironmentPenalty;
        }
        if (!conditions.onGround) {
            speed /= kEnvironmentPenalty;
        }
    }
    const double divisor = result.dropsItems ? kHarvestableDivisor : kUnharvestableDivisor;
    const double ticksNeeded = static_cast<double>(block.hardness) * divisor / speed;

    if (ticksNeeded <= 1.0 + kTickEpsilon) {
        result.instant = true;
        result.ticks = 0;
        result.progressPerTick = ticksNeeded > 0.0 ? 1.0 / ticksNeeded
                                                   : std::numeric_limits<double>::infinity();
        result.seconds = 0.0f;
        return result;
    }
    // Saturate before the cast: converting a float beyond int32 range (huge or infinite
    // hardness from a custom table) is undefined behaviour.
    constexpr double kMaxTicks = static_cast<double>(kUnbreakableTicks);
    const double roundedTicks = std::ceil(ticksNeeded - kTickEpsilon);
    result.ticks = roundedTicks < kMaxTicks ? static_cast<int32_t>(roundedTicks) : kUnbreakableTicks;
    result.progressPerTick = 1.0 / ticksNeeded;
    result.seconds = static_cast<float>(result.ticks) / static_cast<float>(kTicksPerSecond);
    return result;
}

DigResult computeDig(world::BlockId block, const ItemStack& held, DigConditions conditions) noexcept {
    return computeDig(harvestInfo(block), held, conditions);
}

namespace {

// The drops of a harvested block (the tool check already passed), into `outcome`.
void rollHarvestDrops(world::BlockId block, uint64_t dropSeed, uint8_t meta, BlockBreakOutcome& outcome) noexcept {
    outcome.drop = rollDrop(dropRule(block), dropSeed);
    if (const std::optional<DropRule> bonus = bonusDropRule(block)) {
        outcome.bonusDrop = rollDrop(*bonus, dropSeed ^ kBonusSeedSalt);
    }
    if (block == world::BlockId::Gravel) {
        // S6.6 Beta BlockGravel.idDropped: flint 1 time in 10, the gravel otherwise.
        SplitMix64 rng(dropSeed);
        outcome.drop = makeStack(rng.nextInt(kGravelFlintOneIn) == 0 ? items::Flint : toItemId(block), 1);
    }
    if (block == world::BlockId::Crops) {
        // S5.7: wheat when ripe as the drop, the seeds as the second stack.
        SplitMix64 rng(dropSeed);
        const CropHarvest harvest = rollCropHarvest(meta, rng);
        const ItemStack wheat = makeStack(items::Wheat, harvest.wheat);
        const ItemStack seeds = makeStack(items::Seeds, harvest.seeds);
        outcome.drop = wheat.empty() ? std::nullopt : std::optional<ItemStack>{wheat};
        outcome.bonusDrop = seeds.empty() ? std::nullopt : std::optional<ItemStack>{seeds};
    }
}

} // namespace

BlockBreakOutcome breakBlockWith(world::BlockId block, ItemStack& held, uint64_t dropSeed, uint8_t meta) noexcept {
    const BlockHarvestInfo info = harvestInfo(block);
    BlockBreakOutcome outcome{};
    if (!info.breakable()) {
        return outcome;
    }
    outcome.broken = true;
    // Decide the drop before wear: the use that breaks the tool still harvests the block.
    if (canHarvest(info, held)) {
        rollHarvestDrops(block, dropSeed, meta, outcome);
        outcome.meltsIntoWater = block == world::BlockId::Ice; // Only harvestBlock melts it
    }
    outcome.toolWear = onBlockBroken(held);
    return outcome;
}

BlockBreakOutcome explosionDrops(world::BlockId block, uint64_t dropSeed, uint8_t meta) noexcept {
    BlockBreakOutcome outcome{};
    if (block == world::BlockId::Air || world::BlockRegistry::isLiquid(block)) {
        return outcome;
    }
    outcome.broken = true;
    rollHarvestDrops(block, dropSeed, meta, outcome);
    return outcome;
}

bool MiningProgress::advance(const core::BlockPos& target, const DigResult& dig) noexcept {
    if (m_cooldown > 0) {
        --m_cooldown;
        return false;
    }
    if (!dig.breakable) {
        clearProgress();
        return false;
    }
    if (!m_active || !(target == m_target)) {
        m_target = target;
        m_progress = 0.0;
        m_elapsed = 0;
        m_active = true;
    }
    ++m_elapsed;
    m_progress += dig.progressPerTick;
    // Same tolerance as computeDig, expressed as a fraction of this tick's progress, so a
    // constant-rate dig breaks exactly on tick DigResult::ticks.
    if (dig.instant || m_progress + kTickEpsilon * dig.progressPerTick >= 1.0) {
        clearProgress();
        m_cooldown = kBlockHitDelayTicks;
        return true;
    }
    return false;
}

bool MiningProgress::click(const core::BlockPos& target, const DigResult& dig) noexcept {
    if (!dig.breakable || !dig.instant) {
        return false;
    }
    // The clicked block is gone: drop any progress on it, but leave a pending hit delay alone
    // (Beta clickBlock neither reads nor sets blockHitDelay).
    if (m_active && target == m_target) {
        clearProgress();
    }
    return true;
}

void MiningProgress::reset() noexcept {
    clearProgress();
    m_cooldown = 0;
}

void MiningProgress::clearProgress() noexcept {
    m_progress = 0.0;
    m_elapsed = 0;
    m_active = false;
}

float MiningProgress::fraction() const noexcept {
    if (!m_active) {
        return 0.0f;
    }
    return static_cast<float>(std::min(1.0, m_progress));
}

int32_t MiningProgress::crackStage() const noexcept {
    if (!m_active) {
        return -1;
    }
    return std::clamp(static_cast<int32_t>(fraction() * 10.0f), 0, 9);
}

} // namespace betacraft::gameplay
