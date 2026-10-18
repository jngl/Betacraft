#include "game/WorldSession.hpp"

#include "core/Logger.hpp"
#include "game/ItemBridge.hpp"
#include "game/PlayerPersistence.hpp"
#include "game/StarterKit.hpp"
#include "gameplay/Health.hpp"
#include "gameplay/Survival.hpp"
#include "storage/SaveMigration.hpp"

#include <cmath>
#include <system_error>

namespace betacraft::game {

std::unique_ptr<WorldSession> WorldSession::open(OpenOptions options) {
    if (options.folder.empty()) {
        return nullptr;
    }
    std::error_code ec;
    if (options.newWorld && !std::filesystem::exists(options.folder / "level.dat", ec)) {
        std::filesystem::create_directories(options.folder, ec);
        if (ec) {
            core::Logger::error("WorldSession", "Cannot create the world directory ", options.folder.string(), ": ",
                                ec.message());
            return nullptr;
        }
        options.seed = options.newWorld->seed;
    }
    return std::unique_ptr<WorldSession>(new WorldSession(options));
}

WorldSession::WorldSession(const OpenOptions& options)
    : m_folder(options.folder),
      m_features(options.features),
      m_dayLengthSeconds(options.dayLengthSeconds),
      m_world(std::make_unique<world::World>(options.seed, options.folder)) {
    const std::string dir = m_folder.string();
    m_world->setRegionStorageEnabled(m_features.regionStorage);
    m_simulation.setPathfinding(m_features.mobPathfinding); // S8.3, --no-pathfinding
    m_world->setRedstoneEnabled(m_features.redstone);       // S8.7, --no-redstone

    world::WorldLevelData saved{};
    m_levelStatus = m_world->loadLevelDataStatus(saved);
    // S7.4: convert a pre-Sprint 7 chunks/ save before any chunk is read (never in a read-only
    // session: a too new or unreadable level.dat skips it).
    if (m_levelStatus == world::LevelLoadStatus::Ok || m_levelStatus == world::LevelLoadStatus::LegacyV1 ||
        m_levelStatus == world::LevelLoadStatus::Missing) {
        migrateLegacyStorage(options, saved.storageFormat);
    }

    if (m_levelStatus == world::LevelLoadStatus::Ok || m_levelStatus == world::LevelLoadStatus::LegacyV1) {
        m_world->setSeed(saved.seed);
        m_timeOfDay = saved.timeOfDay;
        m_weather = saved.weather; // S8.9: a level.dat without weather keys is clear
        // Position first: setPosition clears the fall distance restorePlayer brings back.
        m_player.setPosition(saved.playerPos);
        m_player.setRotation(saved.playerYaw, saved.playerPitch);
        if (m_levelStatus == world::LevelLoadStatus::Ok) {
            if (saved.player) {
                restorePlayer(m_player, *saved.player, saved.itemsScheme);
            }
            m_spawnPoint = saved.spawnPos ? *saved.spawnPos : m_world->findSafeSpawnPosition();
            m_bedSpawn = saved.bedPos;
        } else {
            // A V1 level.dat has no player data and predates game modes: Creative, old kit.
            m_player.setGameMode(core::GameMode::Creative);
            applyStarterKit(m_player, core::GameMode::Creative);
            m_spawnPoint = m_world->findSafeSpawnPosition();
        }
        m_name = saved.name;
        if (options.newWorld) {
            core::Logger::warn("WorldSession", dir, " already holds a world: it is opened as saved.");
        }
        m_world->updateChunksAround(saved.playerPos, 3, 64);
        core::Logger::info("WorldSession", "Loaded existing world from ", dir);
    } else {
        m_spawnPoint = m_world->findSafeSpawnPosition();
        m_player.setPosition(m_spawnPoint);
        m_world->updateChunksAround(m_spawnPoint, 3, 64);
        if (m_levelStatus == world::LevelLoadStatus::Missing) {
            m_awaitingGameMode = true;
            if (options.newWorld) {
                m_name = options.newWorld->name;
            }
            core::Logger::info("WorldSession", "Generated new Beta 1.7.3 world (seed=", m_world->getSeed(),
                               ") at spawn (", m_spawnPoint.x, ", ", m_spawnPoint.y, ", ", m_spawnPoint.z, ").");
        } else {
            // Too new or unreadable: play, but never overwrite what is on disk (conventions §1.2).
            m_world->setSaveDisabled(true);
            m_player.setGameMode(options.defaultGameMode);
            applyStarterKit(m_player, options.defaultGameMode);
            core::Logger::error("WorldSession", "level.dat in ", dir,
                                m_levelStatus == world::LevelLoadStatus::TooNew ? " is from a newer version"
                                                                                : " is unreadable",
                                ": saving is disabled for this session.");
        }
    }
    // Random ticks replay identically for a given world seed.
    m_randomTickRng = gameplay::SplitMix64(m_world->getSeed() ^ 0x72616E646F6D7469ULL);
    m_weatherRng = gameplay::SplitMix64(m_world->getSeed() ^ 0x7765617468657221ULL);
    if (!m_features.weather) {
        m_weather = world::WeatherState{}; // --no-weather: always clear (and saved clear)
    }
    m_simulation.setWeather(m_weather);
    // Create World chose the mode already: the world starts at once.
    if (m_awaitingGameMode && options.newWorld) {
        startNewWorld(options.newWorld->mode);
    }
}

WorldSession::~WorldSession() noexcept {
    (void)close();
}

void WorldSession::migrateLegacyStorage(const OpenOptions& options, uint32_t storageFormat) {
    if (!m_world->needsStorageMigration(storageFormat)) {
        return;
    }
    const size_t legacyChunks = storage::countLegacyChunks(m_folder);
    core::Logger::info("WorldSession", "Migrating ", legacyChunks, " legacy chunks of ", m_folder.string(),
                       " to region files...");
    if (options.onMigrationProgress) {
        options.onMigrationProgress({legacyChunks, 0.0f});
    }
    const storage::MigrationReport report = m_world->migrateLegacyStorage([&](float fraction) {
        if (options.onMigrationProgress) {
            options.onMigrationProgress({legacyChunks, fraction});
        }
    });
    if (report.completed) {
        core::Logger::info("WorldSession", "Migration of ", m_folder.string(), " finished.");
    } else {
        core::Logger::error("WorldSession", "Migration of ", m_folder.string(),
                            " did not finish; the legacy chunks stay readable and it is retried next launch.");
    }
}

void WorldSession::startNewWorld(core::GameMode mode) {
    if (!m_awaitingGameMode) {
        return;
    }
    m_awaitingGameMode = false;
    m_player.setGameMode(mode);
    applyStarterKit(m_player, mode);
    (void)save(); // The choice survives a crash
}

world::WorldLevelData WorldSession::makeLevelData(std::vector<physics::InventorySlot>* overflow) const {
    const std::vector<physics::InventorySlot> pending = m_crafting.pendingStacks();
    world::WorldLevelData data{};
    data.itemsScheme = world::kCurrentItemsScheme;
    data.seed = m_world->getSeed();
    data.playerPos = m_player.getPosition();
    data.playerYaw = m_player.getYaw();
    data.playerPitch = m_player.getPitch();
    data.timeOfDay = m_timeOfDay;
    data.spawnPos = m_spawnPoint;
    data.bedPos = m_bedSpawn;
    data.player = capturePlayer(m_player, pending, overflow);
    data.storageFormat = m_world->storageFormat();
    data.name = m_name;
    data.weather = m_weather;
    return data;
}

SaveResult WorldSession::save() {
    // A new world quit before its mode was chosen was never played: writing level.dat now
    // would fix its mode to the default and skip the choice and starter kit next launch.
    if (isSaveDisabled() || m_awaitingGameMode) {
        return {};
    }
    // Stacks the grid and cursor hold that a full inventory cannot take would vanish from the
    // save: close the grid first, as Beta does on quit, which throws them in front of the
    // player (and never duplicates them, unlike dropping copies).
    SaveResult result{};
    std::vector<physics::InventorySlot> overflow;
    world::WorldLevelData data = makeLevelData(&overflow);
    if (!overflow.empty()) {
        for (const physics::InventorySlot& stack : m_crafting.close(m_player)) {
            (void)m_simulation.dropFromPlayer(m_player, toItemStack(stack));
        }
        result.closedCrafting = true;
        data = makeLevelData();
    }
    m_world->saveModifiedChunks(data);
    return result;
}

bool WorldSession::close() {
    if (m_closed) {
        return *m_closed;
    }
    (void)save();
    const bool written = m_world->flushStorage();
    m_closed = written;
    return written;
}

void WorldSession::streamAround(int32_t radius) {
    const glm::vec3 feet = m_player.getPosition();
    if (m_features.asyncWorldGen) {
        m_world->requestChunksAround(feet, radius);
        (void)m_world->integrateReadyChunks(world::World::kIntegrationBudget);
    } else {
        m_world->updateChunksAround(feet, radius, 2);
    }
}

void WorldSession::advanceTimeOfDay(float seconds) noexcept {
    if (m_dayLengthSeconds > 1.0f) {
        m_timeOfDay = std::fmod(m_timeOfDay + seconds / m_dayLengthSeconds, 1.0f);
    }
}

void WorldSession::skipTimeIfCreative() noexcept {
    if (m_player.isCreative()) {
        m_timeOfDay = std::fmod(m_timeOfDay + 0.08f, 1.0f);
    }
}

void WorldSession::wakeInBed(const core::BlockPos& bedHead) {
    m_timeOfDay = 0.0f;
    m_bedSpawn = bedHead;
    const glm::vec3 centre = glm::vec3(static_cast<float>(bedHead.x), static_cast<float>(bedHead.y),
                                       static_cast<float>(bedHead.z)) + glm::vec3(0.5f);
    m_spawnPoint = bedRespawnPoint(*m_world, bedHead).value_or(centre);
}

RespawnResult WorldSession::respawn() {
    RespawnResult result{};
    if (m_bedSpawn) {
        world::World& w = *m_world;
        w.updateChunksAround(glm::vec3(static_cast<float>(m_bedSpawn->x), static_cast<float>(m_bedSpawn->y),
                                       static_cast<float>(m_bedSpawn->z)),
                             3, 64);
        if (const std::optional<glm::vec3> nextToBed = bedRespawnPoint(w, *m_bedSpawn)) {
            m_spawnPoint = *nextToBed;
        } else {
            m_bedSpawn.reset();
            m_spawnPoint = w.findSafeSpawnPosition();
            result.bedMissing = true;
        }
    }

    gameplay::respawn(m_player.health());
    m_player.breath().air = gameplay::kMaxAirTicks;
    m_player.burning().fireTicks = 0;
    m_world->updateChunksAround(m_spawnPoint, 3, 64);
    m_player.setPosition(m_world->findStandableAbove(m_spawnPoint));
    m_player.resetInterpolationHistory();
    return result;
}

} // namespace betacraft::game
