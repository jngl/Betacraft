#include "game/Application.hpp"
#include "game/FluidEffects.hpp"
#include "core/Logger.hpp"
#include "crafting/Furnace.hpp"
#include "crafting/ItemIds.hpp"
#include "ecs/Components.hpp"
#include "game/ItemBridge.hpp"
#include "game/ItemUse.hpp"
#include "storage/SaveMigration.hpp"
#include "world/BlockShapes.hpp"
#include "world/Weather.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <variant>

namespace betacraft::game {

namespace {

// F3 storage and streaming numbers (S7.5, S7.10).
[[nodiscard]] ui::HudStorageStats hudStorageStats(const world::World& world) {
    const world::World::StorageStats& storage = world.getStorageStats();
    const world::World::StreamingStats streaming = world.getStreamingStats();
    const auto ms = [](std::chrono::nanoseconds t) { return static_cast<float>(t.count()) / 1.0e6f; };
    return {.bundlesWritten = storage.bundlesWritten,
            .unsolicitedWrites = storage.unsolicitedBundleWrites,
            .generating = static_cast<uint32_t>(streaming.generating),
            .integrating = static_cast<uint32_t>(streaming.integrating),
            .writing = static_cast<uint32_t>(streaming.writing),
            .lastIntegrationMs = ms(streaming.lastIntegration),
            .maxIntegrationMs = ms(streaming.maxIntegration),
            .openRegions = static_cast<uint32_t>(streaming.openRegions),
            .openRegionBytes = streaming.openRegionBytes,
            .loading = static_cast<uint32_t>(streaming.loading)};
}

[[nodiscard]] ui::CraftingScreenView craftingScreenView(const InventoryCrafting& screen, const world::World& world) noexcept {
    ui::CraftingScreenView view{};
    view.gridDim = static_cast<int>(screen.gridSize());
    for (size_t i = 0; i < screen.grid().slotCount() && i < view.grid.size(); ++i) {
        view.grid[i] = screen.gridSlot(i);
    }
    view.result = screen.resultPreview();
    view.carried = screen.carried();
    view.kind = screen.kind();
    for (size_t i = 0; i < screen.containerSlotCount() && i < view.container.size(); ++i) {
        view.container[i] = screen.containerSlot(world, i);
    }
    if (const std::optional<core::BlockPos> pos = screen.containerPos()) {
        if (const world::BlockEntity* entity = world.blockEntities().find(*pos)) {
            if (const auto* furnace = std::get_if<world::FurnaceData>(entity)) {
                view.cookProgress = crafting::cookProgress(*furnace);
                view.burnProgress = crafting::burnProgress(*furnace);
            }
        }
    }
    return view;
}

// The screen a right click on `block` opens (step 2 of the right-click order), if any.
[[nodiscard]] std::optional<ScreenKind> activatedScreen(world::BlockId block) noexcept {
    switch (block) {
        case world::BlockId::CraftingTable: return ScreenKind::Workbench3x3;
        case world::BlockId::Furnace:
        case world::BlockId::LitFurnace: return ScreenKind::Furnace;
        case world::BlockId::Chest: return ScreenKind::Chest;
        default: return std::nullopt;
    }
}

// Beta TileEntityFurnace / TileEntityChest.canInteractWith: within 8 blocks of the centre.
constexpr float kContainerReachSq = 64.0f;

} // namespace

Application::Application(core::EngineConfig config)
    : m_config(std::move(config)) {}

Application::~Application() noexcept {
    shutdown();
}

bool Application::initialize() {
    core::Logger::info("Application", "Starting ", m_config.windowTitle, " ...");
    world::BlockRegistry::initialize();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        core::Logger::error("Application", "SDL_Init failed: ", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    Uint32 winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
    if (m_config.fullscreen) {
        winFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    m_window = SDL_CreateWindow(
        m_config.windowTitle.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        m_config.windowWidth,
        m_config.windowHeight,
        winFlags
    );
    if (!m_window) {
        core::Logger::error("Application", "SDL_CreateWindow failed: ", SDL_GetError());
        return false;
    }

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        core::Logger::error("Application", "SDL_GL_CreateContext failed: ", SDL_GetError());
        return false;
    }

    SDL_GL_MakeCurrent(m_window, m_glContext);
    SDL_GL_SetSwapInterval(m_config.vsync ? 1 : 0);

    if (!render::initOpenGLFunctions()) {
        core::Logger::error("Application", "Failed to load required OpenGL 3.3 Core functions.");
        return false;
    }

    core::Logger::info(
        "OpenGL",
        "Renderer: ", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
        " | Version: ", reinterpret_cast<const char*>(glGetString(GL_VERSION))
    );

    (void)m_audio.initialize();

    if (!m_worldRenderer.initialize()) {
        core::Logger::error("Application", "Failed to initialize WorldRenderer.");
        return false;
    }

    if (!m_hudRenderer.initialize()) {
        core::Logger::error("Application", "Failed to initialize HudRenderer.");
        return false;
    }

    (void)m_textRenderer.initialize();
    (void)m_inputManager.loadFromFile("options.txt");
    syncRuntimeOptions();

    m_titleScreen.setSelectedMode(m_config.defaultGameMode);
    m_titleScreen.setWorldLoaded(false);
    m_createWorld.setSavesRoot(m_config.savesRoot);
    if (m_config.autoEnterWorld && !enterWorld(m_config.worldSaveDir)) {
        return false;
    }

    setMouseCaptured(false);
    m_running = true;
    return true;
}

bool Application::enterWorld(const std::filesystem::path& dir, std::optional<core::NewWorldParams> params) {
    if (dir.empty()) {
        return false;
    }
    leaveWorld();
    const bool fromMenu =
        m_screenState == ui::ScreenState::WorldSelect || m_screenState == ui::ScreenState::CreateWorld;
    m_screenState = ui::ScreenState::Loading;
    setMouseCaptured(false);
    if (fromMenu) {
        presentLoadingFrame("Loading level", "Building terrain", std::nullopt);
    }

    WorldSession::OpenOptions options{};
    options.folder = dir;
    options.seed = m_config.worldSeed;
    options.newWorld = std::move(params);
    options.features = m_config.features;
    options.dayLengthSeconds = m_config.dayLengthSeconds;
    options.defaultGameMode = m_config.defaultGameMode;
    // S7.4: the 10 % log lines, and a progress bar beyond kMigrationProgressThreshold chunks.
    int lastDecile = -1;
    int lastPercent = -1;
    options.onMigrationProgress = [&](const MigrationProgress& progress) {
        const int decile = static_cast<int>(progress.fraction * 10.0f);
        if (decile != lastDecile) {
            lastDecile = decile;
            core::Logger::info("Application", "Migration ", decile * 10, "%");
        }
        const int percent = static_cast<int>(progress.fraction * 100.0f);
        if (progress.legacyChunks > storage::kMigrationProgressThreshold && percent != lastPercent) {
            lastPercent = percent;
            presentLoadingFrame("Converting world", "Converting " + std::to_string(progress.legacyChunks) + " chunks",
                                progress.fraction);
        }
    };
    m_session = WorldSession::open(std::move(options));
    if (!m_session) {
        m_screenState = ui::ScreenState::TitleScreen;
        return false;
    }
    m_config.worldSeed = world().getSeed();
    m_config.worldSaveDir = dir.string();
    // The title's mode button only for a new world opened at startup (autoEnterWorld).
    m_titleScreen.setNewWorld(m_session->awaitingGameMode());
    m_titleScreen.setWorldLoaded(true);
    m_screenState = ui::ScreenState::TitleScreen;
    return true;
}

void Application::syncRuntimeOptions() {
    const int newRenderDist = m_inputManager.getRenderDistance();
    if (m_config.renderDistanceChunks != newRenderDist) {
        m_config.renderDistanceChunks = newRenderDist;
        if (m_session) {
            // S7.7: a larger view distance streams in over the next frames instead of freezing.
            if (m_config.features.asyncWorldGen) {
                world().requestChunksAround(player().getPosition(), streamingRadius());
            } else {
                world().updateChunksAround(player().getPosition(), streamingRadius(), 64);
            }
        }
    }
    m_config.fovDegrees = m_inputManager.getFov();
    if (m_audio.isMusicEnabled() != m_inputManager.isMusicEnabled()) {
        m_audio.toggleMusic();
    }
}

void Application::setMouseCaptured(bool captured) noexcept {
    m_mouseCaptured = captured;
    SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
}

void Application::processEvents() {
    SDL_Event ev{};
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            m_running = false;
            break;
        }

        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            m_config.windowWidth = ev.window.data1;
            m_config.windowHeight = ev.window.data2;
            continue;
        }

        if (ev.type == SDL_MOUSEMOTION) {
            m_mouseX = ev.motion.x;
            m_mouseY = ev.motion.y;
            if (m_screenState == ui::ScreenState::InGame && m_mouseCaptured && !m_inventoryOpen) {
                // S3.8: Beta's sensitivity curve, 0.15 degree per pixel at the default 50 %.
                const float sensitivity = m_inputManager.getDegreesPerPixel();
                player().rotate(
                    static_cast<float>(ev.motion.xrel) * sensitivity,
                    -static_cast<float>(ev.motion.yrel) * sensitivity
                );
            }
            continue;
        }

        // Screen state dispatch
        if (m_screenState == ui::ScreenState::TitleScreen) {
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                bool quitGame = false;
                if (m_titleScreen.handleMouseClick(
                        static_cast<float>(ev.button.x),
                        static_cast<float>(ev.button.y),
                        m_config.windowWidth,
                        m_config.windowHeight,
                        m_screenState,
                        quitGame,
                        m_audio)) {
                    if (m_screenState == ui::ScreenState::InGame) {
                        syncRuntimeOptions();
                        enterGame();
                    } else if (m_screenState == ui::ScreenState::WorldSelect) {
                        openWorldSelect();
                    } else if (m_screenState == ui::ScreenState::OptionsMenu) {
                        m_previousScreenState = ui::ScreenState::TitleScreen;
                    }
                    if (quitGame) {
                        m_running = false;
                    }
                }
            } else if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0 && ev.key.keysym.sym == SDLK_ESCAPE) {
                m_running = false;
            }
            continue;
        }

        if (m_screenState == ui::ScreenState::WorldSelect) {
            ui::WorldSelectChoice choice = ui::WorldSelectChoice::None;
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                choice = m_worldSelect.handleMouseClick(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y),
                                                        m_config.windowWidth, m_config.windowHeight, m_audio);
            } else if (ev.type == SDL_MOUSEWHEEL) {
                m_worldSelect.scroll(ev.wheel.y, m_config.windowHeight);
            } else if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0) {
                choice = m_worldSelect.handleKeyDown(ev.key.keysym.sym, m_audio);
            }
            if (choice == ui::WorldSelectChoice::Play) {
                playSelectedWorld();
            } else if (choice == ui::WorldSelectChoice::Create) {
                openCreateWorld();
            } else if (choice == ui::WorldSelectChoice::Cancel) {
                m_screenState = ui::ScreenState::TitleScreen;
            }
            continue;
        }

        if (m_screenState == ui::ScreenState::CreateWorld) {
            ui::CreateWorldChoice choice = ui::CreateWorldChoice::None;
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                choice = m_createWorld.handleMouseClick(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y),
                                                        m_config.windowWidth, m_config.windowHeight, m_audio);
            } else if (ev.type == SDL_TEXTINPUT) {
                m_createWorld.handleTextInput(ev.text.text);
            } else if (ev.type == SDL_KEYDOWN) {
                choice = m_createWorld.handleKeyDown(ev.key.keysym.sym, m_audio);
            }
            if (choice == ui::CreateWorldChoice::Create) {
                SDL_StopTextInput();
                createWorldFromScreen();
            } else if (choice == ui::CreateWorldChoice::Cancel) {
                SDL_StopTextInput();
                openWorldSelect();
            }
            continue;
        }

        if (m_screenState == ui::ScreenState::Loading) {
            continue; // enterWorld runs to completion before events are read again
        }

        if (m_screenState == ui::ScreenState::OptionsMenu || m_screenState == ui::ScreenState::KeybindingMenu) {
            if (ev.type == SDL_KEYDOWN) {
                if (m_optionsMenu.isListeningForKey()) {
                    m_optionsMenu.handleKeyDown(ev.key.keysym.scancode, m_inputManager, m_audio);
                    syncRuntimeOptions();
                } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    if (m_screenState == ui::ScreenState::KeybindingMenu) {
                        m_screenState = ui::ScreenState::OptionsMenu;
                    } else {
                        (void)m_inputManager.saveToFile("options.txt");
                        m_screenState = m_previousScreenState;
                        if (m_screenState == ui::ScreenState::InGame) {
                            setMouseCaptured(true);
                        }
                    }
                    syncRuntimeOptions();
                    m_audio.playUiClick();
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                m_optionsMenu.handleMouseClick(
                    static_cast<float>(ev.button.x),
                    static_cast<float>(ev.button.y),
                    m_config.windowWidth,
                    m_config.windowHeight,
                    m_screenState,
                    m_previousScreenState,
                    m_inputManager,
                    m_audio
                );
                syncRuntimeOptions();
                if (m_screenState == ui::ScreenState::InGame) {
                    setMouseCaptured(true);
                }
            }
            continue;
        }

        if (m_screenState == ui::ScreenState::DeathScreen) {
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                const ui::HudCanvas canvas = hudCanvas();
                const ui::DeathChoice choice = m_deathScreen.handleMouseClick(
                    static_cast<float>(canvas.toCanvasX(ev.button.x)), static_cast<float>(canvas.toCanvasY(ev.button.y)),
                    canvas.width, canvas.height, m_audio);
                if (choice == ui::DeathChoice::Respawn) {
                    respawnPlayer();
                } else if (choice == ui::DeathChoice::TitleMenu) {
                    // Same as the pause menu's "Save and Quit to Title".
                    leaveWorld();
                }
            }
            continue;
        }

        if (m_screenState == ui::ScreenState::Sleeping) {
            // Beta GuiSleepMP "Leave Bed": Esc gets up without skipping the night.
            if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0 && ev.key.keysym.sym == SDLK_ESCAPE) {
                m_session->wakeUp();
                m_screenState = ui::ScreenState::InGame;
                m_audio.playUiClick();
            }
            continue;
        }

        if (m_screenState == ui::ScreenState::PauseMenu) {
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                syncRuntimeOptions();
                m_screenState = ui::ScreenState::InGame;
                setMouseCaptured(true);
                m_audio.playUiClick();
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                const float sw = static_cast<float>(m_config.windowWidth);
                const float sh = static_cast<float>(m_config.windowHeight);
                const float btnW = 280.0f;
                const float btnH = 38.0f;
                const float btnX = (sw - btnW) * 0.5f;
                const float startY = sh * 0.35f;

                const float mx = static_cast<float>(ev.button.x);
                const float my = static_cast<float>(ev.button.y);

                if (mx >= btnX && mx <= btnX + btnW) {
                    if (my >= startY && my <= startY + btnH) {
                        m_audio.playUiClick();
                        syncRuntimeOptions();
                        m_screenState = ui::ScreenState::InGame;
                        setMouseCaptured(true);
                    } else if (my >= startY + 50.0f && my <= startY + 50.0f + btnH) {
                        m_audio.playUiClick();
                        m_previousScreenState = ui::ScreenState::PauseMenu;
                        m_screenState = ui::ScreenState::OptionsMenu;
                    } else if (my >= startY + 100.0f && my <= startY + 100.0f + btnH) {
                        m_audio.playUiClick();
                        leaveWorld(); // S7.9: saved, every queued chunk on disk, world closed
                    }
                }
            }
            continue;
        }

        // InGame event processing
        switch (ev.type) {
            case SDL_MOUSEWHEEL:
                if (!m_inventoryOpen && ev.wheel.y != 0) {
                    player().scrollSelectedSlot(ev.wheel.y > 0 ? -1 : 1);
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (m_inventoryOpen) {
                    if (ev.button.button == SDL_BUTTON_LEFT || ev.button.button == SDL_BUTTON_RIGHT) {
                        handleInventoryClick(ev.button.x, ev.button.y,
                                             ev.button.button == SDL_BUTTON_LEFT ? SlotButton::Left : SlotButton::Right);
                    }
                } else if (!m_mouseCaptured) {
                    setMouseCaptured(true);
                } else {
                    if (ev.button.button == SDL_BUTTON_LEFT) {
                        m_inWorld.state.leftMouseDown = true;
                        m_inWorld.state.attackPressed = true;
                        player().triggerHandSwing();
                    } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                        handleRightClickAction();
                    } else if (ev.button.button == SDL_BUTTON_MIDDLE) {
                        // Pick Block: Creative conjures a full stack, Survival (Beta) only
                        // selects a hotbar slot that already holds the block.
                        if (m_inWorld.state.currentTarget.hit && m_inWorld.state.currentTarget.blockId != world::BlockId::Bedrock) {
                            // A lit furnace is a furnace in hand: a placed LitFurnace without a
                            // block entity would glow forever.
                            const world::BlockId picked = m_inWorld.state.currentTarget.blockId == world::BlockId::LitFurnace
                                ? world::BlockId::Furnace
                                : m_inWorld.state.currentTarget.blockId;
                            // Doors, beds and crops are picked as the item that places them.
                            gameplay::ItemId pickedItem = gameplay::toItemId(picked);
                            switch (picked) {
                                case world::BlockId::WoodDoor: pickedItem = gameplay::items::WoodenDoor; break;
                                case world::BlockId::IronDoor: pickedItem = gameplay::items::IronDoor; break;
                                case world::BlockId::RedstoneWire: pickedItem = gameplay::items::Redstone; break;
                                case world::BlockId::RedstoneTorchOff:
                                    pickedItem = gameplay::toItemId(world::BlockId::RedstoneTorchOn);
                                    break;
                                case world::BlockId::Bed: pickedItem = gameplay::items::Bed; break;
                                case world::BlockId::Crops: pickedItem = gameplay::items::Seeds; break;
                                default: break;
                            }
                            if (player().isCreative()) {
                                player().setInventorySlot(
                                    static_cast<size_t>(player().getSelectedSlot()),
                                    {pickedItem, gameplay::maxStackSize(pickedItem)}
                                );
                                m_audio.playUiClick();
                            } else {
                                (void)player().selectHotbarSlotHolding(pickedItem);
                            }
                        }
                    }
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    m_inWorld.state.leftMouseDown = false;
                }
                break;

            case SDL_KEYDOWN:
                if (ev.key.repeat != 0) break;
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    if (m_inventoryOpen) {
                        closeInventoryScreen();
                        m_audio.playUiClick();
                    } else {
                        m_screenState = ui::ScreenState::PauseMenu;
                        setMouseCaptured(false);
                        m_audio.playUiClick();
                    }
                    break;
                }

                if (ev.key.keysym.scancode == m_inputManager.getPrimaryBinding(ui::GameAction::Inventory) ||
                    ev.key.keysym.sym == SDLK_e || ev.key.keysym.sym == SDLK_i) {
                    if (m_inventoryOpen) {
                        closeInventoryScreen();
                    } else {
                        openInventoryScreen(crafting::GridSize::Inventory2x2);
                    }
                    m_audio.playUiClick();
                    break;
                }

                // Q throws one item, Ctrl+Q the whole stack (Beta dropCurrentItem); not from
                // the inventory screen (out of scope for S3.4).
                if (!m_inventoryOpen &&
                    ev.key.keysym.scancode == m_inputManager.getPrimaryBinding(ui::GameAction::DropItem)) {
                    const bool wholeStack = (SDL_GetModState() & KMOD_CTRL) != 0;
                    if (sim().dropHeldItem(player(), wholeStack) != ecs::NULL_ENTITY) {
                        player().triggerHandSwing();
                    }
                    break;
                }

                // S8.13 debug weather cycle: no default key, Creative only.
                if (const SDL_Scancode cycleKey = m_inputManager.getPrimaryBinding(ui::GameAction::CycleWeather);
                    cycleKey != SDL_SCANCODE_UNKNOWN && ev.key.keysym.scancode == cycleKey) {
                    if (m_session->cycleWeatherIfCreative()) {
                        showStatusMessage(!m_session->weather().raining      ? "Weather: clear"
                                          : m_session->weather().thundering ? "Weather: thunderstorm"
                                                                 : "Weather: rain");
                        m_audio.playUiClick();
                    }
                    break;
                }

                if (ev.key.keysym.scancode == m_inputManager.getPrimaryBinding(ui::GameAction::ToggleMusic) ||
                    ev.key.keysym.sym == SDLK_m) {
                    m_audio.toggleMusic();
                    m_audio.playUiClick();
                    break;
                }

                if (ev.key.keysym.scancode == m_inputManager.getPrimaryBinding(ui::GameAction::ToggleDebug) ||
                    ev.key.keysym.sym == SDLK_F3) {
                    m_showDebug = !m_showDebug;
                    break;
                }

                switch (ev.key.keysym.sym) {
                    case SDLK_f:
                        if (player().toggleFlyMode()) {
                            m_audio.playUiClick();
                        }
                        break;

                    case SDLK_l:
                        m_config.smoothLighting = !m_config.smoothLighting;
                        m_worldRenderer.markAllChunksDirty(world());
                        m_audio.playUiClick();
                        break;

                    case SDLK_t:
                        m_session->skipTimeIfCreative();
                        break;

                    case SDLK_F5:
                        saveWorld();
                        m_audio.playUiClick();
                        break;

                    case SDLK_F11:
                        m_config.fullscreen = !m_config.fullscreen;
                        SDL_SetWindowFullscreen(m_window, m_config.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        break;

                    case SDLK_1: player().setSelectedSlot(0); break;
                    case SDLK_2: player().setSelectedSlot(1); break;
                    case SDLK_3: player().setSelectedSlot(2); break;
                    case SDLK_4: player().setSelectedSlot(3); break;
                    case SDLK_5: player().setSelectedSlot(4); break;
                    case SDLK_6: player().setSelectedSlot(5); break;
                    case SDLK_7: player().setSelectedSlot(6); break;
                    case SDLK_8: player().setSelectedSlot(7); break;
                    case SDLK_9: player().setSelectedSlot(8); break;
                    default: break;
                }
                break;

            default:
                break;
        }
    }
}

void Application::openInventoryScreen(crafting::GridSize grid) {
    dropAtPlayer(m_session->crafting().open(grid, player()));
    m_inventoryOpen = true;
    m_inWorld.state.leftMouseDown = false;
    m_inWorld.state.attackPressed = false;
    sim().resetMining();
    setMouseCaptured(false);
}

void Application::openContainerScreen(ScreenKind kind, const core::BlockPos& pos) {
    const world::BlockId block = world().getBlock(pos);
    if (!world::hasBlockEntity(block)) {
        return;
    }
    (void)fillDungeonChest(world(), pos);                  // S6.10: loot drawn on first open
    (void)world().blockEntities().getOrCreate(pos, block); // Created on first open
    dropAtPlayer(m_session->crafting().openContainer(kind, pos, player()));
    m_inventoryOpen = true;
    m_inWorld.state.leftMouseDown = false;
    m_inWorld.state.attackPressed = false;
    sim().resetMining();
    setMouseCaptured(false);
}

void Application::closeContainerIfOutOfReach() {
    const std::optional<core::BlockPos> pos = m_session->crafting().containerPos();
    if (!m_inventoryOpen || !pos) {
        return;
    }
    const glm::vec3 d = player().getEyePosition() - core::blockCentre(*pos);
    const world::BlockEntity* entity = world().blockEntities().find(*pos);
    if (glm::dot(d, d) > kContainerReachSq || entity == nullptr ||
        !world::blockEntityMatches(world().getBlock(*pos), *entity)) {
        closeInventoryScreen();
    }
}

void Application::closeInventoryScreen() {
    m_session->closeCrafting();
    m_inventoryOpen = false;
    setMouseCaptured(true);
}

void Application::dropAtPlayer(const std::vector<physics::InventorySlot>& stacks) {
    sim().dropAllFromPlayer(player(), stacks);
}

float Application::hudScale() const noexcept {
    return ui::HudRenderer::hudScale(m_config.windowWidth, m_config.windowHeight,
                                     m_inputManager.getGuiScalePreference());
}

ui::HudCanvas Application::hudCanvas() const noexcept {
    return ui::HudRenderer::hudCanvas(m_config.windowWidth, m_config.windowHeight,
                                      m_inputManager.getGuiScalePreference());
}

void Application::handleInventoryClick(int32_t mouseX, int32_t mouseY, SlotButton button) {
    InventoryCrafting& screen = m_session->crafting();
    const bool container = isContainerScreen(screen.kind());
    const ui::InventoryLayoutKey layout{static_cast<int>(screen.gridSize()), screen.kind(),
                                        player().isCreative() && !container};
    // The inventory is laid out in the GUI Scale canvas (S3.7): window points map back first.
    const ui::HudCanvas canvas = hudCanvas();
    mouseX = canvas.toCanvasX(mouseX);
    mouseY = canvas.toCanvasY(mouseY);
    const ui::InventoryHit hit =
        ui::HudRenderer::hitTestInventoryScreen(mouseX, mouseY, canvas.width, canvas.height, layout);
    // Shift-click moves whole stacks between sections (Beta transferStackInSlot, S3.5).
    const bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
    bool changed = false;
    switch (hit.region) {
        case ui::InventoryRegion::Container:
            changed = shift ? screen.shiftClickContainerSlot(player(), world(), hit.index)
                            : screen.clickContainerSlot(world(), hit.index, button);
            if (!screen.containerPos()) {
                closeInventoryScreen(); // The furnace / chest went away under the screen
                return;
            }
            break;
        case ui::InventoryRegion::Palette:
            changed = player().isCreative() && button == SlotButton::Left &&
                      m_hudRenderer.handleInventoryClick(mouseX, mouseY, canvas.width, canvas.height, player());
            break;
        case ui::InventoryRegion::Inventory:
            changed = shift ? screen.shiftClickInventorySlot(player(), &world(), hit.index)
                            : screen.clickInventorySlot(player(), hit.index, button);
            if (container && !screen.containerPos()) {
                closeInventoryScreen(); // The chest went away under the screen
                return;
            }
            break;
        case ui::InventoryRegion::CraftingGrid:
            changed = shift ? screen.shiftClickGridSlot(player(), hit.index) : screen.clickGridSlot(hit.index, button);
            break;
        case ui::InventoryRegion::CraftingResult: {
            std::vector<physics::InventorySlot> overflow;
            changed = shift ? screen.shiftClickResult(player(), &overflow) : screen.clickResult(player(), &overflow);
            dropAtPlayer(overflow);
            break;
        }
        case ui::InventoryRegion::Armor: // S6.7
            changed = shift ? screen.shiftClickArmorSlot(player(), hit.index)
                            : screen.clickArmorSlot(player(), hit.index, button);
            break;
        case ui::InventoryRegion::Outside: {
            // Beta slotClick(-999): left throws the cursor stack, right a single item.
            const physics::InventorySlot thrown = screen.takeCarried(button);
            if (!thrown.isEmpty()) {
                dropAtPlayer({thrown});
                changed = true;
            }
            break;
        }
        case ui::InventoryRegion::None:
            break;
    }
    if (changed) {
        m_audio.playUiClick();
    }
}

// Beta Minecraft.clickMouse(1), in the canonical right-click order (roadmap conventions §2):
// 1. the entity under the crosshair (an empty bucket on a cow, S6.3);
// 2. activating the targeted block unless sneaking (workbench, furnace, chest, Creative piston);
// 3. the held item on the targeted block (placing the held block);
// 4. the held item in the void (food), also when a block is targeted but step 3 did nothing.
void Application::handleRightClickAction() {
    if (m_inWorld.state.placeCooldown > 0.0f) {
        return;
    }
    const bool onBlock = m_inWorld.state.currentTarget.hit;

    // Step 1 (S6.3): the held item on the entity under the crosshair (milking a cow).
    if (const std::optional<ecs::EntityId> entity = sim().meleeTarget(player(), m_inWorld.state.currentTarget)) {
        if (sim().useHeldItemOnEntity(player(), *entity)) {
            m_inWorld.state.placeCooldown = 0.16f;
            return;
        }
    }

    if (onBlock && !player().isSneaking()) {
        // S8.7: a lever switches, a button goes down (their click comes back as a redstone
        // event); an iron door takes the click and does nothing (Beta BlockDoor.blockActivated).
        const world::BlockId targeted = m_inWorld.state.currentTarget.blockId;
        if (targeted == world::BlockId::Lever || targeted == world::BlockId::StoneButton ||
            targeted == world::BlockId::IronDoor) {
            if (targeted == world::BlockId::Lever) {
                (void)world().toggleLever(m_inWorld.state.currentTarget.blockPos);
            } else if (targeted == world::BlockId::StoneButton) {
                (void)world().pressButton(m_inWorld.state.currentTarget.blockPos);
            }
            player().triggerHandSwing();
            m_inWorld.state.placeCooldown = 0.18f;
            return;
        }
        if (activateDoorOrBed()) {
            return;
        }
        if (const std::optional<ScreenKind> screen = activatedScreen(m_inWorld.state.currentTarget.blockId)) {
            if (*screen == ScreenKind::Workbench3x3) {
                openInventoryScreen(crafting::GridSize::Workbench3x3);
            } else {
                openContainerScreen(*screen, m_inWorld.state.currentTarget.blockPos);
            }
            m_audio.playUiClick();
            return;
        }
        // Creative: clicking a piston pushes the blocks opposite to the clicked face (no
        // redstone yet). In Survival a piston is just a block to build against.
        if (player().isCreative() && m_inWorld.state.currentTarget.blockId == world::BlockId::Piston) {
            core::FaceDir pushDir = m_inWorld.state.currentTarget.face;
            switch (m_inWorld.state.currentTarget.face) {
                case core::FaceDir::PosX: pushDir = core::FaceDir::NegX; break;
                case core::FaceDir::NegX: pushDir = core::FaceDir::PosX; break;
                case core::FaceDir::PosY: pushDir = core::FaceDir::NegY; break;
                case core::FaceDir::NegY: pushDir = core::FaceDir::PosY; break;
                case core::FaceDir::PosZ: pushDir = core::FaceDir::NegZ; break;
                case core::FaceDir::NegZ: pushDir = core::FaceDir::PosZ; break;
            }
            if (world().triggerPistonPush(m_inWorld.state.currentTarget.blockPos, pushDir)) {
                playWorldSound(core::blockCentre(m_inWorld.state.currentTarget.blockPos),
                               [&](const audio::VoiceAcoustics& a) { return m_audio.playPistonSound(a); });
                player().triggerHandSwing();
                m_inWorld.state.placeCooldown = 0.18f;
                return;
            }
        }
    }

    if (onBlock) {
        // Step 3: flint and steel on TNT (S6.6), the held item on the block (door, bed, seeds,
        // hoe, bucket), then the held block.
        if (sim().igniteTnt(world(), player(), m_inWorld.state.currentTarget)) {
            m_inWorld.state.placeCooldown = 0.16f;
            return;
        }
        if (const std::optional<ItemUseResult> used = useHeldItemOnBlock(world(), player(), m_inWorld.state.currentTarget)) {
            playItemUseEffects(*used);
            m_inWorld.state.placeCooldown = 0.16f;
            return;
        }
        if (const std::optional<world::BlockId> placed = placeHeldBlock(world(), player(), m_inWorld.state.currentTarget)) {
            const world::BlockDef& def = world::BlockRegistry::get(*placed);
            player().triggerHandSwing();
            playWorldSound(core::blockCentre(m_inWorld.state.currentTarget.adjacentPos),
                           [&](const audio::VoiceAcoustics& a) { return m_audio.playBlockPlace(def.sound, a); });
            m_inWorld.state.placeCooldown = 0.16f;
            return;
        }
    }

    // Step 4 for buckets (S6.4): a liquid source or a wall within reach, nothing under the
    // crosshair.
    if (!onBlock) {
        if (const std::optional<ItemUseResult> used = useHeldBucket(world(), player())) {
            playItemUseEffects(*used);
            m_inWorld.state.placeCooldown = 0.16f;
            return;
        }
    }
    if (sim().useHeldItem(player())) {
        playWorldSound(player().getEyePosition(), [&](const audio::VoiceAcoustics& a) {
            return m_audio.playSound(audio::SoundEffect::Eat, 0.7f, 1.0f, a);
        });
        m_inWorld.state.placeCooldown = 0.16f;
    }
}

void Application::playItemUseEffects(const ItemUseResult& used) {
    player().triggerHandSwing();
    if (used.kind == ItemUseKind::BucketFilled || used.kind == ItemUseKind::BucketEmptied) {
        // Beta has no bucket sound: a splash for water, the fizz for lava.
        if (world::isLavaBlock(used.block)) {
            playWorldSound(core::blockCentre(used.pos),
                           [&](const audio::VoiceAcoustics& a) { return m_audio.playLavaFizz(0.6f, a); });
        } else {
            playWorldSound(core::blockCentre(used.pos),
                           [&](const audio::VoiceAcoustics& a) { return m_audio.playWaterSplash(a); });
        }
        return;
    }
    const world::BlockDef& def = world::BlockRegistry::get(used.block);
    playWorldSound(core::blockCentre(used.pos),
                   [&](const audio::VoiceAcoustics& a) { return m_audio.playBlockPlace(def.sound, a); });
    if (used.wear == gameplay::WearResult::Broken) {
        playWorldSound(player().getEyePosition(), [&](const audio::VoiceAcoustics& a) {
            return m_audio.playSound(audio::SoundEffect::BreakWood, 0.8f, 1.6f, a);
        });
    }
}

bool Application::activateDoorOrBed() {
    const core::BlockPos pos = m_inWorld.state.currentTarget.blockPos;
    if (const std::optional<bool> open = toggleDoor(world(), pos)) {
        // Beta World.playAuxSFX 1003: random.door_open or door_close.
        const audio::SoundEffect effect = *open ? audio::SoundEffect::DoorOpen : audio::SoundEffect::DoorClose;
        const float pitch = m_sfxRng.nextFloat() * 0.1f + 0.9f;
        playWorldSound(core::blockCentre(pos), [&](const audio::VoiceAcoustics& a) {
            return m_audio.playSound(effect, 1.0f, pitch, a);
        });
        player().triggerHandSwing();
        m_inWorld.state.placeCooldown = 0.18f;
        return true;
    }
    if (world().getBlock(pos) != world::BlockId::Bed) {
        return false;
    }
    switch (m_session->trySleep(pos)) {
        case SleepResult::Asleep:
            m_screenState = ui::ScreenState::Sleeping;
            m_inWorld.state.leftMouseDown = false;
            m_inWorld.state.attackPressed = false;
            sim().resetMining();
            break;
        case SleepResult::NotNight:
            showStatusMessage("You can only sleep at night");
            break;
        case SleepResult::MonstersNearby:
            showStatusMessage("You may not rest now, there are monsters nearby");
            break;
        case SleepResult::NotABed:
            break;
    }
    m_inWorld.state.placeCooldown = 0.18f;
    return true;
}

void Application::showStatusMessage(std::string text) {
    core::Logger::info("Application", text);
    m_inWorld.state.statusMessage = std::move(text);
    m_inWorld.state.statusMessageSeconds = 4.0f;
}

void Application::fixedUpdate(float fixedDt) {
    physics::PlayerInput input{};
    if (m_mouseCaptured && !m_inventoryOpen && m_screenState == ui::ScreenState::InGame) {
        m_inputManager.updateKeyboardState(SDL_GetKeyboardState(nullptr));
        input = m_inputManager.buildPlayerInput();
    }

    const physics::StepEvent stepEv = m_session->stepPlayer(fixedDt, input);
    // Footsteps and splashes come from the player's feet: unoccluded, but they ring in a cave.
    if (stepEv.splashedWater) {
        playWorldSound(player().getPosition(), [&](const audio::VoiceAcoustics& a) { return m_audio.playWaterSplash(a); });
    } else if (stepEv.triggered) {
        const auto& groundDef = world::BlockRegistry::get(stepEv.groundBlock);
        playWorldSound(player().getPosition(),
                       [&](const audio::VoiceAcoustics& a) { return m_audio.playBlockStep(groundDef.sound, a); });
    }

    m_inWorld.state.particles.update(fixedDt, world());
}

void Application::gameTick() {
    const bool playing = m_mouseCaptured && !m_inventoryOpen && m_screenState == ui::ScreenState::InGame;
    const PlayerActions actions{
        .attackPressed = m_inWorld.state.attackPressed && playing,
        .attackHeld = m_inWorld.state.leftMouseDown && playing,
        .target = m_inWorld.state.currentTarget
    };
    m_inWorld.state.attackPressed = false;
    const WorldTickEvents events = m_session->tick(actions);
    playGameTickEffects(events.game);
    if (m_screenState == ui::ScreenState::Sleeping && !m_session->isSleeping()) {
        m_screenState = ui::ScreenState::InGame;
    }
    if (events.game.playerDied) {
        handlePlayerDeath();
    }
    closeContainerIfOutOfReach();

    if (const std::optional<FizzSound> fizz =
            loudestFizzSound(events.fluids, player().getInterpolatedEyePosition(m_renderAlpha))) {
        playWorldSound(fizz->position, [&](const audio::VoiceAcoustics& a) { return m_audio.playLavaFizz(fizz->gain, a); });
    }
    playRedstoneEffects(events.redstone);
    tickWeatherEffects(events.lightningStrike);
}

void Application::tickWeatherEffects(const std::optional<glm::vec3>& strike) {
    const glm::vec3 eye = player().getInterpolatedEyePosition(m_renderAlpha);

    // S8.10 lightning: the flash and bolt fade over kLightningFlashTicks; the thunder follows at the
    // speed of sound, quieter for a far strike.
    if (m_inWorld.state.lightningTicks > 0 && --m_inWorld.state.lightningTicks == 0) {
        m_inWorld.state.lightningBolt.reset();
    }
    if (strike) {
        m_inWorld.state.lightningBolt = *strike;
        m_inWorld.state.lightningSeed = static_cast<uint32_t>(m_presentationRng.next());
        m_inWorld.state.lightningTicks = kLightningFlashTicks;
        const float dist = glm::length(*strike - eye);
        m_inWorld.state.pendingThunder.push_back({world::thunderDelayTicks(dist), std::clamp(1.0f - dist / 256.0f, 0.35f, 1.0f)});
    }
    for (auto it = m_inWorld.state.pendingThunder.begin(); it != m_inWorld.state.pendingThunder.end();) {
        if (--it->ticks <= 0) {
            (void)m_audio.playThunder(it->volume);
            it = m_inWorld.state.pendingThunder.erase(it);
        } else {
            ++it;
        }
    }

    // S8.11 splashes where the rain lands around the player (Beta addRainParticles).
    const float rain = m_session->weather().rainStrength;
    if (rain > 0.0f) {
        const int32_t splashes = static_cast<int32_t>(rain * rain * 12.0f);
        for (int32_t i = 0; i < splashes; ++i) {
            const int32_t x = static_cast<int32_t>(std::floor(eye.x)) + m_presentationRng.nextInt(21) - 10;
            const int32_t z = static_cast<int32_t>(std::floor(eye.z)) + m_presentationRng.nextInt(21) - 10;
            const int32_t y = world::precipitationHeight(world(), x, z);
            if (y <= 0 || std::abs(static_cast<float>(y) - eye.y) > 10.0f ||
                world::precipitationAt(world().getBiomeAt(x, z)) != world::Precipitation::Rain) {
                continue;
            }
            m_inWorld.state.particles.spawnRainSplash(glm::vec3(static_cast<float>(x) + m_presentationRng.nextFloat(), static_cast<float>(y),
                                                  static_cast<float>(z) + m_presentationRng.nextFloat()),
                                        1);
        }
    }

    // S8.12 the rain loop, re-measured 4 times a second.
    if (rain <= 0.0f) {
        m_audio.setRainLoop(0.0f);
    } else if (m_session->gameTicks() % 5U == 0U) {
        const RainAudibility heard = measureRainAudibility(world(), eye);
        m_audio.setRainLoop(rain * heard.gain, heard.cutoffHz);
    }
}

std::string Application::weatherDebugLine() const {
    std::ostringstream out;
    const glm::vec3 feet = player().getPosition();
    const world::BiomeId biome = world().getBiomeAt(static_cast<int32_t>(std::floor(feet.x)),
                                                     static_cast<int32_t>(std::floor(feet.z)));
    const world::Precipitation precip = world::precipitationAt(biome);
    out << "Weather: " << (m_session->weather().raining ? (m_session->weather().thundering ? "storm" : "rain") : "clear") << std::fixed
        << std::setprecision(2) << "  rain " << m_session->weather().rainStrength << " (" << m_session->weather().rainTime << " t)  thunder "
        << m_session->weather().thunderStrength << " (" << m_session->weather().thunderTime << " t)  "
        << (precip == world::Precipitation::Snow ? "snow" : precip == world::Precipitation::Rain ? "rain" : "dry")
        << " here" << (m_config.features.weather ? "" : "  [--no-weather]");
    // Sprint 8 tick costs next to the fluids and lighting ones: A* nodes and redstone work.
    const redstone::RedstoneTickStats& rs = world().getRedstoneStats();
    out << "  |  A* " << sim().lastPathNodes() << " nodes  redstone " << rs.evaluations << " evals"
        << (rs.pending > 0 ? " (+" + std::to_string(rs.pending) + " pending)" : std::string{});
    return out.str();
}

// S8.7 Beta sounds of the redstone events: random.click (volume 0.3, pitch 0.6 down / on,
// 0.5 up / off), the door sounds, the piston, and random.fizz for a torch burning out.
void Application::playRedstoneEffects(const std::vector<redstone::RedstoneEvent>& events) {
    for (const redstone::RedstoneEvent& ev : events) {
        const glm::vec3 at = core::blockCentre(ev.pos);
        switch (ev.kind) {
            case redstone::RedstoneEventKind::Click: {
                const world::BlockId block = world().getBlock(ev.pos);
                const uint8_t meta = world().getMetadata(ev.pos);
                const bool down = block == world::BlockId::Lever       ? (meta & world::kLeverOnBit) != 0
                                : block == world::BlockId::StoneButton ? (meta & world::kButtonPressedBit) != 0
                                                                       : (meta & world::kPlatePressedBit) != 0;
                playWorldSound(at, [&](const audio::VoiceAcoustics& a) {
                    return m_audio.playSound(audio::SoundEffect::Click, 0.3f, down ? 0.6f : 0.5f, a);
                });
                break;
            }
            case redstone::RedstoneEventKind::DoorOpen:
            case redstone::RedstoneEventKind::DoorClose: {
                const audio::SoundEffect effect = ev.kind == redstone::RedstoneEventKind::DoorOpen
                                                      ? audio::SoundEffect::DoorOpen
                                                      : audio::SoundEffect::DoorClose;
                const float pitch = m_sfxRng.nextFloat() * 0.1f + 0.9f;
                playWorldSound(at, [&](const audio::VoiceAcoustics& a) { return m_audio.playSound(effect, 1.0f, pitch, a); });
                break;
            }
            case redstone::RedstoneEventKind::PistonPush:
                playWorldSound(at, [&](const audio::VoiceAcoustics& a) { return m_audio.playPistonSound(a); });
                break;
            case redstone::RedstoneEventKind::TorchBurnout:
                playWorldSound(at, [&](const audio::VoiceAcoustics& a) { return m_audio.playLavaFizz(0.5f, a); });
                break;
        }
    }
}

void Application::playGameTickEffects(const GameTickEvents& events) {
    if (events.digging) {
        player().triggerHandSwing();
        // Beta plays the dig sound every 4 ticks of a held dig.
        if (m_inWorld.state.digTicks++ % 4 == 0) {
            const world::BlockId block = world().getBlock(m_inWorld.state.currentTarget.blockPos);
            const world::BlockDef& def = world::BlockRegistry::get(block);
            playWorldSound(core::blockCentre(m_inWorld.state.currentTarget.blockPos),
                           [&](const audio::VoiceAcoustics& a) { return m_audio.playBlockDigHit(def.sound, a); });
            m_inWorld.state.particles.spawnMiningHitParticles(world(), m_inWorld.state.currentTarget.blockPos, m_inWorld.state.currentTarget.face, block, 2);
        }
    } else {
        m_inWorld.state.digTicks = 0;
    }
    if (events.blockBroken) {
        const BlockBreakEvent& broken = *events.blockBroken;
        const world::BlockDef& def = world::BlockRegistry::get(broken.block);
        m_inWorld.state.particles.spawnBlockBreakBurst(world(), broken.pos, broken.block, 24);
        playWorldSound(core::blockCentre(broken.pos),
                       [&](const audio::VoiceAcoustics& a) { return m_audio.playBlockBreak(def.sound, a); });
    }
    for (const ExplosionEvent& blast : events.explosions) {
        // Beta Explosion.doExplosionB: random.explode at volume 4, pitch (1 + (r1 - r2) * 0.2) * 0.7.
        m_inWorld.state.particles.spawnExplosionBurst(blast.centre, blast.power);
        const float pitch = (1.0f + (m_sfxRng.nextFloat() - m_sfxRng.nextFloat()) * 0.2f) * 0.7f;
        playWorldSound(blast.centre, [&](const audio::VoiceAcoustics& a) {
            return m_audio.playSound(audio::SoundEffect::Explosion, 1.0f, pitch, a);
        });
    }
    if (events.melee && events.melee->result.hit.applied > 0) {
        const glm::vec3 at = sim().entities().valid(events.melee->target)
            ? sim().entities().get<ecs::TransformComp>(events.melee->target).pos
            : player().getEyePosition();
        playWorldSound(at, [&](const audio::VoiceAcoustics& a) {
            return m_audio.playSound(audio::SoundEffect::MobHurt, 0.9f, 0.9f + m_sfxRng.nextFloat() * 0.2f, a);
        });
    }
    // Beta EntityLiving.playLivingSound / hurt / death: pitch (r1 - r2) * 0.2 + 1.
    for (const MobSoundEvent& mob : events.mobSounds) {
        audio::SoundEffect effect = audio::SoundEffect::MobHurt;
        float volume = 0.8f;
        if (mob.sound == MobSoundKind::Death) {
            effect = audio::SoundEffect::MobDeath;
        }
        float pitchScale = 1.0f;
        if (mob.sound == MobSoundKind::Ambient) {
            volume = 0.6f;
            // No dedicated voices for the S6.3 mobs: the nearest existing one, re-pitched.
            switch (mob.kind) {
                case gameplay::MobKind::Zombie: effect = audio::SoundEffect::ZombieGroan; break;
                case gameplay::MobKind::Skeleton: effect = audio::SoundEffect::ZombieGroan; pitchScale = 1.4f; break;
                case gameplay::MobKind::Spider: effect = audio::SoundEffect::ZombieGroan; pitchScale = 0.7f; break;
                case gameplay::MobKind::Sheep: effect = audio::SoundEffect::SheepBaa; break;
                case gameplay::MobKind::Cow: effect = audio::SoundEffect::SheepBaa; pitchScale = 0.6f; break;
                case gameplay::MobKind::Creeper: continue; // Beta creepers make no living sound
                default: effect = audio::SoundEffect::PigOink; break;
            }
        }
        const float pitch = ((m_sfxRng.nextFloat() - m_sfxRng.nextFloat()) * 0.2f + 1.0f) * pitchScale;
        playWorldSound(mob.pos + glm::vec3(0.0f, 1.0f, 0.0f), [&](const audio::VoiceAcoustics& a) {
            return m_audio.playSound(effect, volume, pitch, a);
        });
    }
    if (events.heldWear == gameplay::WearResult::Broken) {
        // Beta "random.break": a sharp snap, pitched up from the wood break.
        playWorldSound(player().getEyePosition(), [&](const audio::VoiceAcoustics& a) {
            return m_audio.playSound(audio::SoundEffect::BreakWood, 0.8f, 1.6f, a);
        });
    }
    if (events.itemsPickedUp > 0) {
        // Beta random.pop: volume 0.2, pitch ((r1 - r2) * 0.7 + 1) * 2; the synthesised pop
        // is already pitched up, so the factor 2 is left out.
        const float pitch = (m_sfxRng.nextFloat() - m_sfxRng.nextFloat()) * 0.7f + 1.0f;
        playWorldSound(player().getEyePosition(), [&](const audio::VoiceAcoustics& a) {
            return m_audio.playSound(audio::SoundEffect::Pop, 0.2f, pitch, a);
        });
    }
    // Beta attackEntityFrom plays the hurt sound on every hit that lands; a fall adds
    // damage.fallbig above 4 damage, damage.fallsmall otherwise.
    for (const PlayerHurtEvent& hurt : events.playerHurt) {
        playWorldSound(player().getEyePosition(), [&](const audio::VoiceAcoustics& a) {
            return m_audio.playSound(audio::SoundEffect::Hurt, 0.9f, 0.9f + m_sfxRng.nextFloat() * 0.2f, a);
        });
        if (hurt.cause == DamageCause::Fall) {
            const audio::SoundEffect fall = hurt.amount > 4 ? audio::SoundEffect::FallBig : audio::SoundEffect::FallSmall;
            playWorldSound(player().getPosition(), [&](const audio::VoiceAcoustics& a) {
                return m_audio.playSound(fall, 1.0f, 1.0f, a);
            });
        }
    }
}

void Application::update(float dt) {
    // The world keeps running while the player sleeps (S5.8).
    if (!m_session || (m_screenState != ui::ScreenState::InGame && m_screenState != ui::ScreenState::Sleeping)) {
        m_audio.setRainLoop(0.0f); // Paused or in a menu: the rain falls silent
        return;
    }
    m_inWorld.state.statusMessageSeconds = std::max(0.0f, m_inWorld.state.statusMessageSeconds - dt);

    m_inWorld.state.totalTime += dt;
    // Before this frame's sounds start, so they are ray-marched from a current listener.
    m_inWorld.acoustics.update(static_cast<double>(m_inWorld.state.totalTime), player().getInterpolatedEyePosition(m_renderAlpha), world());
    if (m_inWorld.state.placeCooldown > 0.0f) {
        m_inWorld.state.placeCooldown = std::max(0.0f, m_inWorld.state.placeCooldown - dt);
    }

    constexpr float FIXED_DT = 1.0f / 60.0f;
    constexpr float MAX_ACCUMULATOR = 0.25f;

    m_inWorld.state.accumulator = std::min(m_inWorld.state.accumulator + dt, MAX_ACCUMULATOR);
    while (m_inWorld.state.accumulator >= FIXED_DT) {
        fixedUpdate(FIXED_DT);
        m_inWorld.state.accumulator -= FIXED_DT;
    }

    // Beta's 20 TPS game tick runs on its own clock: physics steps at 60 Hz.
    m_inWorld.state.gameTickAccumulator = std::min(m_inWorld.state.gameTickAccumulator + dt, MAX_ACCUMULATOR);
    while (m_inWorld.state.gameTickAccumulator >= core::EngineConfig::GAME_TICK_SECONDS) {
        gameTick();
        m_inWorld.state.gameTickAccumulator -= core::EngineConfig::GAME_TICK_SECONDS;
    }

    m_renderAlpha = m_inWorld.state.accumulator / FIXED_DT;
    player().setRenderAlpha(m_renderAlpha);

    // Stream chunks around player: S7.7 disk reads and pass 1 off the main thread, then at
    // most kIntegrationBudget of integration per frame (--no-async-gen: 2 chunks in place).
    m_session->streamAround(streamingRadius());

    // Update voxel raycast from camera eye
    m_inWorld.state.currentTarget = physics::Raycaster::cast(
        world(),
        player().getInterpolatedEyePosition(m_renderAlpha),
        player().getLookDirection(),
        core::EngineConfig::REACH_DISTANCE
    );
}

void Application::collectMobInstances(float tickAlpha) {
    // Beta: limbSwing grows by 4 per block walked and the limbs swing by cos(0.6662 * limbSwing).
    constexpr float kWalkPhasePerBlock = 4.0f * 0.6662f;
    constexpr float kMaxStepPerFrame = 1.0f; // Blocks; larger jumps are teleports, not strides
    constexpr float kHurtWindowSeconds = 0.5f;
    constexpr float kPi = 3.14159265f;

    ecs::Registry& reg = sim().entities();
    m_inWorld.state.mobInstances.clear();
    m_inWorld.state.mobWalkNext.clear();
    reg.view<ecs::TransformComp, ecs::MobKindComp>().each(
        [&](ecs::EntityId e, const ecs::TransformComp& t, const ecs::MobKindComp& mob) {
            if (reg.has<ecs::PlayerTagComp>(e)) {
                return;
            }
            render::MobRenderInstance inst;
            inst.pos = glm::mix(t.prevPos, t.pos, tickAlpha);
            inst.yaw = t.yaw;
            inst.kind = mob.kind;
            if (const auto* health = reg.tryGet<ecs::HealthComp>(e)) {
                inst.hurt = std::clamp(health->hurtInvulnerabilityTimer / kHurtWindowSeconds, 0.0f, 1.0f);
            }
            if (const auto* corpse = reg.tryGet<CorpseComp>(e)) {
                inst.death = std::clamp((static_cast<float>(corpse->ticks) + tickAlpha) /
                                            static_cast<float>(gameplay::kDeathAnimationTicks), 0.0f, 1.0f);
            }
            // S6.3 creeper fuse: Beta RenderCreeper swells the body and flashes it white on
            // every other tenth of the fuse, brighter as it burns down.
            if (const auto* fuse = reg.tryGet<ecs::FuseComp>(e); fuse != nullptr && fuse->ticks > 0) {
                const float total = static_cast<float>(std::max(1, gameplay::mobDef(mob.kind).explode.fuseTicks));
                const float swell = std::clamp((static_cast<float>(fuse->ticks) +
                                                static_cast<float>(fuse->dir) * tickAlpha) / total, 0.0f, 1.0f);
                inst.swell = swell;
                inst.flash = static_cast<int>(swell * 10.0f) % 2 == 1 ? std::min(1.0f, 0.3f + swell * 0.5f) : 0.0f;
            }

            MobWalkState walk{0.0f, inst.pos};
            if (const auto it = m_inWorld.state.mobWalk.find(e); it != m_inWorld.state.mobWalk.end()) {
                walk.phase = it->second.phase;
                const glm::vec2 step(inst.pos.x - it->second.lastPos.x, inst.pos.z - it->second.lastPos.z);
                const float dist = glm::length(step);
                if (dist > 1e-4f && dist < kMaxStepPerFrame && inst.death <= 0.0f) {
                    walk.phase = std::fmod(walk.phase + dist * kWalkPhasePerBlock, 2.0f * kPi);
                } else {
                    // Standing still: the limbs settle back to vertical.
                    const float rest = std::round(walk.phase / kPi) * kPi;
                    walk.phase += (rest - walk.phase) * 0.2f;
                }
            }
            inst.walkPhase = walk.phase;
            m_inWorld.state.mobWalkNext.emplace(e, walk);
            m_inWorld.state.mobInstances.push_back(inst);
        });
    m_inWorld.state.mobWalk.swap(m_inWorld.state.mobWalkNext);
}

void Application::renderFrame(bool present) {
    // Drawable size, not window size: they differ on HiDPI, and both glViewport and the
    // dynamic-light tiles (gl_FragCoord) work in framebuffer pixels.
    int framebufferWidth = m_config.windowWidth;
    int framebufferHeight = m_config.windowHeight;
    SDL_GL_GetDrawableSize(m_window, &framebufferWidth, &framebufferHeight);

    if (!m_session) {
        // Title and world menus with no world open (S7.9).
        clearFrame(framebufferWidth, framebufferHeight);
    } else {
        renderWorldFrame(framebufferWidth, framebufferHeight);
    }
    renderScreens();

    if (present) {
        SDL_GL_SwapWindow(m_window);
    }
}

void Application::renderWorldFrame(int framebufferWidth, int framebufferHeight) {
    // Entities move on the 20 TPS game tick, so they interpolate with that clock's alpha.
    const float tickAlpha = std::clamp(m_inWorld.state.gameTickAccumulator / core::EngineConfig::GAME_TICK_SECONDS, 0.0f, 1.0f);
    player().setTickAlpha(tickAlpha); // The hurt camera roll runs on the game tick
    collectDynamicLights(m_inWorld.state.dynamicLights, player(), &sim().entities(), tickAlpha);
    m_inWorld.state.itemInstances.clear();
    sim().entities().view<ecs::TransformComp, ecs::ItemDropComp>().each(
        [&](ecs::EntityId e, const ecs::TransformComp& t, const ecs::ItemDropComp& item) {
            const float age = item.ageSeconds + tickAlpha * core::EngineConfig::GAME_TICK_SECONDS;
            m_inWorld.state.itemInstances.push_back({glm::mix(t.prevPos, t.pos, tickAlpha), item.id, age,
                                       static_cast<float>(ecs::entityIndex(e) % 628U) * 0.01f});
        });
    collectMobInstances(tickAlpha);
    m_inWorld.state.fallingBlockInstances.clear();
    sim().entities().view<ecs::TransformComp, ecs::FallingBlockComp>().each(
        [&](ecs::EntityId, const ecs::TransformComp& t, const ecs::FallingBlockComp& falling) {
            m_inWorld.state.fallingBlockInstances.push_back({glm::mix(t.prevPos, t.pos, tickAlpha), falling.id});
        });
    // S6.6 primed TNT: the block itself, blinking white over the last kTntFlashTicks.
    sim().entities().view<ecs::TransformComp, ecs::PrimedTntComp>().each(
        [&](ecs::EntityId, const ecs::TransformComp& t, const ecs::PrimedTntComp& tnt) {
            const float flash = tnt.fuse <= kTntFlashTicks && (tnt.fuse / 2) % 2 == 0 ? 0.8f : 0.0f;
            m_inWorld.state.fallingBlockInstances.push_back({glm::mix(t.prevPos, t.pos, tickAlpha), world::BlockId::TNT, flash});
        });
    // S6.2 arrows, along their flight.
    m_inWorld.state.arrowInstances.clear();
    sim().entities().view<ecs::TransformComp, ecs::ArrowComp>().each(
        [&](ecs::EntityId, const ecs::TransformComp& t, const ecs::ArrowComp&) {
            m_inWorld.state.arrowInstances.push_back({glm::mix(t.prevPos, t.pos, tickAlpha), t.yaw, t.pitch});
        });
    // The crack belongs to the block being mined, not to whatever the crosshair crossed since.
    const bool crackOnTarget = m_inWorld.state.currentTarget.hit && sim().miningTarget() &&
                               *sim().miningTarget() == m_inWorld.state.currentTarget.blockPos;

    // S8.10 / S8.11 weather of this frame, interpolated on the game tick.
    m_worldRenderer.setWeather(render::WeatherFrame{
        .rainStrength = world::rainStrengthAt(m_session->weather(), tickAlpha),
        .thunderStrength = world::thunderStrengthAt(m_session->weather(), tickAlpha),
        .lightningFlash = static_cast<float>(m_inWorld.state.lightningTicks) / static_cast<float>(kLightningFlashTicks),
        .bolt = m_inWorld.state.lightningBolt,
        .boltSeed = m_inWorld.state.lightningSeed});
    m_inWorld.state.lastRenderStats = m_worldRenderer.renderWorld(
        world(),
        player(),
        m_inWorld.state.currentTarget,
        crackOnTarget ? sim().crackStage() : -1,
        m_inWorld.state.particles,
        m_config,
        m_session->timeOfDay(tickAlpha), // Advanced per game tick, smoothed between them
        m_inWorld.state.totalTime,
        framebufferWidth,
        framebufferHeight,
        m_inWorld.state.dynamicLights,
        m_inWorld.state.itemInstances,
        m_inWorld.state.mobInstances,
        m_inWorld.state.fallingBlockInstances,
        m_inWorld.state.arrowInstances
    );
}

void Application::renderScreens() {
    const ui::HudCanvas canvas = hudCanvas();
    if (m_session && (m_screenState == ui::ScreenState::InGame || m_screenState == ui::ScreenState::Sleeping)) {
        const ui::HudDebugContext hudCtx{
            .fps = m_smoothedFps,
            .frameTimeMs = m_smoothedFrameMs,
            .timeOfDay = m_session->timeOfDay(),
            .showDebug = m_showDebug,
            .inventoryOpen = m_inventoryOpen,
            .creative = player().isCreative(),
            .musicEnabled = m_audio.isMusicEnabled(),
            .mouseX = canvas.toCanvasX(m_mouseX),
            .mouseY = canvas.toCanvasY(m_mouseY),
            .renderStats = m_inWorld.state.lastRenderStats,
            .targetHit = m_inWorld.state.currentTarget,
            .crafting = craftingScreenView(m_session->crafting(), world()),
            .hurtFlash = gameplay::hurtFlashIntensity(player().health()),
            // Beta GuiIngame: the hearts flash white every 3 ticks while hits are resisted.
            .heartsBlink = player().health().isInvulnerable() && (player().health().invulnerableTicks / 3) % 2 == 1,
            .lowHealth = player().getHealth() <= 4,
            .burning = player().burning().isBurning(),
            .jitterSeed = m_session->gameTicks() * 312871U, // Beta rand.setSeed(updateCounter * 312871)
            .mobs = {.passive = sim().spawner().stats().passive,
                     .hostile = sim().spawner().stats().hostile,
                     .passiveCap = sim().spawner().stats().passiveCap,
                     .hostileCap = sim().spawner().stats().hostileCap},
            .statusMessage = m_inWorld.state.statusMessageSeconds > 0.0f ? m_inWorld.state.statusMessage : std::string{},
            .statusAlpha = std::clamp(m_inWorld.state.statusMessageSeconds, 0.0f, 1.0f),
            .sleepFade = m_session->sleepFade(),
            .storage = hudStorageStats(world()),
            .weather = m_showDebug ? weatherDebugLine() : std::string{}
        };

        m_hudRenderer.render(
            canvas.width,
            canvas.height,
            player(),
            world(),
            m_worldRenderer.getAtlas(),
            m_config,
            hudCtx
        );
    } else if (m_screenState == ui::ScreenState::TitleScreen) {
        std::vector<ui::UiQuadVertex> verts;
        m_titleScreen.render(
            m_config.windowWidth,
            m_config.windowHeight,
            verts,
            static_cast<float>(m_mouseX),
            static_cast<float>(m_mouseY)
        );
        const glm::mat4 ortho = glm::ortho(
            0.0f, static_cast<float>(m_config.windowWidth),
            static_cast<float>(m_config.windowHeight), 0.0f,
            -1.0f, 1.0f
        );
        m_textRenderer.renderBatch(verts, ortho);
    } else if (m_screenState == ui::ScreenState::OptionsMenu || m_screenState == ui::ScreenState::KeybindingMenu) {
        std::vector<ui::UiQuadVertex> verts;
        m_optionsMenu.render(
            m_config.windowWidth,
            m_config.windowHeight,
            verts,
            static_cast<float>(m_mouseX),
            static_cast<float>(m_mouseY),
            m_screenState,
            m_inputManager
        );
        const glm::mat4 ortho = glm::ortho(
            0.0f, static_cast<float>(m_config.windowWidth),
            static_cast<float>(m_config.windowHeight), 0.0f,
            -1.0f, 1.0f
        );
        m_textRenderer.renderBatch(verts, ortho);
    } else if (m_screenState == ui::ScreenState::DeathScreen) {
        std::vector<ui::UiQuadVertex> verts;
        m_deathScreen.render(canvas.width, canvas.height, verts, static_cast<float>(canvas.toCanvasX(m_mouseX)),
                             static_cast<float>(canvas.toCanvasY(m_mouseY)));
        const glm::mat4 ortho = glm::ortho(
            0.0f, static_cast<float>(canvas.width),
            static_cast<float>(canvas.height), 0.0f,
            -1.0f, 1.0f
        );
        m_textRenderer.renderBatch(verts, ortho);
    } else if (m_screenState == ui::ScreenState::PauseMenu) {
        std::vector<ui::UiQuadVertex> verts;
        const float sw = static_cast<float>(m_config.windowWidth);
        const float sh = static_cast<float>(m_config.windowHeight);

        // Dark translucent tint
        const ui::UiQuadVertex v0{{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.65f}, 0.0f};
        const ui::UiQuadVertex v1{{sw, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.65f}, 0.0f};
        const ui::UiQuadVertex v2{{sw, sh}, {0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.65f}, 0.0f};
        const ui::UiQuadVertex v3{{0.0f, sh}, {0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.65f}, 0.0f};
        verts.push_back(v0); verts.push_back(v1); verts.push_back(v2);
        verts.push_back(v0); verts.push_back(v2); verts.push_back(v3);

        const std::string pauseTitle = "Game Paused";
        const float tw = render::TextRenderer::measureTextWidth(pauseTitle, 2.5f);
        render::TextRenderer::appendText(verts, pauseTitle, (sw - tw) * 0.5f, sh * 0.22f, 2.5f, glm::vec4(1.0f, 0.95f, 0.65f, 1.0f), true);

        const float btnW = 280.0f;
        const float btnH = 38.0f;
        const float btnX = (sw - btnW) * 0.5f;
        const float startY = sh * 0.35f;

        auto drawPauseBtn = [&](float by, const char* label) {
            const bool hov = (m_mouseX >= btnX && m_mouseX <= btnX + btnW && m_mouseY >= by && m_mouseY <= by + btnH);
            const glm::vec4 bcol = hov ? glm::vec4(0.42f, 0.42f, 0.50f, 1.0f) : glm::vec4(0.22f, 0.22f, 0.25f, 1.0f);
            const ui::UiQuadVertex b0{{btnX, by}, {0.0f, 0.0f}, bcol, 0.0f};
            const ui::UiQuadVertex b1{{btnX + btnW, by}, {0.0f, 0.0f}, bcol, 0.0f};
            const ui::UiQuadVertex b2{{btnX + btnW, by + btnH}, {0.0f, 0.0f}, bcol, 0.0f};
            const ui::UiQuadVertex b3{{btnX, by + btnH}, {0.0f, 0.0f}, bcol, 0.0f};
            verts.push_back(b0); verts.push_back(b1); verts.push_back(b2);
            verts.push_back(b0); verts.push_back(b2); verts.push_back(b3);

            const float lw = render::TextRenderer::measureTextWidth(label, 1.5f);
            render::TextRenderer::appendText(verts, label, btnX + (btnW - lw) * 0.5f, by + (btnH - 15.0f) * 0.5f, 1.5f, hov ? glm::vec4(1.0f, 1.0f, 0.6f, 1.0f) : glm::vec4(0.9f, 0.9f, 0.9f, 1.0f), true);
        };

        drawPauseBtn(startY, "Back to Game");
        drawPauseBtn(startY + 50.0f, "Options...");
        drawPauseBtn(startY + 100.0f, "Save and Quit to Title");

        const glm::mat4 ortho = glm::ortho(
            0.0f, static_cast<float>(m_config.windowWidth),
            static_cast<float>(m_config.windowHeight), 0.0f,
            -1.0f, 1.0f
        );
        m_textRenderer.renderBatch(verts, ortho);
    } else if (m_screenState == ui::ScreenState::WorldSelect || m_screenState == ui::ScreenState::CreateWorld ||
               m_screenState == ui::ScreenState::Loading) {
        std::vector<ui::UiQuadVertex> verts;
        const float mx = static_cast<float>(m_mouseX);
        const float my = static_cast<float>(m_mouseY);
        if (m_screenState == ui::ScreenState::WorldSelect) {
            m_worldSelect.render(m_config.windowWidth, m_config.windowHeight, verts, mx, my);
        } else if (m_screenState == ui::ScreenState::CreateWorld) {
            // Beta GuiTextField: the caret blinks every 6 of 20 ticks.
            const bool caret = (SDL_GetTicks() / 300U) % 2U == 0U;
            m_createWorld.render(m_config.windowWidth, m_config.windowHeight, verts, mx, my, caret);
        } else {
            ui::appendLoadingScreen(verts, m_config.windowWidth, m_config.windowHeight, "Loading level", "", std::nullopt);
        }
        const glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(m_config.windowWidth),
                                           static_cast<float>(m_config.windowHeight), 0.0f, -1.0f, 1.0f);
        m_textRenderer.renderBatch(verts, ortho);
    }
}

int Application::run(int maxFrames) {
    if (maxFrames >= 0 && m_session) {
        enterGame();
    }

    Uint64 prevTicks = SDL_GetPerformanceCounter();
    const double perfFreq = static_cast<double>(SDL_GetPerformanceFrequency());
    int frameCounter = 0;

    while (m_running) {
        const Uint64 nowTicks = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(static_cast<double>(nowTicks - prevTicks) / perfFreq);
        prevTicks = nowTicks;
        dt = std::clamp(dt, 0.0005f, 0.1f);

        if (!runFrame(dt, true)) break;

        if (maxFrames > 0 && ++frameCounter >= maxFrames) {
            core::Logger::info("Application", "Completed requested ", maxFrames, " frames cleanly.");
            break;
        }
    }

    return 0;
}

int Application::runFixedFrames(int frames, float frameDt) {
    if (m_session) {
        enterGame();
    }
    for (int i = 0; i < frames && m_running; ++i) {
        if (!runFrame(frameDt, false)) break;
    }
    return 0;
}

void Application::enterGame() {
    if (!m_session) {
        return;
    }
    if (m_session->awaitingGameMode()) {
        m_session->startNewWorld(m_titleScreen.selectedMode());
        m_titleScreen.setNewWorld(false);
    }
    if (player().isDead()) {
        // Saved while dead (death screen, then "Title menu"): respawn from the death screen.
        m_screenState = ui::ScreenState::DeathScreen;
        setMouseCaptured(false);
        return;
    }
    m_screenState = ui::ScreenState::InGame;
    setMouseCaptured(true);
}

void Application::leaveWorld() {
    if (!m_session) {
        return;
    }
    // No mesh job may still read the World; then level.dat, the modified chunks and every
    // queued chunk on disk; then the session, and everything of the world with it.
    m_worldRenderer.releaseWorld();
    if (!m_session->close()) {
        core::Logger::error("Application", "Some chunks of ", m_config.worldSaveDir, " could not be written.");
    }
    m_session.reset();

    // Nothing of this world may leak into the next one: the effects and the screens' state.
    m_inWorld.reset(); // effects, render instances, timers, input latches
    m_inventoryOpen = false;
    m_audio.setRainLoop(0.0f);
    m_worldRenderer.setWeather({});

    m_titleScreen.setNewWorld(false);
    m_titleScreen.setWorldLoaded(false);
    m_screenState = ui::ScreenState::TitleScreen;
    setMouseCaptured(false);
}

void Application::openWorldSelect() {
    m_worldSelect.refresh(m_config.savesRoot);
    m_screenState = ui::ScreenState::WorldSelect;
}

void Application::openCreateWorld() {
    m_createWorld.setSavesRoot(m_config.savesRoot);
    m_createWorld.reset(m_config.defaultGameMode);
    m_screenState = ui::ScreenState::CreateWorld;
    SDL_StartTextInput();
}

void Application::playSelectedWorld() {
    const ui::WorldSummary* selected = m_worldSelect.selectedWorld();
    if (!selected) {
        return;
    }
    const std::filesystem::path dir = selected->path;
    if (enterWorld(dir)) {
        syncRuntimeOptions();
        enterGame();
    } else {
        openWorldSelect();
    }
}

void Application::createWorldFromScreen() {
    std::error_code ec;
    std::filesystem::create_directories(m_config.savesRoot, ec);
    const std::filesystem::path dir = m_createWorld.folder(m_config.savesRoot);
    // Beta: new Random().nextLong() for an empty seed field.
    const uint64_t randomSeed = gameplay::SplitMix64(static_cast<uint64_t>(SDL_GetPerformanceCounter()) ^
                                                     static_cast<uint64_t>(std::random_device{}()))
                                    .next();
    if (enterWorld(dir, m_createWorld.params(randomSeed))) {
        syncRuntimeOptions();
        enterGame();
    } else {
        openWorldSelect();
    }
}

void Application::presentLoadingFrame(std::string_view title, std::string_view status, std::optional<float> progress) {
    if (!m_window || !m_glContext) {
        return;
    }
    SDL_PumpEvents(); // Keeps the window responsive while the world opens
    int framebufferWidth = m_config.windowWidth;
    int framebufferHeight = m_config.windowHeight;
    SDL_GL_GetDrawableSize(m_window, &framebufferWidth, &framebufferHeight);
    clearFrame(framebufferWidth, framebufferHeight);
    std::vector<ui::UiQuadVertex> verts;
    ui::appendLoadingScreen(verts, m_config.windowWidth, m_config.windowHeight, title, status, progress);
    const glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(m_config.windowWidth),
                                       static_cast<float>(m_config.windowHeight), 0.0f, -1.0f, 1.0f);
    m_textRenderer.renderBatch(verts, ortho);
    SDL_GL_SwapWindow(m_window);
}

void Application::clearFrame(int framebufferWidth, int framebufferHeight) {
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Application::saveWorld() {
    if (!m_session) {
        return;
    }
    // Stacks the grid held that the inventory could not take were thrown in front of the
    // player and the grid closed: the screen goes with it.
    if (m_session->save().closedCrafting) {
        m_inventoryOpen = false;
        setMouseCaptured(true);
    }
}

void Application::handlePlayerDeath() {
    // The session already emptied the grid and the cursor and scattered everything.
    m_inventoryOpen = false;
    m_inWorld.state.leftMouseDown = false;
    m_inWorld.state.attackPressed = false;
    m_screenState = ui::ScreenState::DeathScreen;
    setMouseCaptured(false);
}

void Application::respawnPlayer() {
    if (!m_session) {
        return;
    }
    // S5.8: next to the bed slept in, or back to the world spawn when it is gone.
    if (m_session->respawn().bedMissing) {
        showStatusMessage("Your home bed was missing or obstructed");
    }
    m_screenState = ui::ScreenState::InGame;
    setMouseCaptured(true);
}

bool Application::runFrame(float dt, bool present) {
    const float instantFps = 1.0f / dt;
    m_smoothedFps = glm::mix(m_smoothedFps, instantFps, 0.08f);
    m_smoothedFrameMs = glm::mix(m_smoothedFrameMs, dt * 1000.0f, 0.08f);

    processEvents();
    if (!m_running) return false;

    update(dt);
    renderFrame(present);
    return true;
}

int32_t Application::streamingRadius() const noexcept {
    // Only decorated chunks are meshed and pass 2 needs the 3x3 around a chunk, so one extra
    // ring keeps the rendered radius at renderDistanceChunks.
    return m_config.renderDistanceChunks + 1;
}

void Application::shutdown() noexcept {
    // Meshing workers and section meshes go first, while the World and GL context exist.
    m_worldRenderer.shutdown();
    leaveWorld(); // Saved and flushed (S7.6: unload saves still queued included)

    m_audio.shutdown();

    if (m_glContext) {
        SDL_GL_DeleteContext(m_glContext);
        m_glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}

} // namespace betacraft::game
