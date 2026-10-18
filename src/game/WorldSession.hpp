#pragma once

#include "core/Config.hpp"
#include "core/Types.hpp"
#include "game/EntitySimulation.hpp"
#include "game/InventoryCrafting.hpp"
#include "gameplay/SplitMix64.hpp"
#include "fluids/FluidSimulator.hpp"
#include "physics/Player.hpp"
#include "redstone/RedstoneEngine.hpp"
#include "world/Weather.hpp"
#include "world/World.hpp"
#include "world/WorldStorage.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace betacraft::game {

// S7.4 progress of a legacy save's migration, reported while WorldSession::open converts it.
struct MigrationProgress {
    size_t legacyChunks{0};
    float fraction{0.0f}; // 0 before the first chunk, 1 when done
};

// What WorldSession::save did besides writing.
struct SaveResult {
    // The crafting grid and cursor stacks did not fit in the inventory: the session closed
    // the grid and threw them in front of the player. The caller closes its inventory screen.
    bool closedCrafting{false};
};

// S5.8 beds (Beta BlockBed / EntityPlayer.sleepInBedAt).
enum class SleepResult : uint8_t {
    Asleep,         // Lying in the bed: the night passes after kSleepTicks
    NotNight,       // Beta "You can only sleep at night"
    MonstersNearby, // Beta "You may not rest now, there are monsters nearby"
    NotABed         // Not a whole bed, or the player is dead
};
// Beta EntityPlayer.sleepTimer: the screen fades out for 100 ticks before the morning.
inline constexpr int32_t kSleepTicks = 100;
// Beta sleepInBedAt: hostile mobs within x/z +-8 and y +-5 of the bed keep the player awake.
inline constexpr int32_t kBedMonsterReachXZ = 8;
inline constexpr int32_t kBedMonsterReachY = 5;

// Daytime as the bed sees it: time of day in [0, 0.5) (SkyRenderer: 0 sunrise, 0.5 sunset).
[[nodiscard]] constexpr bool isDaytime(float timeOfDay) noexcept {
    return timeOfDay >= 0.0f && timeOfDay < 0.5f;
}

// What happened during one WorldSession::tick. Ongoing conditions (weather, time of day,
// sleeping, mining) are read from the session instead.
struct WorldTickEvents {
    GameTickEvents game;                           // The input stage and the entity simulation
    std::vector<redstone::RedstoneEvent> redstone; // Clicks, doors, pistons, burnouts (S8.7)
    std::vector<fluids::FluidEvent> fluids;        // Wash-aways (already dropped) and fizzes
    std::optional<glm::vec3> lightningStrike;      // S8.10 where lightning struck, if it did
};

// What WorldSession::respawn did besides bringing the player back.
struct RespawnResult {
    // S5.8 the bed spawn was gone or obstructed: it is cleared and the player is back at the
    // world spawn. The caller shows Beta's "Your home bed was missing or obstructed".
    bool bedMissing{false};
};

// One open world (glossary: World session), GL-free: the World, the Player, the entity
// simulation and every per-world simulation state (weather, time of day, spawn points, the
// crafting grid, the random-tick and trample RNGs). Destroying the session drops all of it,
// so nothing carries over to the next world.
//
// It owns the world's lifecycle and its game tick; the Application only decides when to tick
// (not in menus, yes while sleeping) and turns the returned events into effects.
class WorldSession : private core::NonCopyable {
public:
    struct OpenOptions {
        std::filesystem::path folder;
        // Seed of a new world opened without newWorld (EngineConfig::worldSeed).
        uint64_t seed{0};
        // A world made on the Create World screen: its name, seed and mode. Ignored when the
        // folder already holds a world. With it, open starts the new world at once.
        std::optional<core::NewWorldParams> newWorld;
        // The flags that affect the world: region storage, redstone, pathfinding, weather,
        // and async generation (on by default; tests turn it off for determinism).
        core::FeatureFlags features{};
        float dayLengthSeconds{1200.0f};
        // Mode and kit of a world whose level.dat is too new or unreadable.
        core::GameMode defaultGameMode{core::GameMode::Survival};
        // Called with fraction 0 before the first legacy chunk, then as the migration goes.
        std::function<void(const MigrationProgress&)> onMigrationProgress;
    };

    // Opens the world saved in `options.folder` (a new one when it holds no level.dat):
    // level.dat, the S7.4 migration of a pre-Sprint 7 save, then the chunks around the player.
    // Every LevelLoadStatus gives a playable world (TooNew and Corrupt with saving disabled);
    // null only when the folder cannot be created.
    [[nodiscard]] static std::unique_ptr<WorldSession> open(OpenOptions options);
    // close() if nobody did. Precondition: no mesh job reads world() any more
    // (WorldRenderer::releaseWorld).
    ~WorldSession() noexcept;

    [[nodiscard]] world::LevelLoadStatus levelStatus() const noexcept { return m_levelStatus; }
    // A new world opened without a mode: nothing is written until startNewWorld.
    [[nodiscard]] bool awaitingGameMode() const noexcept { return m_awaitingGameMode; }
    // Applies `mode` and its starter kit, then writes the first level.dat. No-op unless
    // awaitingGameMode().
    void startNewWorld(core::GameMode mode);
    // level.dat too new or unreadable: never overwritten (World holds the flag).
    [[nodiscard]] bool isSaveDisabled() const noexcept { return m_world->isSaveDisabled(); }

    // level.dat and the modified chunks (World::saveModifiedChunks), unless saving is disabled
    // or the world awaits its mode. Stacks the grid and cursor hold that the inventory cannot
    // take are thrown in front of the player and the grid is closed (closedCrafting).
    SaveResult save();
    // save(), then every queued chunk written (World::flushStorage). False when some chunk
    // could not be written. Later calls do nothing and return the first result.
    bool close();

    [[nodiscard]] world::World& world() noexcept { return *m_world; }
    [[nodiscard]] const world::World& world() const noexcept { return *m_world; }
    [[nodiscard]] physics::Player& player() noexcept { return m_player; }
    [[nodiscard]] const physics::Player& player() const noexcept { return m_player; }
    [[nodiscard]] EntitySimulation& simulation() noexcept { return m_simulation; }
    [[nodiscard]] const EntitySimulation& simulation() const noexcept { return m_simulation; }
    // The inventory screen's 2x2 / 3x3 grid, cursor and opened container.
    [[nodiscard]] InventoryCrafting& crafting() noexcept { return m_crafting; }
    [[nodiscard]] const InventoryCrafting& crafting() const noexcept { return m_crafting; }
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] const core::FeatureFlags& features() const noexcept { return m_features; }

    // One game tick (20 TPS) in Beta 1.7.3's single-player order (Minecraft.runTick):
    //  1. input: mining and attack, and the drops of what they removed;
    //  2. entity phase: entities, TNT, explosions, falling-block landing (drops spawned there
    //     move in this tick), then block entities; the death scatter on the tick the player dies;
    //  3. world phase: weather, sleep (the morning applied on waking in bed), natural
    //     spawning, time of day + 1 tick, World::tick (scheduled ticks, redstone, fluids),
    //     random ticks and the lightning roll;
    //  4. World's queues, in this order: falling-block spawns, unsupported breaks, removed
    //     block entities, fluid wash-away drops. These exist at the end of this tick and first
    //     move in the next one (Beta spawns them during World.tick, after the entities).
    // No clock inside: the caller calls it once per game tick.
    WorldTickEvents tick(const PlayerActions& actions);

    // The 60 Hz player step: movement and collisions, then farmland trampling (S5.7).
    physics::StepEvent stepPlayer(float dt, const physics::PlayerInput& input);

    // Streams chunks within `radius` of the player, once per frame: S7.7 disk reads and pass 1
    // off the main thread, then World::kIntegrationBudget of integration (without async
    // generation: 2 chunks in place).
    void streamAround(int32_t radius);

    // Time of day (SkyRenderer: 0 sunrise, 0.25 noon). tick() advances it by one game tick of
    // a day lasting OpenOptions::dayLengthSeconds; 1 s or less freezes it.
    [[nodiscard]] float timeOfDay() const noexcept { return m_timeOfDay; }
    // Between the previous tick's time and this one's, `alpha` (0..1) of the way: the sky
    // stays smooth between game ticks.
    [[nodiscard]] float timeOfDay(float alpha) const noexcept;
    void setTimeOfDay(float timeOfDay) noexcept { m_timeOfDay = m_prevTimeOfDay = timeOfDay; }
    // Creative time skip (+0.08 of a day); nothing outside Creative.
    void skipTimeIfCreative() noexcept;

    // S8.9 weather, advanced by tick() (clear and frozen with --no-weather).
    [[nodiscard]] world::WeatherState& weather() noexcept { return m_weather; }
    [[nodiscard]] const world::WeatherState& weather() const noexcept { return m_weather; }
    // S8.13 debug weather cycle (clear -> rain -> thunder): Creative only, and never with
    // --no-weather. False when nothing changed.
    bool cycleWeatherIfCreative();
    // Game ticks run; seeds the low-health heart jitter (Beta updateCounter).
    [[nodiscard]] uint32_t gameTicks() const noexcept { return m_gameTicks; }

    // S5.8 right click on a bed (step 2 of the right-click order): at night, with no hostile
    // mob near the bed, the player falls asleep in it; tick() then counts kSleepTicks, applies
    // the morning and the bed spawn, and reports GameTickEvents::wokeInBed. `bed` is either half.
    SleepResult trySleep(const core::BlockPos& bed);
    [[nodiscard]] bool isSleeping() const noexcept { return m_sleepingBed.has_value(); }
    // Head cell of the bed being slept in.
    [[nodiscard]] const std::optional<core::BlockPos>& sleepingBed() const noexcept { return m_sleepingBed; }
    [[nodiscard]] int32_t sleepTicks() const noexcept { return m_sleepTicks; }
    // 0..1 black veil of the sleep screen.
    [[nodiscard]] float sleepFade() const noexcept;
    // Leaves the bed without skipping the night (hurt, bed broken, Esc).
    void wakeUp() noexcept;
    // S5.8 the night passed in the bed whose head is `bedHead`: morning, and the bed becomes
    // the spawn point. tick() calls it when a full night's sleep ends.
    void wakeInBed(const core::BlockPos& bedHead);

    // S1.6 the death screen's Respawn (Beta respawnPlayer): next to the bed spawn when that bed
    // is still there and not blocked, else at the world spawn (World::findSafeSpawnPosition),
    // which becomes the spawn point. Full health and air, no fire, the chunks around the spawn
    // loaded, the feet on the first free cell at or above it, no interpolation from the death
    // place. The inventory (scattered at death), the game mode and the selected slot are left
    // alone. A session opened on a world saved while dead has player().isDead() until then.
    RespawnResult respawn();

    [[nodiscard]] const glm::vec3& spawnPoint() const noexcept { return m_spawnPoint; }
    void setSpawnPoint(const glm::vec3& point) noexcept { m_spawnPoint = point; }
    // S5.8 head of the bed the spawn point is next to.
    [[nodiscard]] const std::optional<core::BlockPos>& bedSpawn() const noexcept { return m_bedSpawn; }
    [[nodiscard]] std::optional<core::BlockPos>& bedSpawn() noexcept { return m_bedSpawn; }

private:
    explicit WorldSession(const OpenOptions& options);
    [[nodiscard]] world::WorldLevelData makeLevelData(std::vector<physics::InventorySlot>* overflow = nullptr) const;
    void migrateLegacyStorage(const OpenOptions& options, uint32_t storageFormat);
    // Beta wakeUpPlayer: a hit, death or the bed going away ends the sleep early; a full
    // night's sleep reports wokeInBed and applies the morning.
    void tickSleep(GameTickEvents& events);
    // Beta onDeath: the grid and cursor stacks go back to the inventory (what does not fit is
    // thrown), then all 40 slots scatter and mining stops.
    void scatterOnDeath();
    // Beta World.tick worldTime + 1: one game tick of the day.
    void tickTimeOfDay() noexcept;
    // S8.10 Beta World.updateLightning: a strike within 64 blocks of the player, where it rains.
    [[nodiscard]] std::optional<glm::vec3> rollLightning();

    std::filesystem::path m_folder;
    core::FeatureFlags m_features;
    float m_dayLengthSeconds{1200.0f};

    std::unique_ptr<world::World> m_world;
    physics::Player m_player;
    EntitySimulation m_simulation;
    InventoryCrafting m_crafting;

    world::LevelLoadStatus m_levelStatus{world::LevelLoadStatus::Missing};
    bool m_awaitingGameMode{false};
    std::optional<bool> m_closed; // close()'s result once it ran

    std::string m_name; // level.dat "name" (S7.8)
    glm::vec3 m_spawnPoint{8.5f, 82.0f, 8.5f};
    std::optional<core::BlockPos> m_bedSpawn;
    float m_timeOfDay{0.22f}; // Morning sunlight
    float m_prevTimeOfDay{0.22f}; // Before the last tick, for timeOfDay(alpha)
    uint32_t m_gameTicks{0};
    // S8.9 weather, saved in level.dat (weather.*); always clear with --no-weather.
    world::WeatherState m_weather{};
    gameplay::SplitMix64 m_weatherRng{0x5EA7E5ULL};    // Seeded from the world seed
    gameplay::SplitMix64 m_randomTickRng{0};           // S5.1, seeded from the world seed
    gameplay::SplitMix64 m_trampleRng{0x7EA3B1EULL};   // S5.7 farmland trampling rolls
    std::optional<core::BlockPos> m_sleepingBed;      // S5.8 head of the bed slept in
    int32_t m_sleepTicks{0};
};

} // namespace betacraft::game
