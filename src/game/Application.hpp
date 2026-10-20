#pragma once

#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Types.hpp"
#include "game/AcousticsController.hpp"
#include "game/DynamicLightCollector.hpp"
#include "game/EntitySimulation.hpp"
#include "game/ItemUse.hpp"
#include "game/WorldSession.hpp"
#include "lighting/DynamicLightManager.hpp"
#include "physics/Player.hpp"
#include "physics/Raycaster.hpp"
#include "render/ParticleSystem.hpp"
#include "render/TextRenderer.hpp"
#include "render/WorldRenderer.hpp"
#include "ui/CreateWorldScreen.hpp"
#include "ui/DeathScreen.hpp"
#include "ui/HudRenderer.hpp"
#include "ui/InputManager.hpp"
#include "ui/OptionsMenu.hpp"
#include "ui/ScreenState.hpp"
#include "ui/TitleScreen.hpp"
#include "ui/WorldSelectScreen.hpp"
#include "world/World.hpp"

#include <SDL2/SDL.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace betacraft::game {

class Application : private core::NonCopyable {
public:
    explicit Application(core::EngineConfig config = {});
    ~Application() noexcept;

    // Window, GL and renderers; with EngineConfig::autoEnterWorld also enterWorld(worldSaveDir),
    // otherwise the title screen starts with no world.
    [[nodiscard]] bool initialize();
    // S7.9 opens the world saved in `dir` (leaving the current one first) through
    // WorldSession::open, with a progress bar on the Loading screen while a pre-Sprint 7 save
    // beyond storage::kMigrationProgressThreshold chunks is converted. A directory without
    // level.dat is a new world: `params` gives its seed, mode and name (nullopt:
    // EngineConfig::worldSeed and the title screen's mode); `params` is ignored for an
    // existing world. Ends on the title screen with the world loaded: enterGame (run,
    // runFixedFrames, "Singleplayer") goes in. False when the directory cannot be created.
    bool enterWorld(const std::filesystem::path& dir, std::optional<core::NewWorldParams> params = std::nullopt);
    // S7.9 back to the title screen with no world: stops the meshing, closes the world
    // session (saved, every queued chunk written) and drops it with the per-world effects.
    // No-op without a world.
    void leaveWorld();
    [[nodiscard]] bool hasWorld() const noexcept { return m_session != nullptr; }
    int run(int maxFrames = -1);
    // Deterministic frame loop for tests and headless runs: in game with the mouse captured,
    // each frame advancing exactly `frameDt` seconds (0.05 = one 20 TPS game tick). Frames
    // are rendered but not presented: nobody watches them, and on XWayland the swap of a
    // second window created in the same process can block for seconds.
    int runFixedFrames(int frames, float frameDt);
    void shutdown() noexcept;

    [[nodiscard]] const core::EngineConfig& getConfig() const noexcept { return m_config; }
    void syncRuntimeOptions();

    [[nodiscard]] ui::ScreenState getScreenState() const noexcept { return m_screenState; }
    void setScreenState(ui::ScreenState state) noexcept { m_screenState = state; }

    [[nodiscard]] ui::InputManager& getInputManager() noexcept { return m_inputManager; }
    [[nodiscard]] const ui::InputManager& getInputManager() const noexcept { return m_inputManager; }

    // Null while no world is open (title screen without autoEnterWorld, after leaveWorld).
    [[nodiscard]] world::World* getWorld() noexcept { return m_session ? &m_session->world() : nullptr; }
    [[nodiscard]] WorldSession* getWorldSession() noexcept { return m_session.get(); }
    // Precondition for both: a world is open (hasWorld()).
    [[nodiscard]] physics::Player& getPlayer() noexcept { return m_session->player(); }
    [[nodiscard]] EntitySimulation& getSimulation() noexcept { return m_session->simulation(); }

    // Death screen "Respawn": full health and air at the spawn point, back in game.
    void respawnPlayer();
    // Precondition for both: a world is open (hasWorld()).
    [[nodiscard]] const std::optional<core::BlockPos>& getBedSpawn() const noexcept { return m_session->bedSpawn(); }
    [[nodiscard]] const glm::vec3& getSpawnPoint() const noexcept { return m_session->spawnPoint(); }
    [[nodiscard]] const std::string& getStatusMessage() const noexcept { return m_inWorld.state.statusMessage; }
    // The open world's level.dat is too new or unreadable (WorldSession::isSaveDisabled).
    [[nodiscard]] bool isSaveDisabled() const noexcept { return m_session && m_session->isSaveDisabled(); }

    // S3.7 GUI Scale: TextRenderer::calculateGuiScale(w, h, pref) / 3 for the current window
    // and preference (read every frame); 1.0 for Auto at 1280x720.
    [[nodiscard]] float hudScale() const noexcept;
    // The virtual canvas the HUD, the inventory and the death screen are laid out in.
    [[nodiscard]] ui::HudCanvas hudCanvas() const noexcept;

private:
    // Leaves the title for the world: a new world gets its mode and starter kit first, and a
    // dead player lands on the death screen.
    void enterGame();
    // S7.8 menu flow: the World Select list (refreshed from savesRoot), the Create World form,
    // and entering the chosen or new world.
    void openWorldSelect();
    void openCreateWorld();
    void playSelectedWorld();
    void createWorldFromScreen();
    // One Loading screen frame, presented at once (enterWorld blocks the frame loop).
    void presentLoadingFrame(std::string_view title, std::string_view status, std::optional<float> progress);
    // Title / menu screens drawn without a world behind them.
    void clearFrame(int framebufferWidth, int framebufferHeight);
    // WorldSession::save; closes the inventory screen when the session closed the grid.
    void saveWorld();
    // The open world session's parts (precondition: m_session).
    [[nodiscard]] world::World& world() noexcept { return m_session->world(); }
    [[nodiscard]] const world::World& world() const noexcept { return m_session->world(); }
    [[nodiscard]] physics::Player& player() noexcept { return m_session->player(); }
    [[nodiscard]] const physics::Player& player() const noexcept { return m_session->player(); }
    [[nodiscard]] EntitySimulation& sim() noexcept { return m_session->simulation(); }
    [[nodiscard]] const EntitySimulation& sim() const noexcept { return m_session->simulation(); }
    // events.playerDied (the session already scattered everything): the inventory screen and
    // the input latches let go, then the death screen.
    void handlePlayerDeath();
    // One frame (events, update, render); false once the application was asked to quit.
    bool runFrame(float dt, bool present);
    void processEvents();
    void update(float dt);
    void fixedUpdate(float fixedDt);
    void gameTick();
    void renderFrame(bool present);
    // The 3D world with its entities and particles (a world is open).
    void renderWorldFrame(int framebufferWidth, int framebufferHeight);
    // The HUD or the screen of the current state, over whatever was drawn.
    void renderScreens();
    // Fills m_mobInstances from the session's mobs, interpolated with the game-tick alpha.
    void collectMobInstances(float tickAlpha);

    void handleRightClickAction();
    // S5.6-S5.8 step 2 of the right-click order for doors and beds; true when the click was used.
    // Hand swing and sound of a step 3/4 item use (door, bed, hoe, seeds, bucket).
    void playItemUseEffects(const ItemUseResult& used);
    bool activateDoorOrBed();
    void playRedstoneEffects(const std::vector<redstone::RedstoneEvent>& events);
    // S8.10-S8.12 after each game tick: the flash, bolt and delayed thunder of `strike`, rain
    // splashes around the player, and the rain loop's gain and occlusion.
    void tickWeatherEffects(const std::optional<glm::vec3>& strike);
    // F3 line of the weather state.
    [[nodiscard]] std::string weatherDebugLine() const;
    void showStatusMessage(std::string text);
    void handleInventoryClick(int32_t mouseX, int32_t mouseY, SlotButton button);
    // Opens the inventory screen on a 2x2 (inventory) or 3x3 (workbench) grid, or closes it;
    // whatever the inventory cannot take back is thrown in front of the player.
    void openInventoryScreen(crafting::GridSize grid);
    // Furnace or chest screen on the block entity at `pos` (created on first open).
    void openContainerScreen(ScreenKind kind, const core::BlockPos& pos);
    // gameTick: an open container screen closes beyond 8 blocks (Beta canInteractWith) or
    // once its block entity is gone (broken, chunk unloaded).
    void closeContainerIfOutOfReach();
    void closeInventoryScreen();
    void dropAtPlayer(const std::vector<physics::InventorySlot>& stacks);
    void playGameTickEffects(const GameTickEvents& events);
    void setMouseCaptured(bool captured) noexcept;
    [[nodiscard]] int32_t streamingRadius() const noexcept;
    // Plays a world sound at `source` with occlusion and reverb: `play(const audio::VoiceAcoustics&)`.
    template <typename PlayFn>
    void playWorldSound(const glm::vec3& source, PlayFn&& play) {
        (void)m_inWorld.acoustics.playAt(source, world(), std::forward<PlayFn>(play));
    }

    // Each mob's limb swing: the phase advances with the distance its render position covered
    // since the previous frame (Beta limbSwing).
    struct MobWalkState {
        float phase{0.0f};
        glm::vec3 lastPos{0.0f};
    };
    // S8.10 a thunder clap on its way (speed of sound).
    struct PendingThunder {
        int32_t ticks{0};
        float volume{1.0f};
    };
    static constexpr int32_t kLightningFlashTicks = 6;

    // InWorldEffects::state: everything of it but the acoustics, rebuilt with {}.
    struct InWorldPresentation {
        render::ParticleSystem particles;
        lighting::DynamicLightManager dynamicLights;
        float totalTime{0.0f};
        // Fixed-timestep accumulators: player step and 20 TPS game tick.
        float accumulator{0.0f};
        float gameTickAccumulator{0.0f};

        // Instances drawn this frame, and the mobs' limb swing.
        std::vector<render::ItemEntityInstance> itemInstances;
        std::vector<render::FallingBlockInstance> fallingBlockInstances;
        std::vector<render::ArrowRenderInstance> arrowInstances; // S6.2
        std::vector<render::MobRenderInstance> mobInstances;
        std::unordered_map<ecs::EntityId, MobWalkState> mobWalk;
        std::unordered_map<ecs::EntityId, MobWalkState> mobWalkNext;

        // S8.10 lightning: the thunder claps on their way, and the last bolt, drawn with the
        // sky flash for kLightningFlashTicks.
        std::vector<PendingThunder> pendingThunder;
        std::optional<glm::vec3> lightningBolt;
        uint32_t lightningSeed{0};
        int32_t lightningTicks{0};

        int32_t digTicks{0};
        float placeCooldown{0.0f};
        // S5.8 one-line notices above the hotbar (bed refusals, missing bed), shown for a while.
        std::string statusMessage;
        float statusMessageSeconds{0.0f};

        bool leftMouseDown{false};
        bool attackPressed{false}; // Latched until the next game tick consumes it
        physics::RaycastHit currentTarget{};

        render::RenderStats lastRenderStats{};
    };
    // Application's per-world presentation state: effects, render instances, timers, input
    // latches. Nothing of the world's simulation. leaveWorld calls reset() once, so the next
    // world inherits none of it. The acoustics hold the audio engine and are reset in place;
    // everything else is rebuilt with {}.
    struct InWorldEffects {
        explicit InWorldEffects(audio::AudioEngine& audio) noexcept : acoustics{audio} {}
        void reset() {
            state = {};
            acoustics.reset();
        }
        InWorldPresentation state;
        AcousticsController acoustics;
    };

    core::EngineConfig m_config;
    SDL_Window* m_window{nullptr};
    SDL_GLContext m_glContext{nullptr};

    // The open world and all its simulation state; null on the title screen without a world.
    std::unique_ptr<WorldSession> m_session;
    render::WorldRenderer m_worldRenderer;
    render::TextRenderer m_textRenderer;
    ui::HudRenderer m_hudRenderer;
    audio::AudioEngine m_audio;
    InWorldEffects m_inWorld{m_audio};

    ui::ScreenState m_screenState{ui::ScreenState::TitleScreen};
    ui::ScreenState m_previousScreenState{ui::ScreenState::TitleScreen};
    ui::InputManager m_inputManager;
    ui::TitleScreen m_titleScreen;
    ui::DeathScreen m_deathScreen;
    ui::OptionsMenu m_optionsMenu;
    ui::WorldSelectScreen m_worldSelect;
    ui::CreateWorldScreen m_createWorld;
    bool m_running{false};
    bool m_mouseCaptured{false};
    bool m_inventoryOpen{false};
    bool m_showDebug{false};

    int32_t m_mouseX{0};
    int32_t m_mouseY{0};

    gameplay::SplitMix64 m_sfxRng{0x50D5EEDULL}; // Sound pitch jitter (pop, hurt)
    // Effects that do not change the world (lightning bolt shape, rain splashes): never the
    // session's weather RNG, so effects cannot shift the simulation.
    gameplay::SplitMix64 m_presentationRng{0x9E5E47A7ULL};
    float m_renderAlpha{1.0f};
    float m_smoothedFps{60.0f};
    float m_smoothedFrameMs{16.6f};
};

} // namespace betacraft::game
