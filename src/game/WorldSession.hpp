#pragma once

#include "core/Config.hpp"
#include "core/Types.hpp"
#include "game/EntitySimulation.hpp"
#include "game/InventoryCrafting.hpp"
#include "gameplay/SplitMix64.hpp"
#include "physics/Player.hpp"
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
// Spec #9 delivery step 2: the lifecycle lives here; the Application still runs the game tick
// and steps the state through the accessors below until the tick moves in (ticket #14).
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

    // Streams chunks within `radius` of the player, once per frame: S7.7 disk reads and pass 1
    // off the main thread, then World::kIntegrationBudget of integration (without async
    // generation: 2 chunks in place).
    void streamAround(int32_t radius);

    // Time of day (SkyRenderer: 0 sunrise, 0.25 noon).
    [[nodiscard]] float timeOfDay() const noexcept { return m_timeOfDay; }
    // `seconds` of a day lasting OpenOptions::dayLengthSeconds; 1 s or less freezes it.
    void advanceTimeOfDay(float seconds) noexcept;
    // Creative time skip (+0.08 of a day); nothing outside Creative.
    void skipTimeIfCreative() noexcept;

    // Per-world state the Application's tick steps until the tick moves in (ticket #14).
    [[nodiscard]] world::WeatherState& weather() noexcept { return m_weather; }
    [[nodiscard]] const world::WeatherState& weather() const noexcept { return m_weather; }
    [[nodiscard]] gameplay::SplitMix64& weatherRng() noexcept { return m_weatherRng; }
    [[nodiscard]] gameplay::SplitMix64& randomTickRng() noexcept { return m_randomTickRng; }
    [[nodiscard]] gameplay::SplitMix64& trampleRng() noexcept { return m_trampleRng; }
    // Game ticks run; seeds the low-health heart jitter (Beta updateCounter).
    [[nodiscard]] uint32_t gameTicks() const noexcept { return m_gameTicks; }
    void countGameTick() noexcept { ++m_gameTicks; }
    // S5.8 the night passed in the bed whose head is `bedHead`: morning, and the bed becomes
    // the spawn point.
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
    uint32_t m_gameTicks{0};
    // S8.9 weather, saved in level.dat (weather.*); always clear with --no-weather.
    world::WeatherState m_weather{};
    gameplay::SplitMix64 m_weatherRng{0x5EA7E5ULL};    // Seeded from the world seed
    gameplay::SplitMix64 m_randomTickRng{0};           // S5.1, seeded from the world seed
    gameplay::SplitMix64 m_trampleRng{0x7EA3B1EULL};   // S5.7 farmland trampling rolls
};

} // namespace betacraft::game
