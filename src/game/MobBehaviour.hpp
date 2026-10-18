#pragma once

#include "core/Config.hpp"
#include "ecs/Entity.hpp"
#include "ecs/Registry.hpp"
#include "gameplay/MobDefs.hpp"
#include "gameplay/SplitMix64.hpp"
#include "world/Weather.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace betacraft::world { class World; }

namespace betacraft::game {

// Sprint 4 mob rules that need the World or the player but not the EntitySimulation: target
// acquisition, contact attacks, burning in daylight and ambient sounds. EntitySimulation runs them
// on the 20 TPS tick around the ECS systems; the player is seen through its proxy entity
// (ecs::EntityFactory::spawnPlayerProxy).

// Beta EntityMob.findPlayerToAttack: hostiles look for the player within 16 blocks, one
// search every 10 ticks per mob (staggered by entity index so the raycasts spread out).
inline constexpr int32_t kTargetSearchIntervalTicks = 10;
inline constexpr float kTargetSearchRange = 16.0f;
// Beta EntityMob.attackEntity: a hit when closer than 2 blocks with the boxes overlapping on Y.
inline constexpr float kMobMeleeReach = 2.0f;
// Beta EntityLiving.knockBack: 0.4 blocks/tick away from the attacker and 0.4 up.
inline constexpr float kMobKnockbackSpeed = 0.4f * 20.0f; // blocks per second
// A passive mob hit by the player runs away for this long (Beta EntityAnimal fleeingTick).
inline constexpr float kFleeSeconds = 3.0f;
// Daylight sets a burning mob on fire for this many ticks; it loses 1 health every
// kBurnDamageIntervalTicks of it (Beta Entity.onEntityUpdate: fire % 20 == 0).
inline constexpr int32_t kSunBurnTicks = 160;
inline constexpr int32_t kBurnDamageIntervalTicks = 20;
// Beta EntityLiving.onEntityUpdate: a 1 in 80 chance per tick of the living sound.
inline constexpr int32_t kAmbientSoundChance = 80;
inline constexpr float kAmbientSoundRange = 16.0f;

enum class MobSoundKind : uint8_t { Ambient, Hurt, Death };

// A mob sound for the Application to play (S4.9): ambient groans/oinks/baas, hurt and death.
struct MobSoundEvent {
    gameplay::MobKind kind{gameplay::MobKind::Pig};
    MobSoundKind sound{MobSoundKind::Ambient};
    glm::vec3 pos{0.0f};
};

// A contact hit of a hostile on the player (meleeContact); EntitySimulation turns it into
// damagePlayer({damage, DamageCause::Mob, sourcePos}).
struct MobMeleeHit {
    ecs::EntityId mob{ecs::NULL_ENTITY};
    int32_t damage{0};
    glm::vec3 sourcePos{0.0f};
};

// Eye height of a mob of that box height (Beta Entity.getEyeHeight: height * 0.85).
[[nodiscard]] constexpr float mobEyeHeight(float height) noexcept { return height * 0.85f; }

// Hostiles in Idle or Wander switch to Chase the proxy when it is within kTargetSearchRange
// and in plain sight (Raycaster::cast from the mob's eyes to the player's eyes stops no
// closer than the player). Each mob searches on the ticks where
// (tick + entityIndex(e)) % kTargetSearchIntervalTicks == 0. In Creative, with a dead player
// or without a valid proxy, every hostile drops its target at once (MobAISystem then idles).
// S6.3: a mob that is neutralInDaylight (spider) does not look for the player while
// `sunBrightness` (gameplay::sunBrightness) is above kSpiderNeutralBrightness (Beta
// EntitySpider.findPlayerToAttack); the default 0 is night.
void acquireTargets(ecs::Registry& reg, const world::World& world, ecs::EntityId proxy, core::GameMode mode,
                    uint64_t tick = 0, bool playerDead = false, float sunBrightness = 0.0f);

// Counts the attack cooldowns down, then lets every hostile chasing the proxy with a ready
// cooldown hit it when the horizontal distance is below kMobMeleeReach and the boxes overlap
// on Y. Each hit restarts the cooldown (MobAttackComp::COOLDOWN_TICKS). Mobs without a melee
// attack (attackDamage 0: skeleton, creeper) are skipped, cooldown included.
[[nodiscard]] std::vector<MobMeleeHit> meleeContact(ecs::Registry& reg, ecs::EntityId proxy);
// Same, and the mob must also see the player: a block between the mob's eyes and the
// player's eyes (Raycaster::cast) stops the hit, so a zombie cannot hit through a wall
// (Beta EntityCreature attacks only a target it canEntityBeSeen). EntitySimulation uses this one.
[[nodiscard]] std::vector<MobMeleeHit> meleeContact(ecs::Registry& reg, const world::World& world,
                                                    ecs::EntityId proxy);

// Beta EntityZombie.onLivingUpdate: a living mob that burnsInDaylight catches fire
// (BurningComp{kSunBurnTicks}) when sunBrightness > 0.5, the sky light at its eyes is 15,
// its head is out of water and rand(30) < (brightness - 0.4) * 2. An already burning mob is
// topped back up to kSunBurnTicks, keeping its damage rhythm.
void sunBurn(ecs::Registry& reg, const world::World& world, float timeOfDay, gameplay::SplitMix64& rng);
// S8.10: the same under `weather`. A mob standing in the rain (world::isRainingAt at its feet
// or eyes: Beta Entity.isWet) never catches fire.
void sunBurn(ecs::Registry& reg, const world::World& world, float timeOfDay, gameplay::SplitMix64& rng,
             const world::WeatherState& weather);

// One tick of fire: 1 damage (HealthSystem::applyDamage) every kBurnDamageIntervalTicks,
// the fire goes out when it runs down or the mob touches water. Returns the mobs the fire
// hurt this tick.
std::vector<ecs::EntityId> tickBurning(ecs::Registry& reg, const world::World& world);
// S8.10: rain also puts the fire out (Beta Entity.isWet: in water or in the rain).
std::vector<ecs::EntityId> tickBurning(ecs::Registry& reg, const world::World& world,
                                       const world::WeatherState& weather);

// True while `e` has a BurningComp with ticks left.
[[nodiscard]] bool isBurning(const ecs::Registry& reg, ecs::EntityId e);

// --- S6.3 skeleton, spider and creeper (Beta EntitySkeleton / EntitySpider / EntityCreeper) ---

// Spider: Beta EntitySpider.attackEntity leaps between these distances, 1 tick in kSpiderLeapChance.
inline constexpr float kSpiderLeapMinDistance = 2.0f;
inline constexpr float kSpiderLeapMaxDistance = 6.0f;
inline constexpr int32_t kSpiderLeapChance = 10;
inline constexpr float kSpiderLeapSpeed = 0.4f * 20.0f; // blocks per second, horizontal and vertical
// A spider loses its target 1 tick in kSpiderForgetChance while the sun is above this.
inline constexpr float kSpiderNeutralBrightness = 0.5f;
inline constexpr int32_t kSpiderForgetChance = 100;
// Beta EntitySkeleton: the arrow leaves from the eyes, aimed at 0.2 below the player's eyes.
inline constexpr float kArrowAimBelowEyes = 0.2f;

// Mobs with a RangedAttack (skeleton) chasing the proxy: within MobDef::ranged.range and in
// plain sight they stop, face the player and, every ranged.intervalTicks (the MobAttackComp
// cooldown), shoot an arrow (game/Projectiles.hpp) from their eyes towards 0.2 below the
// player's eyes, aimed elevation x horizontal distance higher, at ranged.speed with
// ranged.spread. Run it after MobAISystem. Returns the arrows spawned.
std::vector<ecs::EntityId> rangedAttacks(ecs::Registry& reg, const world::World& world, ecs::EntityId proxy,
                                         gameplay::SplitMix64& rng);

// Spiders (climbsWalls climbing is MobAISystem's): one chasing the proxy, on the ground,
// in sight and between kSpiderLeapMinDistance and kSpiderLeapMaxDistance leaps at it 1 tick
// in kSpiderLeapChance (kSpiderLeapSpeed towards it plus 0.2 of its velocity, and up;
// MobAIComp::leaping). A neutralInDaylight mob drops its target 1 tick in kSpiderForgetChance
// while sunBrightness > kSpiderNeutralBrightness. Run it after MobAISystem.
void spiderBehaviour(ecs::Registry& reg, const world::World& world, ecs::EntityId proxy, float sunBrightness,
                     gameplay::SplitMix64& rng);

// A creeper whose fuse ran out this tick: EntitySimulation explodes it with `power` at its feet and
// removes it without drops.
struct CreeperBlast {
    ecs::EntityId creeper{ecs::NULL_ENTITY};
    glm::vec3 centre{0.0f};
    float power{0.0f};
};

// Living mobs with a FuseComp: chasing the proxy in plain sight, the fuse is lit (dir +1)
// when closer than explode.igniteRange, or still closer than explode.cancelRange once lit;
// otherwise it goes out (dir -1). ticks moves by dir (never below 0) and, at
// explode.fuseTicks, the creeper is reported. A lit creeper stands still. Run it after
// MobAISystem.
[[nodiscard]] std::vector<CreeperBlast> tickCreeperFuses(ecs::Registry& reg, const world::World& world,
                                                         ecs::EntityId proxy);

// A passive mob hit by the player flees from the proxy for kFleeSeconds.
void onMobHurtByPlayer(ecs::Registry& reg, ecs::EntityId mob, ecs::EntityId proxy);

// Living mobs within kAmbientSoundRange of `listener` each make their sound with a 1 in
// kAmbientSoundChance chance.
void ambientMobSounds(ecs::Registry& reg, const glm::vec3& listener, gameplay::SplitMix64& rng,
                      std::vector<MobSoundEvent>& out);

} // namespace betacraft::game
