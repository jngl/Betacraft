#pragma once

#include "core/Types.hpp"
#include "ecs/CommandBuffer.hpp"
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"
#include "fluids/FluidSimulator.hpp"
#include "game/MobBehaviour.hpp"
#include "game/MobSpawner.hpp"
#include "gameplay/Harvesting.hpp"
#include "gameplay/Health.hpp"
#include "gameplay/ItemStack.hpp"
#include "gameplay/MeleeCombat.hpp"
#include "gameplay/MobDefs.hpp"
#include "gameplay/SplitMix64.hpp"
#include "physics/Player.hpp"
#include "physics/Raycaster.hpp"
#include "voxel/Block.hpp"
#include "world/Weather.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace betacraft::world { class World; }

namespace betacraft::game {

// What the player does during one game tick. The Application latches the button-down edge
// between ticks so a click shorter than 50 ms is not lost.
struct PlayerActions {
    bool attackPressed{false}; // Left button went down since the previous tick
    bool attackHeld{false};
    physics::RaycastHit target{}; // Block under the crosshair (hit == false: none in reach)
};

struct BlockBreakEvent {
    core::BlockPos pos{};
    voxel::BlockId block{voxel::BlockId::Air};
    bool dropped{false}; // An item entity was spawned (correct tool / harvest level)
    ecs::EntityId drop{ecs::NULL_ENTITY};
};

struct MeleeHitEvent {
    ecs::EntityId target{ecs::NULL_ENTITY};
    gameplay::MeleeResult result{};
};

// What hurt the player. Grows only at the end (S6.2 arrows hit as Projectile, S6.6 blasts as
// Explosion).
enum class DamageCause : uint8_t { Fall, Drown, Void, Lava, Fire, Cactus, Mob, Suffocation, Projectile, Explosion };

// One hit on the player, the only way to lower its health (EntitySimulation::damagePlayer).
struct PlayerDamage {
    int32_t amount{0};
    DamageCause cause{DamageCause::Fall};
    std::optional<glm::vec3> sourcePos{}; // Attacker position: a full hit knocks the player away (S4.5)
    bool bypassArmor{false};              // Armor (S6.7) ignores this hit: only the void (Beta: falls and drowning go through it)
};

struct PlayerHurtEvent {
    int32_t amount{0}; // Health actually removed
    DamageCause cause{DamageCause::Fall};
    bool killed{false};
};

// S6.6 one explosion that went off (particles and the explosion sound).
struct ExplosionEvent {
    glm::vec3 centre{0.0f};
    float power{0.0f};
    size_t blocksDestroyed{0};
};

// S6.6 explosions and TNT (Beta Explosion / EntityTNTPrimed / BlockTNT).
inline constexpr float kTntPower = 4.0f;
// Beta EntityTNTPrimed: thrown up at 0.2 blocks per tick, a 0.02 random sideways nudge.
inline constexpr float kPrimedTntLaunchSpeed = 0.2f * 20.0f;
inline constexpr float kPrimedTntNudgeSpeed = 0.02f * 20.0f;
inline constexpr float kPrimedTntSize = 0.98f;
// TNT caught in a blast: fuse rand(kChainFuseRandom) + kChainFuseMin (Beta
// BlockTNT.onBlockDestroyedByExplosion).
inline constexpr int32_t kChainFuseMin = 10;
inline constexpr int32_t kChainFuseRandom = 20;
// The last ticks of the fuse flash white (render).
inline constexpr int32_t kTntFlashTicks = 10;
// Beta Explosion.doExplosionB: each destroyed block drops its item with this chance.
inline constexpr float kExplosionDropChance = 0.3f;
// Queued explosions run at most this many per tick, so a wall of TNT cannot freeze a frame.
inline constexpr size_t kMaxExplosionsPerTick = 4;
// Beta ItemFlintAndSteel: 64 uses.
inline constexpr int32_t kFlintAndSteelMaxDamage = gameplay::kFlintAndSteelMaxDamage;

struct GameTickEvents {
    std::optional<BlockBreakEvent> blockBroken;
    std::optional<MeleeHitEvent> melee;
    bool digging{false};  // A held dig tick on a breakable block (dig sound / particles cue)
    gameplay::WearResult heldWear{gameplay::WearResult::Unchanged};
    int32_t itemsPickedUp{0};
    std::vector<PlayerHurtEvent> playerHurt; // Hits that landed this tick, in order
    bool playerDied{false};                  // Set on the tick whose hit killed the player
    std::vector<MobSoundEvent> mobSounds;    // Ambient, hurt and death sounds of mobs (S4.9)
    // S5.8: set on the tick a full night's sleep ends, to the head of the bed slept in. The
    // WorldSession has already applied the morning and the bed spawn.
    std::optional<core::BlockPos> wokeInBed;
    std::vector<ExplosionEvent> explosions; // S6.6, in the order they went off
};

// Dead entities stay targetable for gameplay::kDeathAnimationTicks before they are removed.
struct CorpseComp {
    int32_t ticks{0};
    bool burning{false}; // On fire when it died: a pig then drops cooked porkchop (S4.6)
};

// GL-free entity simulation (glossary: Entity simulation), the part of the WorldSession that
// runs the input stage and the entity phase of each game tick: the ECS registry of non-player
// entities (dropped items, mobs), Beta hold-to-mine with tool requirements and wear, item
// entities and their pickup, melee hits with knockback and hurt frames, natural spawning
// (spawnMobs). physics::Player keeps its own 60 Hz movement.
// Beta sleepInBedAt: hostile mobs within x/z +-8 and y +-5 of the bed keep the player awake.
inline constexpr int32_t kBedMonsterReachXZ = 8;
inline constexpr int32_t kBedMonsterReachY = 5;

class EntitySimulation {
public:
    static constexpr float TICK_SECONDS = 1.0f / static_cast<float>(gameplay::kTicksPerSecond);
    // Beta EntityPlayer.onLivingUpdate collects items in its box grown by 1 block on X/Z.
    static constexpr float PICKUP_REACH_XZ = 1.0f;
    // Beta Block.dropBlockAsItem: items appear within the inner 0.7 of the broken cell.
    static constexpr float DROP_SPREAD = 0.7f;
    static constexpr float OUT_OF_WORLD_Y = -64.0f;

    explicit EntitySimulation(uint64_t seed = 0x5EED5EEDULL);

    [[nodiscard]] ecs::Registry& entities() noexcept { return m_entities; }
    [[nodiscard]] const ecs::Registry& entities() const noexcept { return m_entities; }
    [[nodiscard]] const gameplay::MiningProgress& mining() const noexcept { return m_mining; }
    // Crack overlay stage for the renderer: 0..9, -1 when nothing is being mined.
    [[nodiscard]] int32_t crackStage() const noexcept { return m_mining.crackStage(); }
    [[nodiscard]] const std::optional<core::BlockPos>& miningTarget() const noexcept { return m_miningTarget; }

    // The input stage and the entity phase of one game tick (Beta 1.7.3 Minecraft.runTick up
    // to World.updateEntities). Input: the player's proxy entity follows the player, the
    // player's survival rules (fall, breath, void, health timers), the player's attack (melee
    // or mining), then the drops of the blocks and block entities those edits removed. Entity
    // phase: the entity systems (target search, AI, skeleton shots, spider leaps and creeper
    // fuses (S6.3), movement, voxel collision, arrows (S6.2), sun burning, mob contact hits,
    // health/corpses/death drops, item ageing), the falling blocks, the pressure plates, the
    // primed TNT fuses and up to kMaxExplosionsPerTick queued explosions (S6.6), the drops of
    // what they removed, and item pickup; drops spawned after the movement still move in this
    // tick (Beta updates entities added during the loop). Block entities run last. A dead
    // player neither attacks nor picks anything up. `timeOfDay` follows SkyRenderer (0.25 =
    // noon, the default). S8.10 `weather`: mobs in the rain do not burn and lose their fire
    // (clear by default). Natural spawning is spawnMobs, called after.
    GameTickEvents tick(world::World& world, physics::Player& player, const PlayerActions& actions,
                        float timeOfDay = 0.25f, const storage::WeatherState& weather = {});

    // Natural mob spawning and despawning (MobSpawner) and the dungeon spawners (S6.10), when
    // naturalSpawning() is on. S8.10 rain and thunder darken spawning, and a thunderstorm lets
    // hostiles spawn by day. The WorldSession calls it once per game tick, after tick().
    void spawnMobs(world::World& world, const physics::Player& player, float timeOfDay,
                   const storage::WeatherState& weather);

    // The single entry point for damage to the player (fall, drowning, void, and later lava,
    // mobs, explosions). Nothing happens in Creative or to a dead player; otherwise
    // gameplay::applyDamage runs (hurt-resistance window included), a PlayerHurtEvent is
    // recorded when health was removed, and playerDied is set on the killing hit. A full hit
    // also starts the hurt camera roll (Player::onHurt). Unless `bypassArmor`, the landing
    // damage wears the worn armor and is reduced by it (S6.7, gameplay::absorbDamage).
    gameplay::HitResult damagePlayer(physics::Player& player, const PlayerDamage& damage, GameTickEvents& events);

    // S6.6 Beta Explosion.doExplosionA/B at `centre` with `power` (TNT 4, creeper 3): the
    // gameplay::explosionBlocks cells turn to air, each dropping its item with
    // kExplosionDropChance (a door or bed once); TNT in them becomes primed TNT with a
    // kChainFuseMin + rand(kChainFuseRandom) fuse. Within 2 x power, mobs take
    // gameplay::explosionDamage (HealthSystem::applyDamage), the player the same through
    // damagePlayer (DamageCause::Explosion), and both are pushed away; dropped items there are
    // destroyed. `source` (the TNT or the creeper) is left alone. An ExplosionEvent is
    // recorded. Called outside tick(), the hits and the event go to the next tick's events.
    void explode(world::World& world, physics::Player& player, glm::vec3 centre, float power, ecs::EntityId source);
    void explode(world::World& world, physics::Player& player, glm::vec3 centre, float power, ecs::EntityId source,
                 GameTickEvents& events);
    // Queues an explosion for tick(), which runs at most kMaxExplosionsPerTick of them, oldest
    // first; the rest wait for the next ticks.
    void queueExplosion(const glm::vec3& centre, float power, ecs::EntityId source = ecs::NULL_ENTITY);
    [[nodiscard]] size_t pendingExplosions() const noexcept { return m_explosionQueue.size(); }

    // S6.6 turns the TNT block at `cell` into a primed TNT entity (Beta EntityTNTPrimed: thrown
    // up at kPrimedTntLaunchSpeed) with `fuse` ticks. Returns NULL_ENTITY when `cell` holds no
    // TNT (it is left as is) or the registry is full.
    ecs::EntityId primeTnt(world::World& world, const core::BlockPos& cell,
                           int32_t fuse = ecs::PrimedTntComp::DEFAULT_FUSE_TICKS);
    // S6.6 right click with flint and steel (259) on TNT (step 3 of the right-click order):
    // primes it and wears the flint and steel by 1 (64 uses). Returns the primed entity,
    // nullopt when the held item or the target does not fit.
    std::optional<ecs::EntityId> igniteTnt(world::World& world, physics::Player& player,
                                           const physics::RaycastHit& target);

    // S5.8 Beta sleepInBedAt: a living hostile mob within x/z +-kBedMonsterReachXZ and y
    // -kBedMonsterReachY..+kBedMonsterReachY+1 of the bed whose head is at `bedHead`.
    [[nodiscard]] bool hostileMobNearBed(const core::BlockPos& bedHead);

    // Stops mining (button released, screen opened): drops progress and the hit delay.
    void resetMining() noexcept;

    // The player's stand-in that mobs target (ecs::EntityFactory::spawnPlayerProxy); tick()
    // creates it and keeps it on the player's feet. NULL_ENTITY before the first tick.
    [[nodiscard]] ecs::EntityId playerProxy() const noexcept { return m_playerProxy; }
    // Copies the player's position into the proxy, recreating it when it is no longer valid.
    void syncPlayerProxy(const physics::Player& player);

    [[nodiscard]] const MobSpawner& spawner() const noexcept { return m_spawner; }
    // spawnMobs does nothing while off; on by default.
    void setNaturalSpawning(bool enabled) noexcept { m_naturalSpawning = enabled; }
    [[nodiscard]] bool naturalSpawning() const noexcept { return m_naturalSpawning; }

    // S8.3 A* path following for mobs (FeatureFlags::mobPathfinding, --no-pathfinding); on by
    // default. Off: mobs lose their MobPathComp and steer in straight lines.
    void setPathfinding(bool enabled);
    [[nodiscard]] bool pathfinding() const noexcept { return m_pathfinding; }
    [[nodiscard]] const ecs::PathPlanningSystem& pathPlanning() const noexcept { return m_pathPlanning; }
    // A* nodes expanded by the last tick's planning (F3 tick costs, Sprint 8 risks).
    [[nodiscard]] int32_t lastPathNodes() const noexcept { return m_lastPathNodes; }

    // Death drops of a mob (Beta EntityLiving.dropFewItems), quantities rolled with the
    // session's rng; a pig that was burning drops cooked porkchop. Returns the number of
    // entities spawned.
    size_t spawnMobDrops(gameplay::MobKind kind, const glm::vec3& pos, bool burning);

    // Spawns a dropped item entity (Beta EntityItem). Returns NULL_ENTITY for empty stacks
    // or a full registry.
    ecs::EntityId spawnItem(const gameplay::ItemStack& stack, const glm::vec3& pos,
                            const glm::vec3& velocity = glm::vec3(0.0f));
    // Spawns `stack` at the cell as Beta does for a broken block: random spot in the inner
    // DROP_SPREAD of the cell, small random pop.
    ecs::EntityId spawnBlockDrop(const gameplay::ItemStack& stack, const core::BlockPos& cell);

    // Spawns the drops of blocks flowing water destroyed (fluids BlockWashedAway events, Beta
    // dropBlockAsItem); other events are ignored. Returns the number of entities spawned.
    size_t spawnFluidDrops(std::span<const fluids::FluidEvent> events);
    // Spawns the drops of the flora and torches World broke because their support went away
    // (World::takeUnsupportedBreaks); tick() runs it after the player's attack and after the
    // explosions, the WorldSession after the world phase.
    size_t spawnUnsupportedDrops(world::World& world);
    // S5.3: spawns the falling sand and gravel entities World's scheduled updates released
    // (World::takeFallingBlockSpawns); the WorldSession runs it right after the world phase.
    // Returns the number of entities spawned.
    size_t spawnFallingBlocks(world::World& world);
    // spawnFallingBlocks (for what nobody spawned yet), then settles the falling blocks that
    // landed: back to a block in a replaceable cell, otherwise (or after kFallingBlockMaxTicks
    // in flight) an item through spawnBlockDrop. tick() runs it after the entity systems
    // moved them.
    void tickFallingBlocks(world::World& world);

    // S8.7 pressure plates (Beta BlockPressurePlate.onEntityCollidedWithBlock): every plate
    // whose trigger box (inset 1/8, 1/4 high) the player or a mob touches is reported to
    // World::reportPlateContact; a wooden plate also reacts to items, arrows, falling blocks
    // and primed TNT. tick() runs it after the entities moved. Returns the contacts reported.
    size_t tickPressurePlates(world::World& world, const physics::Player& player);

    // Spawns the contents of the chests and furnaces whose block went away
    // (World::takeRemovedBlockEntities), one entity per non-empty slot; tick() runs it with
    // spawnUnsupportedDrops, and so does the WorldSession after the world phase.
    size_t spawnRemovedBlockEntityDrops(world::World& world);

    // One tick of every resident furnace (crafting::tickFurnace): the block swaps between
    // Furnace and LitFurnace when it lights or goes out (the entity is kept), and a changed
    // furnace marks its chunk for saving. tick() runs it last, after every entity.
    void tickBlockEntities(world::World& world);

    // Restarts the drop, spawn and mob random streams from `seed` (the WorldSession seeds
    // them from the world seed).
    void reseed(uint64_t seed) noexcept;

    // Right click with the held item in the void (step 4 of the right-click order): eats
    // food at once (Beta ItemFood), even at full health; mushroom stew leaves its bowl.
    // Returns the health actually restored, nullopt when nothing was used (not food, or a
    // dead player).
    std::optional<int32_t> useHeldItem(physics::Player& player);

    // Throws `stack` from 0.3 below the player's eyes along the look (Beta dropPlayerItem),
    // collectable only after gameplay::kDeathDropPickupDelayTicks.
    ecs::EntityId dropFromPlayer(const physics::Player& player, const gameplay::ItemStack& stack);
    // dropFromPlayer for each of `stacks` (a closed grid, a cursor, what did not fit).
    void dropAllFromPlayer(const physics::Player& player, std::span<const physics::InventorySlot> stacks);

    // Q / Ctrl+Q (Beta EntityPlayer.dropCurrentItem): takes one item, or the whole stack, off
    // the selected hotbar slot, wear kept, and throws it with dropFromPlayer. Returns the
    // entity, NULL_ENTITY when the hand is empty, the player is dead or the registry is full
    // (the item then stays in the hand).
    ecs::EntityId dropHeldItem(physics::Player& player, bool wholeStack);

    // Death (Beta dropPlayerItemWithRandomChoice for every slot): scatters the 36 inventory
    // stacks and the 4 armor pieces from the eyes (gameplay::scatterInventoryOnDeath), each
    // collectable after its pickup delay, and empties the inventory and the armor. Close the inventory screen first so the grid
    // and the cursor stack are scattered too. Returns the number of entities spawned.
    size_t dropInventoryOnDeath(physics::Player& player);

    // S6.3 right click on the entity under the crosshair (step 1 of the right-click order,
    // `target` from meleeTarget): an empty bucket on a living cow becomes a milk bucket (Beta
    // EntityCow.interact). Returns false when nothing happened.
    bool useHeldItemOnEntity(physics::Player& player, ecs::EntityId target);

    // Entity the player's look ray hits first, closer than the melee reach and the first
    // solid block in the way (`blockTarget`).
    [[nodiscard]] std::optional<ecs::EntityId> meleeTarget(const physics::Player& player,
                                                           const physics::RaycastHit& blockTarget);

private:
    void tickPlayerSurvival(world::World& world, physics::Player& player, GameTickEvents& events);
    void handleAttack(world::World& world, physics::Player& player, const PlayerActions& actions,
                      GameTickEvents& events);
    void breakBlock(world::World& world, physics::Player& player, const core::BlockPos& pos,
                    voxel::BlockId block, GameTickEvents& events);
    void attackEntity(physics::Player& player, ecs::EntityId target, GameTickEvents& events);
    bool spawnNaturalDrop(voxel::BlockId block, const core::BlockPos& cell, uint8_t meta = 0);
    void runEntitySystems(world::World& world, physics::Player& player, float timeOfDay,
                          const storage::WeatherState& weather, GameTickEvents& events);
    void tickPrimedTnt();
    void runExplosionQueue(world::World& world, physics::Player& player, GameTickEvents& events);
    // Moves the drops spawned after this tick's movement (m_lateDrops) by one tick.
    void moveLateDrops(world::World& world);
    int32_t pickUpItems(physics::Player& player);

    ecs::Registry m_entities;
    ecs::CommandBuffer m_commands;
    gameplay::MiningProgress m_mining;
    std::optional<core::BlockPos> m_miningTarget;
    gameplay::SplitMix64 m_rng; // Drop rolls and drop scatter, reproducible per seed
    // Separate streams so mob spawning, sun burning and ambient sounds never shift the drop
    // rolls above.
    gameplay::SplitMix64 m_spawnRng;
    gameplay::SplitMix64 m_mobRng;
    MobSpawner m_spawner;
    ecs::EntityId m_playerProxy{ecs::NULL_ENTITY};
    bool m_naturalSpawning{true};
    bool m_pathfinding{true};
    ecs::PathPlanningSystem m_pathPlanning;
    int32_t m_lastPathNodes{0};
    uint64_t m_ticks{0};
    struct QueuedExplosion {
        glm::vec3 centre{0.0f};
        float power{0.0f};
        ecs::EntityId source{ecs::NULL_ENTITY};
    };
    std::vector<QueuedExplosion> m_explosionQueue;
    // Set once tick()'s movement ran: spawnItem then records the drop in m_lateDrops.
    bool m_movedThisTick{false};
    std::vector<ecs::EntityId> m_lateDrops;
    // Where explode() records hits and events: the running tick's, or m_pendingEvents
    // (handed to the next tick) when called from outside tick().
    GameTickEvents* m_tickEvents{nullptr};
    GameTickEvents m_pendingEvents;
};

// Metadata a block placed by the player at `pos` starts with (S5.4): the player bit of leaves,
// the wall side of a ladder (nullopt when no normal cube can hold it), the orientation of
// stairs from the look direction, 0 otherwise.
std::optional<uint8_t> placementMetadata(const world::World& world, const physics::Player& player,
                                         voxel::BlockId block, const core::BlockPos& pos, core::FaceDir clickedFace);

// Right-click placement of the held block at `target.adjacentPos` (Beta ItemBlock.onItemUse):
// the cell must be replaceable (air, liquids, tall grass) and a solid block must not overlap
// the player. Consumes one item and returns the placed block, nullopt when nothing changed.
std::optional<voxel::BlockId> placeHeldBlock(world::World& world, physics::Player& player,
                                             const physics::RaycastHit& target);

// S6.10: a dungeon chest (Chest with worldgen::kChestLootPendingMeta) draws its loot on first
// access (worldgen::rollDungeonChestLoot, seeded by the world seed and `pos`): the slots go to
// the BlockEntityStore and the metadata drops to 0. Returns false for anything else (an
// ordinary or already opened chest is left alone). Opening, breaking and blasting a chest
// call it first.
bool fillDungeonChest(world::World& world, const core::BlockPos& pos);

// S5.8: the head cell of the bed one of whose halves is at `pos`; nullopt when `pos` is not
// part of a whole bed.
[[nodiscard]] std::optional<core::BlockPos> bedHeadAt(const world::World& world, const core::BlockPos& pos);

// Beta BlockBed.getNearestEmptyChunkCoordinates: feet position (cell centre) of the first cell
// around the bed whose head is at `bedHead` with a normal cube under it and air in it and
// above it; nullopt when the bed is gone or every such cell is blocked.
[[nodiscard]] std::optional<glm::vec3> bedRespawnPoint(const world::World& world, const core::BlockPos& bedHead);

} // namespace betacraft::game
