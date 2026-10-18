#include "game/EntitySimulation.hpp"

#include "ai/WorldNavQuery.hpp"
#include "crafting/Furnace.hpp"
#include "crafting/ItemIds.hpp"
#include "ecs/Components.hpp"
#include "ecs/Systems.hpp"
#include "game/FallingBlocks.hpp"
#include "game/ItemBridge.hpp"
#include "game/Projectiles.hpp"
#include "gameplay/Armor.hpp"
#include "gameplay/DeathDrops.hpp"
#include "gameplay/Explosion.hpp"
#include "gameplay/Food.hpp"
#include "gameplay/Health.hpp"
#include "gameplay/Knockback.hpp"
#include "gameplay/MobSpawnRules.hpp"
#include "gameplay/Survival.hpp"
#include "world/BlockShapes.hpp"
#include "world/BlockTicks.hpp"
#include "world/World.hpp"
#include "worldgen/Dungeons.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace betacraft::game {

namespace {

// Beta EntityItem: motion X/Z in [-0.1, 0.1) and Y 0.2 blocks per tick on spawn.
constexpr float kDropPopHorizontal = 0.1f * static_cast<float>(gameplay::kTicksPerSecond);
constexpr float kDropPopVertical = 0.2f * static_cast<float>(gameplay::kTicksPerSecond);

[[nodiscard]] gameplay::Health toHealth(const ecs::HealthComp& h, const CorpseComp* corpse) noexcept {
    gameplay::Health out{};
    out.current = h.current;
    out.max = h.max;
    out.invulnerableTicks = static_cast<int32_t>(
        std::lround(h.hurtInvulnerabilityTimer * static_cast<float>(gameplay::kTicksPerSecond)));
    out.lastDamage = h.lastDamage;
    out.deathTicks = corpse != nullptr ? corpse->ticks : 0;
    return out;
}

// Beta EntityLiving.knockBack from a mob's hit: 0.4 blocks/tick away from the attacker and
// 0.4 up (Player::applyKnockback halves the velocity and caps the upward speed).
constexpr gameplay::KnockbackParams kMobHitKnockback{.horizontalSpeed = kMobKnockbackSpeed,
                                                     .verticalSpeed = kMobKnockbackSpeed,
                                                     .velocityRetention = 0.5f,
                                                     .direction = gameplay::KnockbackDirection::AttackerToTarget};

[[nodiscard]] int16_t toInt16(int32_t v) noexcept {
    return static_cast<int16_t>(std::clamp(v, 0, static_cast<int32_t>(std::numeric_limits<int16_t>::max())));
}

} // namespace

EntitySimulation::EntitySimulation(uint64_t seed)
    : m_rng(seed), m_spawnRng(seed ^ 0x6D6F62737061776EULL), m_mobRng(seed ^ 0x6D6F626C6976696EULL) {}

GameTickEvents EntitySimulation::tick(world::World& world, physics::Player& player, const PlayerActions& actions,
                                 float timeOfDay) {
    // Hits and explosions recorded by explode() calls made between ticks come first.
    GameTickEvents events = std::exchange(m_pendingEvents, GameTickEvents{});
    m_tickEvents = &events;
    ++m_ticks;
    syncPlayerProxy(player);
    tickPlayerSurvival(world, player, events);
    if (player.isDead()) {
        resetMining();
    } else {
        handleAttack(world, player, actions, events);
    }
    tickBlockEntities(world);
    spawnUnsupportedDrops(world);
    spawnRemovedBlockEntityDrops(world);
    runEntitySystems(world, player, timeOfDay, events);
    tickFallingBlocks(world);
    (void)tickPressurePlates(world, player);
    tickPrimedTnt();
    runExplosionQueue(world, player, events);
    if (!player.isDead()) {
        events.itemsPickedUp = pickUpItems(player);
    }
    tickSleep(world, player, events);
    m_tickEvents = nullptr;
    return events;
}

void EntitySimulation::tickSleep(const world::World& world, const physics::Player& player, GameTickEvents& events) {
    if (!m_sleepingBed) {
        return;
    }
    // Beta wakeUpPlayer: a hit, death or the bed going away ends the sleep early.
    if (player.isDead() || !events.playerHurt.empty() || !bedHeadAt(world, *m_sleepingBed)) {
        wakeUp();
        return;
    }
    if (++m_sleepTicks >= kSleepTicks) {
        events.wokeInBed = m_sleepingBed;
        wakeUp();
    }
}

SleepResult EntitySimulation::trySleep(const world::World& world, const physics::Player& player, const core::BlockPos& bed,
                                  float timeOfDay) {
    const std::optional<core::BlockPos> head = bedHeadAt(world, bed);
    if (!head || player.isDead()) {
        return SleepResult::NotABed;
    }
    if (isDaytime(timeOfDay)) {
        return SleepResult::NotNight;
    }
    bool monsters = false;
    m_entities.view<ecs::TransformComp, ecs::MobKindComp>().each(
        [&](ecs::EntityId e, const ecs::TransformComp& t, const ecs::MobKindComp& mob) {
            if (monsters || m_entities.has<ecs::PlayerTagComp>(e) || m_entities.has<CorpseComp>(e) ||
                gameplay::mobDef(mob.kind).category != gameplay::MobCategory::Hostile) {
                return;
            }
            const glm::vec3 d = t.pos - glm::vec3(static_cast<float>(head->x) + 0.5f, static_cast<float>(head->y),
                                                  static_cast<float>(head->z) + 0.5f);
            monsters = std::abs(d.x) <= static_cast<float>(kBedMonsterReachXZ) + 0.5f &&
                       std::abs(d.z) <= static_cast<float>(kBedMonsterReachXZ) + 0.5f &&
                       d.y >= -static_cast<float>(kBedMonsterReachY) &&
                       d.y <= static_cast<float>(kBedMonsterReachY) + 1.0f;
        });
    if (monsters) {
        return SleepResult::MonstersNearby;
    }
    m_sleepingBed = head;
    m_sleepTicks = 0;
    return SleepResult::Asleep;
}

float EntitySimulation::sleepFade() const noexcept {
    return m_sleepingBed ? std::clamp(static_cast<float>(m_sleepTicks) / static_cast<float>(kSleepTicks), 0.0f, 1.0f)
                         : 0.0f;
}

void EntitySimulation::wakeUp() noexcept {
    m_sleepingBed.reset();
    m_sleepTicks = 0;
}

gameplay::HitResult EntitySimulation::damagePlayer(physics::Player& player, const PlayerDamage& damage,
                                             GameTickEvents& events) {
    if (player.isCreative() || player.isDead()) {
        return {};
    }
    // S6.7 Beta EntityPlayer.damageEntity: the part of the hit that lands wears every worn
    // piece by its full amount, then the armor value computed before the wear absorbs it
    // (fractions carried in the remainder). Only the void bypasses armor.
    const gameplay::HitResult hit =
        gameplay::applyDamage(player.health(), damage.amount, [&](int32_t landing) noexcept {
            if (damage.bypassArmor) {
                return landing;
            }
            std::array<gameplay::ItemStack, gameplay::kArmorSlotCount> worn{};
            for (size_t i = 0; i < worn.size(); ++i) {
                worn[i] = toItemStack(player.getArmorSlot(i));
            }
            const int32_t armor = gameplay::totalArmorValue(worn);
            gameplay::damageArmor(worn, landing);
            for (size_t i = 0; i < worn.size(); ++i) {
                player.setArmorSlot(i, toInventorySlot(worn[i]));
            }
            return gameplay::absorbDamage(landing, armor, player.armorDamageRemainder());
        });
    if (hit.outcome == gameplay::HitOutcome::Full) {
        player.onHurt(); // Beta hurtTime: only fresh hits tilt the camera
    }
    if (damage.sourcePos && hit.appliesKnockback()) {
        player.applyKnockback(gameplay::computeKnockbackImpulse(glm::vec3(0.0f), *damage.sourcePos,
                                                                player.getPosition(), kMobHitKnockback));
    }
    if (hit.applied > 0 || hit.killed) {
        events.playerHurt.push_back({hit.applied, damage.cause, hit.killed});
    }
    if (hit.killed) {
        events.playerDied = true;
    }
    return hit;
}

// Beta EntityLiving.onEntityUpdate / fall order: landing damage, fire then lava
// (Entity.onEntityUpdate), cactus contact (moveEntity), then air, then the void, then the
// hurt-resistance timers count down. Every hit goes through damagePlayer, so the resistance
// window keeps lava from killing in 5 ticks.
void EntitySimulation::tickPlayerSurvival(world::World& world, physics::Player& player, GameTickEvents& events) {
    const float fall = player.takePendingFall();
    player.tickHurtTime();
    if (player.isCreative()) {
        player.breath().air = gameplay::kMaxAirTicks;
        player.burning().fireTicks = 0;
        gameplay::tickHealth(player.health());
        return;
    }
    if (const int32_t dmg = gameplay::fallDamage(fall); dmg > 0) {
        (void)damagePlayer(player, {.amount = dmg, .cause = DamageCause::Fall}, events);
    }
    if (const int32_t dmg = gameplay::tickFire(player.burning(), player.isInLava(), player.isInWaterMaterial());
        dmg > 0) {
        (void)damagePlayer(player, {.amount = dmg, .cause = DamageCause::Fire}, events);
    }
    if (player.isInLava()) {
        (void)damagePlayer(player, {.amount = gameplay::kLavaDamage, .cause = DamageCause::Lava}, events);
    }
    // Beta EntityLiving.onEntityUpdate: eyes inside an opaque solid block (fallen sand, a
    // closing piston) hurt by 1 every tick, the resistance window spacing the hits.
    const glm::vec3 eye = player.getEyePosition();
    const world::BlockId eyeBlock = world.getBlock(static_cast<int32_t>(std::floor(eye.x)),
                                                   static_cast<int32_t>(std::floor(eye.y)),
                                                   static_cast<int32_t>(std::floor(eye.z)));
    if (world::BlockRegistry::isOpaque(eyeBlock) && world::BlockRegistry::isSolid(eyeBlock)) {
        (void)damagePlayer(player, {.amount = gameplay::kSuffocationDamage, .cause = DamageCause::Suffocation}, events);
    }
    if (player.isTouchingCactus()) {
        (void)damagePlayer(player, {.amount = gameplay::kCactusDamage, .cause = DamageCause::Cactus}, events);
    }
    if (const int32_t dmg = gameplay::tickBreath(player.breath(), player.isHeadUnderWater()); dmg > 0) {
        (void)damagePlayer(player, {.amount = dmg, .cause = DamageCause::Drown}, events);
    }
    if (const int32_t dmg = gameplay::outOfWorldDamage(player.getPosition().y); dmg > 0) {
        (void)damagePlayer(player, {.amount = dmg, .cause = DamageCause::Void, .bypassArmor = true}, events);
    }
    gameplay::tickHealth(player.health());
}

void EntitySimulation::syncPlayerProxy(const physics::Player& player) {
    const glm::vec3 feet = player.getPosition();
    if (ecs::TransformComp* t = m_entities.valid(m_playerProxy) ? m_entities.tryGet<ecs::TransformComp>(m_playerProxy)
                                                                : nullptr) {
        t->prevPos = t->pos;
        t->pos = feet;
        return;
    }
    m_playerProxy = ecs::EntityFactory::spawnPlayerProxy(m_entities, feet);
}

size_t EntitySimulation::spawnMobDrops(gameplay::MobKind kind, const glm::vec3& pos, bool burning) {
    size_t spawned = 0;
    for (const gameplay::MobDropRoll& roll : gameplay::rollMobDrops(kind, burning, m_rng)) {
        // Beta Entity.entityDropItem: from the mob's feet with EntityItem's random pop.
        const glm::vec3 velocity((m_rng.nextFloat() * 2.0f - 1.0f) * kDropPopHorizontal, kDropPopVertical,
                                 (m_rng.nextFloat() * 2.0f - 1.0f) * kDropPopHorizontal);
        if (spawnItem(gameplay::makeStack(roll.item, roll.count), pos, velocity) != ecs::NULL_ENTITY) {
            ++spawned;
        }
    }
    return spawned;
}

void EntitySimulation::resetMining() noexcept {
    m_mining.reset();
    m_miningTarget.reset();
}

ecs::EntityId EntitySimulation::spawnItem(const gameplay::ItemStack& stack, const glm::vec3& pos, const glm::vec3& velocity) {
    if (stack.empty()) {
        return ecs::NULL_ENTITY;
    }
    return ecs::EntityFactory::spawnItemDrop(m_entities, pos, stack.id, stack.count, velocity, stack.damage);
}

ecs::EntityId EntitySimulation::spawnBlockDrop(const gameplay::ItemStack& stack, const core::BlockPos& cell) {
    const float margin = (1.0f - DROP_SPREAD) * 0.5f;
    const glm::vec3 pos(static_cast<float>(cell.x) + margin + m_rng.nextFloat() * DROP_SPREAD,
                        static_cast<float>(cell.y) + margin + m_rng.nextFloat() * DROP_SPREAD,
                        static_cast<float>(cell.z) + margin + m_rng.nextFloat() * DROP_SPREAD);
    const glm::vec3 velocity((m_rng.nextFloat() * 2.0f - 1.0f) * kDropPopHorizontal, kDropPopVertical,
                             (m_rng.nextFloat() * 2.0f - 1.0f) * kDropPopHorizontal);
    return spawnItem(stack, pos, velocity);
}

bool EntitySimulation::spawnNaturalDrop(world::BlockId block, const core::BlockPos& cell, uint8_t meta) {
    // Beta dropBlockAsItem without a player: the bare-hand drop table, nothing to wear.
    gameplay::ItemStack noTool{};
    const gameplay::BlockBreakOutcome outcome = gameplay::breakBlockWith(block, noTool, m_rng.next(), meta);
    if (outcome.bonusDrop) {
        (void)spawnBlockDrop(*outcome.bonusDrop, cell);
    }
    return outcome.drop && spawnBlockDrop(*outcome.drop, cell) != ecs::NULL_ENTITY;
}

size_t EntitySimulation::spawnFluidDrops(std::span<const fluids::FluidEvent> events) {
    size_t spawned = 0;
    for (const fluids::FluidEvent& ev : events) {
        if (ev.kind == fluids::FluidEventKind::BlockWashedAway && spawnNaturalDrop(ev.block, ev.pos)) {
            ++spawned;
        }
    }
    return spawned;
}

size_t EntitySimulation::spawnUnsupportedDrops(world::World& world) {
    size_t spawned = 0;
    for (const world::World::UnsupportedBreak& broken : world.takeUnsupportedBreaks()) {
        if (spawnNaturalDrop(broken.block, broken.pos, broken.meta)) {
            ++spawned;
        }
    }
    return spawned;
}

void EntitySimulation::tickFallingBlocks(world::World& world) {
    for (const world::World::FallingBlockSpawn& spawn : world.takeFallingBlockSpawns()) {
        (void)spawnFallingBlock(m_entities, spawn.pos, spawn.id);
    }
    struct Landed {
        ecs::EntityId entity;
        world::BlockId id;
        FallingBlockStep step;
    };
    std::vector<Landed> landed;
    m_entities.view<ecs::TransformComp, ecs::VelocityComp, ecs::FallingBlockComp>().each(
        [&](ecs::EntityId e, const ecs::TransformComp& t, const ecs::VelocityComp& v, ecs::FallingBlockComp& falling) {
            ++falling.ticks;
            const FallingBlockStep step = resolveFallingBlock(world, t, v, falling);
            if (step.outcome != FallingBlockOutcome::Falling) {
                landed.push_back({e, falling.id, step});
            }
        });
    // Spawning drops would grow the pools under the view: act once it is done.
    for (const Landed& l : landed) {
        if (l.step.outcome == FallingBlockOutcome::Place) {
            (void)world.setBlockWithMetadata(l.step.cell.x, l.step.cell.y, l.step.cell.z, l.id, 0);
        } else {
            (void)spawnBlockDrop(gameplay::makeStack(gameplay::toItemId(l.id), 1), l.step.cell);
        }
        (void)m_entities.destroy(l.entity);
    }
}

size_t EntitySimulation::spawnRemovedBlockEntityDrops(world::World& world) {
    size_t spawned = 0;
    const auto drop = [&](const world::StoredStack& s, const core::BlockPos& pos) {
        if (s.empty()) return;
        const physics::InventorySlot slot{s.id, s.count, s.damage};
        if (spawnBlockDrop(toItemStack(slot), pos) != ecs::NULL_ENTITY) {
            ++spawned;
        }
    };
    for (const world::World::RemovedBlockEntity& removed : world.takeRemovedBlockEntities()) {
        if (const auto* furnace = std::get_if<world::FurnaceData>(&removed.entity)) {
            drop(furnace->input, removed.pos);
            drop(furnace->fuel, removed.pos);
            drop(furnace->output, removed.pos);
        } else if (const auto* chest = std::get_if<world::ChestData>(&removed.entity)) {
            for (const world::StoredStack& s : chest->slots) {
                drop(s, removed.pos);
            }
        }
    }
    return spawned;
}

void EntitySimulation::tickBlockEntities(world::World& world) {
    struct Change {
        core::BlockPos pos;
        bool litChanged;
        bool burning;
    };
    std::vector<Change> changes;
    const crafting::SmeltingBook& book = crafting::SmeltingBook::beta();
    world.blockEntities().forEach([&](const core::BlockPos& pos, world::BlockEntity& entity) {
        auto* furnace = std::get_if<world::FurnaceData>(&entity);
        if (furnace == nullptr) return;
        const crafting::FurnaceTickResult r = crafting::tickFurnace(*furnace, book);
        if (r.changed) {
            changes.push_back({pos, r.litChanged, furnace->burnTicks > 0});
        }
    });
    // Block edits after the walk: setBlock must not run while the store is being iterated.
    for (const Change& c : changes) {
        if (c.litChanged) {
            const world::BlockId current = world.getBlock(c.pos);
            if (current == world::BlockId::Furnace || current == world::BlockId::LitFurnace) {
                (void)world.setBlock(c.pos, c.burning ? world::BlockId::LitFurnace : world::BlockId::Furnace);
            }
        }
        world.markBlockEntityChanged(c.pos);
    }
}

std::optional<int32_t> EntitySimulation::useHeldItem(physics::Player& player) {
    if (player.isDead()) {
        return std::nullopt;
    }
    const physics::InventorySlot held = player.getHeldItem();
    const std::optional<gameplay::FoodInfo> food = held.isEmpty() ? std::nullopt : gameplay::foodInfo(held.id);
    if (!food) {
        return std::nullopt;
    }
    // Beta ItemFood.onItemRightClick: one item eaten, health topped up, even when full.
    const int32_t healed = gameplay::heal(player.health(), food->heal);
    (void)player.consumeHeldItem(1);
    if (food->leftover != gameplay::kNoItem) {
        if (player.getHeldItem().isEmpty()) {
            player.setHeldItem(physics::InventorySlot{food->leftover, 1});
        } else if (const int32_t left = player.insertStack({food->leftover, 1}); left > 0) {
            (void)dropFromPlayer(player, gameplay::makeStack(food->leftover, left));
        }
    }
    player.triggerHandSwing();
    return healed;
}

ecs::EntityId EntitySimulation::dropFromPlayer(const physics::Player& player, const gameplay::ItemStack& stack) {
    // Beta dropPlayerItem: 0.3 blocks/tick along the look plus a 0.1 lift.
    constexpr float kThrowSpeed = 0.3f * static_cast<float>(gameplay::kTicksPerSecond);
    constexpr float kThrowLift = 0.1f * static_cast<float>(gameplay::kTicksPerSecond);
    const glm::vec3 pos = player.getEyePosition() - glm::vec3(0.0f, gameplay::kDeathDropEyeOffset, 0.0f);
    const glm::vec3 velocity = player.getLookDirection() * kThrowSpeed + glm::vec3(0.0f, kThrowLift, 0.0f);
    const ecs::EntityId e = spawnItem(stack, pos, velocity);
    if (e != ecs::NULL_ENTITY) {
        m_entities.get<ecs::ItemDropComp>(e).pickupDelaySeconds =
            static_cast<float>(gameplay::kDeathDropPickupDelayTicks) * TICK_SECONDS;
    }
    return e;
}

ecs::EntityId EntitySimulation::dropHeldItem(physics::Player& player, bool wholeStack) {
    const physics::InventorySlot held = player.getHeldItem();
    if (held.isEmpty() || player.isDead()) {
        return ecs::NULL_ENTITY;
    }
    const int32_t count = wholeStack ? held.count : 1;
    const ecs::EntityId e = dropFromPlayer(player, toItemStack(physics::InventorySlot{held.id, count, held.damage}));
    if (e != ecs::NULL_ENTITY) {
        (void)player.consumeHeldItem(count);
    }
    return e;
}

size_t EntitySimulation::dropInventoryOnDeath(physics::Player& player) {
    // The 36 inventory stacks, then the 4 armor pieces (Beta InventoryPlayer.dropAllItems).
    std::array<gameplay::ItemStack, physics::Player::TOTAL_INVENTORY_SIZE + physics::Player::ARMOR_SLOT_COUNT> stacks{};
    for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
        stacks[i] = toItemStack(player.getInventorySlot(i));
        player.setInventorySlot(i, {});
    }
    for (size_t i = 0; i < physics::Player::ARMOR_SLOT_COUNT; ++i) {
        stacks[physics::Player::TOTAL_INVENTORY_SIZE + i] = toItemStack(player.getArmorSlot(i));
        player.setArmorSlot(i, {});
    }
    size_t spawned = 0;
    for (const gameplay::DroppedItem& drop :
         gameplay::scatterInventoryOnDeath(stacks, player.getEyePosition(), m_rng.next())) {
        const ecs::EntityId e = spawnItem(drop.stack, drop.position, drop.velocity);
        if (e == ecs::NULL_ENTITY) continue;
        m_entities.get<ecs::ItemDropComp>(e).pickupDelaySeconds = static_cast<float>(drop.pickupDelayTicks) * TICK_SECONDS;
        ++spawned;
    }
    return spawned;
}

bool EntitySimulation::useHeldItemOnEntity(physics::Player& player, ecs::EntityId target) {
    if (player.isDead() || !m_entities.valid(target) || m_entities.has<CorpseComp>(target)) {
        return false;
    }
    const ecs::MobKindComp* mob = m_entities.tryGet<ecs::MobKindComp>(target);
    const ecs::HealthComp* health = m_entities.tryGet<ecs::HealthComp>(target);
    const physics::InventorySlot held = player.getHeldItem();
    if (mob == nullptr || health == nullptr || health->isDead() || mob->kind != gameplay::MobKind::Cow ||
        held.isEmpty() || gameplay::canonicalize(held.id) != gameplay::items::Bucket) {
        return false;
    }
    // Beta EntityCow.interact: the bucket in the hand is replaced by a milk bucket.
    player.setHeldItem(physics::InventorySlot{gameplay::items::MilkBucket, 1});
    player.triggerHandSwing();
    return true;
}

std::optional<ecs::EntityId> EntitySimulation::meleeTarget(const physics::Player& player,
                                                      const physics::RaycastHit& blockTarget) {
    std::vector<physics::AABB> boxes;
    std::vector<ecs::EntityId> ids;
    m_entities.view<ecs::TransformComp, ecs::AABBColliderComp, ecs::HealthComp>().each(
        [&](ecs::EntityId e, const ecs::TransformComp& t, const ecs::AABBColliderComp& c, const ecs::HealthComp&) {
            boxes.push_back(c.boundsAt(t.pos));
            ids.push_back(e);
        });
    // Walls occlude entities: never reach past the block the crosshair is on.
    const float reach = blockTarget.hit ? std::min(gameplay::kDefaultMeleeReach, blockTarget.distance)
                                        : gameplay::kDefaultMeleeReach;
    const auto hit = gameplay::pickMeleeTarget(player.getEyePosition(), player.getLookDirection(), boxes, reach);
    return hit ? std::optional<ecs::EntityId>{ids[hit->index]} : std::nullopt;
}

// Beta Minecraft.clickMouse + PlayerControllerSP: a press swings at the entity under the
// crosshair if there is one, otherwise clicks the block; every tick the button stays down
// damages the targeted block (the press tick included). The click counts even when the
// button was already released before this tick (a tap shorter than 50 ms).
void EntitySimulation::handleAttack(world::World& world, physics::Player& player, const PlayerActions& actions,
                               GameTickEvents& events) {
    if (actions.attackPressed) {
        if (const auto target = meleeTarget(player, actions.target)) {
            resetMining();
            attackEntity(player, *target, events);
            return;
        }
    }
    if (!(actions.attackPressed || actions.attackHeld) || !actions.target.hit) {
        resetMining();
        return;
    }

    const core::BlockPos pos = actions.target.blockPos;
    const world::BlockId block = world.getBlock(pos); // The live block, not the raycast snapshot
    // Fly mode is a debug camera, not Beta's airborne state: it does not slow digging.
    const gameplay::DigConditions conditions{.inWater = player.isHeadUnderWater(),
                                             .onGround = player.isOnGround() || player.isFlying()};
    const gameplay::DigResult dig = gameplay::computeDig(block, toItemStack(player.getHeldItem()), conditions);
    if (!dig.breakable) {
        resetMining();
        return;
    }
    if (actions.attackPressed && m_mining.click(pos, dig)) {
        m_miningTarget.reset();
        breakBlock(world, player, pos, block, events);
        return;
    }
    if (!actions.attackHeld) {
        resetMining(); // Released within the tick: Beta resetBlockRemoving
        return;
    }
    events.digging = true;
    m_miningTarget = pos;
    if (m_mining.advance(pos, dig)) {
        m_miningTarget.reset();
        breakBlock(world, player, pos, block, events);
    }
}

void EntitySimulation::breakBlock(world::World& world, physics::Player& player, const core::BlockPos& pos,
                             world::BlockId block, GameTickEvents& events) {
    gameplay::ItemStack held = toItemStack(player.getHeldItem());
    if (block == world::BlockId::TNT && !player.isCreative()) {
        // S6.6: TNT broken in Survival lights instead of dropping (Beta BlockTNT with the
        // ignite bit); the tool wears as on any block.
        if (primeTnt(world, pos) == ecs::NULL_ENTITY) {
            return;
        }
        events.heldWear = gameplay::onBlockBroken(held);
        player.setHeldItem(toInventorySlot(held));
        events.blockBroken = BlockBreakEvent{pos, block, false, ecs::NULL_ENTITY};
        return;
    }
    if (block == world::BlockId::Chest) {
        (void)fillDungeonChest(world, pos); // S6.10: the loot is drawn before it spills out
    }
    const gameplay::BlockBreakOutcome outcome =
        gameplay::breakBlockWith(block, held, m_rng.next(), world.getMetadata(pos)); // Crop stage (S5.7)
    if (!outcome.broken) {
        return;
    }
    world::BlockId replacement = world::BlockId::Air;
    if (outcome.meltsIntoWater) {
        const world::BlockId below = world.getBlock(pos.x, pos.y - 1, pos.z);
        if (world::BlockRegistry::isSolid(below) || world::BlockRegistry::isLiquid(below)) {
            replacement = world::BlockId::Water;
        }
    }
    if (!world.setBlock(pos, replacement)) {
        return; // Unloaded chunk: nothing broke, so nothing wears or drops
    }
    player.setHeldItem(toInventorySlot(held));
    events.heldWear = outcome.toolWear;

    BlockBreakEvent ev{pos, block, false, ecs::NULL_ENTITY};
    if (outcome.drop) {
        ev.drop = spawnBlockDrop(*outcome.drop, pos);
        ev.dropped = ev.drop != ecs::NULL_ENTITY;
    }
    if (outcome.bonusDrop) {
        (void)spawnBlockDrop(*outcome.bonusDrop, pos);
    }
    events.blockBroken = ev;
}

void EntitySimulation::attackEntity(physics::Player& player, ecs::EntityId target, GameTickEvents& events) {
    ecs::HealthComp& health = m_entities.get<ecs::HealthComp>(target);
    const ecs::TransformComp& transform = m_entities.get<ecs::TransformComp>(target);
    ecs::VelocityComp* velocity = m_entities.tryGet<ecs::VelocityComp>(target);

    gameplay::TargetState state{};
    state.health = toHealth(health, m_entities.tryGet<CorpseComp>(target));
    state.position = transform.pos;
    state.velocity = velocity != nullptr ? velocity->linear : glm::vec3(0.0f);

    // A grounded player is not falling, whatever residual velocity the 60 Hz physics left.
    const gameplay::AttackerState attacker{
        .eyePosition = player.getEyePosition(),
        .look = player.getLookDirection(),
        .verticalVelocity = player.isOnGround() || player.isFlying() ? 0.0f : player.getVelocity().y};
    gameplay::ItemStack weapon = toItemStack(player.getHeldItem());
    const gameplay::MeleeResult result = gameplay::performMeleeAttack(attacker, weapon, state);

    health.current = toInt16(state.health.current);
    health.lastDamage = toInt16(state.health.lastDamage);
    health.hurtInvulnerabilityTimer =
        static_cast<float>(state.health.invulnerableTicks) / static_cast<float>(gameplay::kTicksPerSecond);
    if (velocity != nullptr && result.hit.appliesKnockback()) {
        velocity->linear = state.velocity;
        velocity->onGround = velocity->onGround && result.impulse.y <= 0.0f;
    }

    if (result.hit.applied > 0) {
        onMobHurtByPlayer(m_entities, target, m_playerProxy); // the melee event plays MobHurt
    }

    player.setHeldItem(toInventorySlot(weapon));
    events.heldWear = result.weaponWear;
    events.melee = MeleeHitEvent{target, result};
}

// Fixed-tick order from the ECS integration notes: target search -> AI -> skeleton, spider and
// creeper attacks (S6.3) -> movement -> voxel collision -> arrows (S6.2) -> fire -> mob contact hits -> health (corpses, death drops) -> natural
// spawning -> item ageing. Structural changes go through the command buffer.
void EntitySimulation::setPathfinding(bool enabled) {
    m_pathfinding = enabled;
    if (enabled) {
        return;
    }
    std::vector<ecs::EntityId> planned;
    m_entities.view<ecs::MobPathComp>().each([&](ecs::EntityId e, const ecs::MobPathComp&) { planned.push_back(e); });
    for (const ecs::EntityId e : planned) {
        (void)m_entities.remove<ecs::MobPathComp>(e);
    }
    m_pathPlanning.clear();
}

void EntitySimulation::runEntitySystems(world::World& world, physics::Player& player, float timeOfDay,
                                   GameTickEvents& events) {
    const float sun = gameplay::sunBrightness(timeOfDay);
    acquireTargets(m_entities, world, m_playerProxy, player.gameMode(), m_ticks, player.isDead(), sun);
    if (m_pathfinding) {
        // S8.3: every mob follows A* paths, planned before the steering.
        std::vector<ecs::EntityId> unplanned;
        m_entities.view<ecs::MobAIComp>().each([&](ecs::EntityId e, const ecs::MobAIComp&) {
            if (!m_entities.has<ecs::MobPathComp>(e)) {
                unplanned.push_back(e);
            }
        });
        for (const ecs::EntityId e : unplanned) {
            (void)m_entities.emplace<ecs::MobPathComp>(e);
        }
        const ai::WorldNavQuery query(world);
        ecs::PathBudget budget{};
        m_pathPlanning.update(m_entities, query, budget);
        m_lastPathNodes = budget.spent;
    }
    ecs::MobAISystem::update(m_entities, TICK_SECONDS);
    // S6.3 attacks that override the steering: skeletons stop and shoot, spiders leap, lit
    // creepers stand still and explode.
    (void)rangedAttacks(m_entities, world, m_playerProxy, m_mobRng);
    spiderBehaviour(m_entities, world, m_playerProxy, sun, m_mobRng);
    for (const CreeperBlast& blast : tickCreeperFuses(m_entities, world, m_playerProxy)) {
        explode(world, player, blast.centre, blast.power, blast.creeper, events);
        (void)m_entities.destroy(blast.creeper); // Beta setEntityDead: no drops
    }
    ecs::MovementPhysicsSystem::update(m_entities, TICK_SECONDS);
    ecs::VoxelCollisionSystem::update(m_entities, world);
    // S6.2 arrows, on the segment they just flew.
    const ArrowTickResult arrows = tickArrows(m_entities, world, m_playerProxy, [&](const ArrowPlayerHit& hit) {
        const gameplay::HitResult r = damagePlayer(
            player, {.amount = hit.damage, .cause = DamageCause::Projectile, .sourcePos = hit.sourcePos}, events);
        return r.applied > 0 || r.killed;
    });
    for (const ecs::EntityId e : arrows.mobsHurt) {
        if (const ecs::MobKindComp* mob = m_entities.tryGet<ecs::MobKindComp>(e)) {
            events.mobSounds.push_back({mob->kind, MobSoundKind::Hurt, m_entities.get<ecs::TransformComp>(e).pos});
        }
    }

    sunBurn(m_entities, world, timeOfDay, m_mobRng, m_weather);
    for (const ecs::EntityId e : tickBurning(m_entities, world, m_weather)) {
        if (const ecs::MobKindComp* mob = m_entities.tryGet<ecs::MobKindComp>(e)) {
            events.mobSounds.push_back({mob->kind, MobSoundKind::Hurt, m_entities.get<ecs::TransformComp>(e).pos});
        }
    }
    for (const MobMeleeHit& hit : meleeContact(m_entities, world, m_playerProxy)) {
        (void)damagePlayer(player, {.amount = hit.damage, .cause = DamageCause::Mob, .sourcePos = hit.sourcePos},
                           events);
    }

    for (const ecs::EntityId e : ecs::HealthSystem::update(m_entities, TICK_SECONDS)) {
        const CorpseComp* corpse = m_entities.tryGet<CorpseComp>(e);
        const ecs::MobKindComp* mob = m_entities.tryGet<ecs::MobKindComp>(e);
        const int32_t ticks = (corpse != nullptr ? corpse->ticks : 0) + 1;
        const bool burning = corpse != nullptr ? corpse->burning : isBurning(m_entities, e);
        if (corpse == nullptr && mob != nullptr) {
            events.mobSounds.push_back({mob->kind, MobSoundKind::Death, m_entities.get<ecs::TransformComp>(e).pos});
        }
        if (ticks >= gameplay::kDeathAnimationTicks) {
            if (mob != nullptr) {
                (void)spawnMobDrops(mob->kind, m_entities.get<ecs::TransformComp>(e).pos, burning);
            }
            m_commands.destroy(e);
        } else {
            m_commands.emplaceOrReplace(e, CorpseComp{ticks, burning});
            m_commands.remove<ecs::MobAIComp>(e); // The dead do not walk
        }
    }
    if (m_naturalSpawning) {
        m_spawner.tick(world, m_entities, m_commands, player.getPosition(), timeOfDay, player.gameMode(), m_spawnRng,
                       m_weather);
        m_spawner.tickSpawners(world, m_entities, m_commands, player.getPosition(), timeOfDay, m_spawnRng,
                               m_weather); // S6.10
    }
    // Beta Entity.onEntityUpdate kills anything below the void floor; over unloaded chunks
    // the world reads as air, so this is also what ends a fall there.
    m_entities.view<ecs::TransformComp>().each([&](ecs::EntityId e, const ecs::TransformComp& t) {
        if (t.pos.y < OUT_OF_WORLD_Y) {
            m_commands.destroy(e);
        }
    });
    (void)m_commands.flush(m_entities);
    (void)ecs::ItemDropSystem::update(m_entities, TICK_SECONDS);
    ambientMobSounds(m_entities, player.getPosition(), m_mobRng, events.mobSounds);
}

void EntitySimulation::queueExplosion(const glm::vec3& centre, float power, ecs::EntityId source) {
    m_explosionQueue.push_back({centre, power, source});
}

void EntitySimulation::tickPrimedTnt() {
    m_entities.view<ecs::TransformComp, ecs::PrimedTntComp>().each(
        [&](ecs::EntityId e, const ecs::TransformComp& t, ecs::PrimedTntComp& tnt) {
            if (--tnt.fuse > 0) {
                return;
            }
            // Beta EntityTNTPrimed.explode: from the middle of the 0.98 box. The entity is gone
            // by then, so no source: a recycled id must not be spared by a queued blast.
            queueExplosion(t.pos + glm::vec3(0.0f, kPrimedTntSize * 0.5f, 0.0f), kTntPower, ecs::NULL_ENTITY);
            m_commands.destroy(e);
        });
    (void)m_commands.flush(m_entities);
}

void EntitySimulation::runExplosionQueue(world::World& world, physics::Player& player, GameTickEvents& events) {
    const size_t n = std::min(m_explosionQueue.size(), kMaxExplosionsPerTick);
    if (n == 0) {
        return;
    }
    // Explosions queued while these run (none today: chained TNT gets a fuse) wait their turn.
    std::vector<QueuedExplosion> batch(m_explosionQueue.begin(), m_explosionQueue.begin() + static_cast<std::ptrdiff_t>(n));
    m_explosionQueue.erase(m_explosionQueue.begin(), m_explosionQueue.begin() + static_cast<std::ptrdiff_t>(n));
    for (const QueuedExplosion& q : batch) {
        explode(world, player, q.centre, q.power, q.source, events);
    }
}

ecs::EntityId EntitySimulation::primeTnt(world::World& world, const core::BlockPos& cell, int32_t fuse) {
    if (world.getBlock(cell) != world::BlockId::TNT || !world.setBlock(cell, world::BlockId::Air)) {
        return ecs::NULL_ENTITY;
    }
    const ecs::EntityId e = m_entities.create();
    if (e == ecs::NULL_ENTITY) {
        return ecs::NULL_ENTITY;
    }
    const glm::vec3 feet(static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y), static_cast<float>(cell.z) + 0.5f);
    m_entities.emplace<ecs::TransformComp>(e, ecs::TransformComp{feet, feet, 0.0f, 0.0f});
    // Beta EntityTNTPrimed: a random heading for the 0.02 nudge, 0.2 up; gravity 0.04 (half
    // the mobs'), drag 0.98, 0.7 on the ground.
    const float angle = m_rng.nextFloat() * 6.2831853f;
    ecs::VelocityComp v{};
    v.linear = glm::vec3(-std::sin(angle) * kPrimedTntNudgeSpeed, kPrimedTntLaunchSpeed,
                         -std::cos(angle) * kPrimedTntNudgeSpeed);
    v.gravityScale = ecs::EntityFactory::ITEM_GRAVITY_SCALE;
    v.horizontalDrag = ecs::EntityFactory::ITEM_AIR_DRAG;
    v.verticalDrag = ecs::EntityFactory::ITEM_AIR_DRAG;
    v.groundFriction = 7.1334989f; // -20 ln 0.7
    m_entities.emplace<ecs::VelocityComp>(e, v);
    m_entities.emplace<ecs::AABBColliderComp>(e, ecs::AABBColliderComp{kPrimedTntSize, kPrimedTntSize});
    m_entities.emplace<ecs::PrimedTntComp>(e, ecs::PrimedTntComp{std::max(1, fuse)});
    return e;
}

std::optional<ecs::EntityId> EntitySimulation::igniteTnt(world::World& world, physics::Player& player,
                                                    const physics::RaycastHit& target) {
    physics::InventorySlot held = player.getHeldItem();
    if (!target.hit || player.isDead() || held.isEmpty() ||
        gameplay::canonicalize(held.id) != gameplay::items::FlintAndSteel ||
        world.getBlock(target.blockPos) != world::BlockId::TNT) {
        return std::nullopt;
    }
    const ecs::EntityId e = primeTnt(world, target.blockPos);
    if (e == ecs::NULL_ENTITY) {
        return std::nullopt;
    }
    // Beta ItemStack.damageItem: it breaks once its damage passes 64.
    if (++held.damage > kFlintAndSteelMaxDamage) {
        (void)player.consumeHeldItem(1);
    } else {
        player.setHeldItem(held);
    }
    player.triggerHandSwing();
    return e;
}

void EntitySimulation::explode(world::World& world, physics::Player& player, glm::vec3 centre, float power,
                          ecs::EntityId source) {
    explode(world, player, centre, power, source, m_tickEvents != nullptr ? *m_tickEvents : m_pendingEvents);
}

void EntitySimulation::explode(world::World& world, physics::Player& player, glm::vec3 centre, float power,
                          ecs::EntityId source, GameTickEvents& events) {
    if (!(power > 0.0f)) {
        return;
    }
    // doExplosionA: the cells, then the entities while the blocks still shield them.
    const std::vector<core::BlockPos> cells =
        gameplay::explosionBlocks(gameplay::ExplosionInput{centre, power, m_rng.next()},
                                  [&](int32_t x, int32_t y, int32_t z) {
                                      const world::BlockId id = world.getBlock(x, y, z);
                                      return id == world::BlockId::Air ? -1.0f
                                                                       : world::BlockRegistry::get(id).blastResistance;
                                  });
    const auto isSolid = [&](int32_t x, int32_t y, int32_t z) {
        return world::BlockRegistry::isSolid(world.getBlock(x, y, z));
    };
    const float radius = gameplay::kExplosionDamageRadiusFactor * power;
    constexpr float kPerTick = static_cast<float>(gameplay::kTicksPerSecond);
    struct EntityHit {
        ecs::EntityId entity;
        int32_t damage;
        glm::vec3 push;
    };
    std::vector<EntityHit> hits;
    m_entities.view<ecs::TransformComp, ecs::AABBColliderComp>().each(
        [&](ecs::EntityId e, const ecs::TransformComp& t, const ecs::AABBColliderComp& c) {
            if (e == source || m_entities.has<ecs::PlayerTagComp>(e)) {
                return;
            }
            const float distance = glm::length(t.pos - centre);
            if (distance >= radius) {
                return;
            }
            if (m_entities.has<ecs::ItemDropComp>(e)) {
                m_commands.destroy(e); // Beta EntityItem: 5 health, gone at once
                return;
            }
            const float seen = gameplay::exposure(centre, c.boundsAt(t.pos), isSolid);
            const int32_t damage = m_entities.has<ecs::HealthComp>(e) && !m_entities.has<CorpseComp>(e)
                                       ? gameplay::explosionDamage(distance, power, seen)
                                       : 0;
            hits.push_back({e, damage, gameplay::explosionKnockback(centre, t.pos, power, seen) * kPerTick});
        });
    (void)m_commands.flush(m_entities);
    for (const EntityHit& hit : hits) {
        if (hit.damage > 0 && ecs::HealthSystem::applyDamage(m_entities, hit.entity, hit.damage)) {
            if (const ecs::MobKindComp* mob = m_entities.tryGet<ecs::MobKindComp>(hit.entity)) {
                events.mobSounds.push_back(
                    {mob->kind, MobSoundKind::Hurt, m_entities.get<ecs::TransformComp>(hit.entity).pos});
            }
        }
        if (ecs::VelocityComp* v = m_entities.tryGet<ecs::VelocityComp>(hit.entity)) {
            v->linear += hit.push;
            v->onGround = v->onGround && hit.push.y <= 0.0f;
        }
    }
    const float playerDistance = glm::length(player.getPosition() - centre);
    if (playerDistance < radius && !player.isDead()) {
        const float seen = gameplay::exposure(centre, player.getAABB(), isSolid);
        (void)damagePlayer(player,
                           {.amount = gameplay::explosionDamage(playerDistance, power, seen),
                            .cause = DamageCause::Explosion},
                           events);
        if (!player.isCreative()) {
            player.addVelocity(gameplay::explosionKnockback(centre, player.getPosition(), power, seen) * kPerTick);
        }
    }

    // doExplosionB: the blocks go, dropping their item 3 times in 10; TNT lights.
    size_t destroyed = 0;
    std::vector<core::BlockPos> tnt;
    for (const core::BlockPos& p : cells) {
        const world::BlockId id = world.getBlock(p); // Live: a door's other half is already gone
        if (id == world::BlockId::Air) {
            continue;
        }
        if (id == world::BlockId::TNT) {
            tnt.push_back(p);
            continue;
        }
        if (id == world::BlockId::Chest) {
            (void)fillDungeonChest(world, p); // S6.10
        }
        const uint8_t meta = world.getMetadata(p);
        const bool drops = m_rng.nextFloat() < kExplosionDropChance;
        const uint64_t dropSeed = m_rng.next();
        if (!world.setBlock(p, world::BlockId::Air)) {
            continue;
        }
        ++destroyed;
        if (drops) {
            const gameplay::BlockBreakOutcome outcome = gameplay::explosionDrops(id, dropSeed, meta);
            if (outcome.drop) {
                (void)spawnBlockDrop(*outcome.drop, p);
            }
            if (outcome.bonusDrop) {
                (void)spawnBlockDrop(*outcome.bonusDrop, p);
            }
        }
    }
    for (const core::BlockPos& p : tnt) {
        if (primeTnt(world, p, kChainFuseMin + m_rng.nextInt(kChainFuseRandom)) != ecs::NULL_ENTITY) {
            ++destroyed;
        }
    }
    events.explosions.push_back({centre, power, destroyed});
}

size_t EntitySimulation::tickPressurePlates(world::World& world, const physics::Player& player) {
    size_t contacts = 0;
    const auto touch = [&](const physics::AABB& box, bool living) {
        // Plates lie in the cell at the feet (they do not collide).
        const auto y = static_cast<int32_t>(std::floor(box.min.y));
        for (int32_t x = static_cast<int32_t>(std::floor(box.min.x)); x <= static_cast<int32_t>(std::floor(box.max.x)); ++x) {
            for (int32_t z = static_cast<int32_t>(std::floor(box.min.z)); z <= static_cast<int32_t>(std::floor(box.max.z));
                 ++z) {
                const world::BlockId id = world.getBlock(x, y, z);
                if (id != world::BlockId::WoodPressurePlate && (id != world::BlockId::StonePressurePlate || !living)) {
                    continue;
                }
                const auto fx = static_cast<float>(x);
                const auto fy = static_cast<float>(y);
                const auto fz = static_cast<float>(z);
                const physics::AABB trigger{{fx + 0.125f, fy, fz + 0.125f}, {fx + 0.875f, fy + 0.25f, fz + 0.875f}};
                if (box.intersects(trigger)) {
                    (void)world.reportPlateContact({x, y, z});
                    ++contacts;
                }
            }
        }
    };
    if (!player.isDead()) {
        touch(player.getAABB(), true);
    }
    m_entities.view<ecs::TransformComp, ecs::AABBColliderComp>().each(
        [&](ecs::EntityId e, const ecs::TransformComp& t, const ecs::AABBColliderComp& c) {
            if (m_entities.has<ecs::PlayerTagComp>(e)) {
                return; // The player's proxy: the player itself was tested above
            }
            const bool living = m_entities.has<ecs::MobKindComp>(e) && !m_entities.has<CorpseComp>(e);
            touch(c.boundsAt(t.pos), living);
        });
    return contacts;
}

int32_t EntitySimulation::pickUpItems(physics::Player& player) {
    const physics::AABB reach = player.getAABB().expanded({PICKUP_REACH_XZ, 0.0f, PICKUP_REACH_XZ});
    int32_t picked = 0;
    m_entities.view<ecs::TransformComp, ecs::AABBColliderComp, ecs::ItemDropComp>().each(
        [&](ecs::EntityId e, const ecs::TransformComp& t, const ecs::AABBColliderComp& c, ecs::ItemDropComp& item) {
            if (!ecs::ItemDropSystem::canPickUp(item) || item.count <= 0 || !reach.intersects(c.boundsAt(t.pos))) {
                return;
            }
            const int32_t left = player.insertStack({item.id, item.count, item.damage});
            picked += item.count - left;
            if (left == 0) {
                m_commands.destroy(e);
            } else {
                item.count = left;
            }
        });
    (void)m_commands.flush(m_entities);
    return picked;
}

std::optional<uint8_t> placementMetadata(const world::World& world, const physics::Player& player,
                                         world::BlockId block, const core::BlockPos& pos, core::FaceDir clickedFace) {
    switch (block) {
        case world::BlockId::Leaves:
            return world::kLeavesPlayerPlacedBit;
        case world::BlockId::Ladder: {
            // Beta BlockLadder.onBlockPlaced: the clicked wall when it is a normal cube, else
            // the first normal cube around in the order +Z, -Z, +X, -X; no wall, no ladder.
            const auto wallAt = [&](uint8_t meta) {
                const glm::ivec3 d = world::ladderSupportOffset(meta);
                return world::isNormalCube(world.getBlock(pos.x + d.x, pos.y + d.y, pos.z + d.z));
            };
            if (const std::optional<uint8_t> clicked = world::ladderMetaForClickedFace(clickedFace);
                clicked && wallAt(*clicked)) {
                return clicked;
            }
            for (const uint8_t meta : {uint8_t{2}, uint8_t{3}, uint8_t{4}, uint8_t{5}}) {
                if (wallAt(meta)) {
                    return meta;
                }
            }
            return std::nullopt;
        }
        case world::BlockId::WoodStairs:
        case world::BlockId::CobbleStairs: {
            const glm::vec3 look = player.getLookDirection();
            return world::stairsMetaForLook(look.x, look.z);
        }
        // S8.5: a lever on any side or on top of a normal cube, a button on its sides, a
        // redstone torch and the plates on top of one (Beta canPlaceBlockAt).
        case world::BlockId::Lever:
        case world::BlockId::StoneButton: {
            const std::optional<uint8_t> attach = world::attachmentForClickedFace(clickedFace);
            if (!attach || (block == world::BlockId::StoneButton && *attach == world::kAttachFloor)) {
                return std::nullopt;
            }
            const glm::ivec3 d = world::attachmentOffset(*attach);
            if (!world::isNormalCube(world.getBlock(pos.x + d.x, pos.y + d.y, pos.z + d.z))) {
                return std::nullopt;
            }
            return attach;
        }
        case world::BlockId::RedstoneTorchOn:
        case world::BlockId::StonePressurePlate:
        case world::BlockId::WoodPressurePlate:
            if (!world::isNormalCube(world.getBlock(pos.x, pos.y - 1, pos.z))) {
                return std::nullopt;
            }
            return block == world::BlockId::RedstoneTorchOn ? world::kAttachFloor : uint8_t{0};
        case world::BlockId::Piston: {
            const glm::vec3 look = player.getLookDirection();
            return world::pistonMetaForLook(look.x, look.y, look.z);
        }
        default:
            return uint8_t{0};
    }
}

std::optional<world::BlockId> placeHeldBlock(world::World& world, physics::Player& player,
                                             const physics::RaycastHit& target) {
    const physics::InventorySlot held = player.getHeldItem();
    if (!target.hit || held.isEmpty() || !crafting::isPlaceableBlock(held.id)) {
        return std::nullopt;
    }
    const auto block = static_cast<world::BlockId>(held.id);
    // Doors, beds and crops come from their items (ItemUse): a lone half or a crop off
    // farmland is never placed as a block.
    if (world::isDoorBlock(block) || block == world::BlockId::Bed || block == world::BlockId::Crops ||
        block == world::BlockId::RedstoneWire || block == world::BlockId::RedstoneTorchOff) {
        return std::nullopt; // S8.5: wire comes from redstone dust, an unlit torch from a lit one
    }
    const core::BlockPos pos = target.adjacentPos;
    if (pos.y < 0 || pos.y >= world::Chunk::HEIGHT ||
        !world::BlockRegistry::get(world.getBlock(pos)).replaceable) {
        return std::nullopt;
    }
    // A placed block starts at metadata 0; for a fluid poured into flowing fluid of the same
    // kind that turns the cell into a source although its id does not change. Placed leaves
    // carry the bit that keeps them from decaying; ladders and stairs their orientation.
    const std::optional<uint8_t> placedMeta = placementMetadata(world, player, block, pos, target.face);
    if (!placedMeta) {
        return std::nullopt;
    }
    const uint8_t meta = *placedMeta;
    if (world::BlockRegistry::get(block).solid &&
        player.getAABB().intersects(physics::AABB::fromBlock(pos.x, pos.y, pos.z, block, meta))) {
        return std::nullopt;
    }
    if (!world.setBlockWithMetadata(pos.x, pos.y, pos.z, block, meta)) {
        return std::nullopt;
    }
    (void)player.consumeHeldItem(1);
    return block;
}

bool fillDungeonChest(world::World& world, const core::BlockPos& pos) {
    if (world.getBlock(pos) != world::BlockId::Chest || world.getMetadata(pos) != worldgen::kChestLootPendingMeta) {
        return false;
    }
    world::BlockEntity& entity = world.blockEntities().getOrCreate(pos, world::BlockId::Chest);
    if (auto* chest = std::get_if<world::ChestData>(&entity)) {
        chest->slots = worldgen::rollDungeonChestLoot(world.getSeed(), pos);
    }
    world.markBlockEntityChanged(pos);
    (void)world.setMetadata(pos.x, pos.y, pos.z, 0);
    return true;
}

std::optional<core::BlockPos> bedHeadAt(const world::World& world, const core::BlockPos& pos) {
    if (world.getBlock(pos) != world::BlockId::Bed) {
        return std::nullopt;
    }
    const uint8_t meta = world.getMetadata(pos);
    const glm::ivec3 d = world::bedPartnerOffset(meta);
    const core::BlockPos other = pos.offset(d.x, d.y, d.z);
    if (world.getBlock(other) != world::BlockId::Bed ||
        (world.getMetadata(other) & world::kBedHeadBit) == (meta & world::kBedHeadBit)) {
        return std::nullopt;
    }
    return (meta & world::kBedHeadBit) != 0 ? pos : other;
}

std::optional<glm::vec3> bedRespawnPoint(const world::World& world, const core::BlockPos& bedHead) {
    if (bedHeadAt(world, bedHead) != std::optional<core::BlockPos>{bedHead}) {
        return std::nullopt;
    }
    const glm::ivec3 dir = world::bedHeadDirection(world.getMetadata(bedHead));
    // Beta: the 3x3 around the head, then the 3x3 around the foot.
    for (int32_t half = 0; half <= 1; ++half) {
        const int32_t x0 = bedHead.x - dir.x * half - 1;
        const int32_t z0 = bedHead.z - dir.z * half - 1;
        for (int32_t x = x0; x <= x0 + 2; ++x) {
            for (int32_t z = z0; z <= z0 + 2; ++z) {
                if (world::isNormalCube(world.getBlock(x, bedHead.y - 1, z)) &&
                    world.getBlock(x, bedHead.y, z) == world::BlockId::Air &&
                    world.getBlock(x, bedHead.y + 1, z) == world::BlockId::Air) {
                    return glm::vec3(static_cast<float>(x) + 0.5f, static_cast<float>(bedHead.y),
                                     static_cast<float>(z) + 0.5f);
                }
            }
        }
    }
    return std::nullopt;
}

RespawnChoice chooseRespawnPoint(world::World& world, std::optional<core::BlockPos>& bed, const glm::vec3& spawnPoint) {
    if (!bed) {
        return {spawnPoint, false};
    }
    world.updateChunksAround(glm::vec3(static_cast<float>(bed->x), static_cast<float>(bed->y), static_cast<float>(bed->z)), 3, 64);
    if (const std::optional<glm::vec3> nextToBed = bedRespawnPoint(world, *bed)) {
        return {*nextToBed, false};
    }
    bed.reset();
    return {world.findSafeSpawnPosition(), true};
}

glm::vec3 respawnPlayer(world::World& world, physics::Player& player, const glm::vec3& spawnPoint) {
    gameplay::respawn(player.health());
    player.breath().air = gameplay::kMaxAirTicks;
    player.burning().fireTicks = 0;
    world.updateChunksAround(spawnPoint, 3, 64);
    const glm::vec3 feet = world.findStandableAbove(spawnPoint);
    player.setPosition(feet);
    player.resetInterpolationHistory();
    return feet;
}

} // namespace betacraft::game
