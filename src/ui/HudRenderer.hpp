#pragma once

#include "core/Config.hpp"
#include "core/Types.hpp"
#include "game/ScreenKind.hpp"
#include "physics/Player.hpp"
#include "physics/Raycaster.hpp"
#include "render/GLHeaders.hpp"
#include "render/Shader.hpp"
#include "render/TextureAtlas.hpp"
#include "render/WorldRenderer.hpp"
#include "ui/BitmapFont.hpp"
#include "ui/ScreenState.hpp"
#include "world/World.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace betacraft::ui {

// What the inventory screen shows besides the player's own slots (game::InventoryCrafting).
struct CraftingScreenView {
    int gridDim{2}; // 2: inventory grid, 3: workbench
    std::array<physics::InventorySlot, 9> grid{};
    physics::InventorySlot result{};
    physics::InventorySlot carried{}; // Drawn under the mouse cursor
    // S2.6: a furnace (container[0..2]: input, fuel, output) or a chest (27 slots) replaces
    // the grid and the palette.
    game::ScreenKind kind{game::ScreenKind::Inventory2x2};
    std::array<physics::InventorySlot, 27> container{};
    float cookProgress{0.0f}; // Furnace arrow fill, 0..1
    float burnProgress{0.0f}; // Furnace flame fill, 0..1
};

// Outside (S3.4): the cursor is off the dialog panel; only hitTestInventoryScreen reports it.
// Armor (S6.7): the 4 armor slots left of the player inventory, index = gameplay::ArmorSlot;
// only on the player's own inventory screen (Inventory2x2).
enum class InventoryRegion : uint8_t { None, Palette, Inventory, CraftingGrid, CraftingResult, Container, Outside, Armor };

// What decides the inventory screen layout. A container screen has no grid, no result slot
// and no palette; its slots sit where the palette is drawn otherwise.
struct InventoryLayoutKey {
    int gridDim{2};
    game::ScreenKind kind{game::ScreenKind::Inventory2x2};
    bool palette{true}; // Creative palette drawn (and hit); ignored on container screens
};

struct InventoryHit {
    InventoryRegion region{InventoryRegion::None};
    size_t index{0};
};

struct SlotRect {
    float x{0.0f};
    float y{0.0f};
    float size{0.0f};
};

// S3.7 GUI Scale: the HUD, the inventory and the death screen are laid out in a virtual
// canvas of (window / scale) points, stretched over the window by the orthographic matrix.
// scale = TextRenderer::calculateGuiScale / 3, so Auto at 1280x720 (guiScale 3) is 1.0 and
// the canvas is the window itself.
struct HudCanvas {
    int32_t width{0};  // Canvas size (rounded window / scale), what layout code receives
    int32_t height{0};
    float scale{1.0f};
    int32_t windowWidth{0};
    int32_t windowHeight{0};

    // A window point (mouse) in canvas points, with the exact ratio the ortho matrix uses.
    [[nodiscard]] int32_t toCanvasX(int32_t windowX) const noexcept {
        return windowWidth > 0 ? static_cast<int32_t>(static_cast<int64_t>(windowX) * width / windowWidth) : windowX;
    }
    [[nodiscard]] int32_t toCanvasY(int32_t windowY) const noexcept {
        return windowHeight > 0 ? static_cast<int32_t>(static_cast<int64_t>(windowY) * height / windowHeight)
                                : windowY;
    }
};

// Mob numbers and caps for the F3 overlay (game::MobSpawner::stats, S4.10).
struct HudMobStats {
    int32_t passive{0};
    int32_t hostile{0};
    int32_t passiveCap{0};
    int32_t hostileCap{0};
};

// F3 line of the mobs: "Mobs: <passive>/<cap> passive  <hostile>/<cap> hostile  Feet light: <n>",
// `feetLight` being the effective light (gameplay::effectiveLight) of the feet cell.
[[nodiscard]] std::string mobDebugLine(const HudMobStats& mobs, int32_t feetLight);

// Chunk storage numbers for the F3 overlay (world::World::getStorageStats, S7.5).
struct HudStorageStats {
    uint64_t bundlesWritten{0};
    // Bundles written for a chunk with no player edit in its 3x3 neighbourhood: must stay 0
    // (bug 8, fluid simulation alone never saves a chunk).
    uint64_t unsolicitedWrites{0};
    // S7.10 streaming (world::World::getStreamingStats): chunks waiting on each stage, the
    // slowest per-frame integration of the last couple of seconds and the open region files.
    uint32_t generating{0};
    uint32_t integrating{0};
    uint32_t writing{0};
    float lastIntegrationMs{0.0f};
    float maxIntegrationMs{0.0f};
    uint32_t openRegions{0};
    uint64_t openRegionBytes{0};
    uint32_t loading{0}; // Disk reads not answered yet
};

// F3 line of the storage: "Storage: <n> bundles written  <m> without player edit".
[[nodiscard]] std::string storageDebugLine(const HudStorageStats& storage);
// S7.10 F3 line: "Chunks: <r> read  <g> gen  <i> integrate  <w> write  Integration:
// <last>/<max> ms  Regions: <n> (<kib> KiB)".
[[nodiscard]] std::string streamingDebugLine(const HudStorageStats& storage);
// Above this, integration made a frame miss the Sprint 7 target (no hitch over 8 ms): red.
inline constexpr float kIntegrationHitchMs = 8.0f;

struct HudDebugContext {
    float fps{60.0f};
    float frameTimeMs{16.6f};
    float timeOfDay{0.25f};
    bool showDebug{false};
    bool inventoryOpen{false};
    bool creative{false}; // Palette, Creative help line; hearts and bubbles only in Survival
    bool musicEnabled{false};
    int32_t mouseX{0};
    int32_t mouseY{0};
    render::RenderStats renderStats{};
    physics::RaycastHit targetHit{};
    CraftingScreenView crafting{};
    // S3.2 hurt feedback (Survival)
    float hurtFlash{0.0f};   // gameplay::hurtFlashIntensity: red veil of 0.25 * hurtFlash
    bool heartsBlink{false}; // Hearts outlined in white (invulnerability blink)
    bool lowHealth{false};   // Health <= 4: the hearts jitter
    bool burning{false};     // Orange veil
    uint32_t jitterSeed{0};  // Changes every game tick, so the jitter moves at 20 Hz
    HudMobStats mobs{};      // F3 mob counts and caps
    // S5.8: a one-line notice above the hotbar (empty: none), faded by statusAlpha 0..1, and
    // the black veil of the sleep screen (0..1).
    std::string statusMessage{};
    float statusAlpha{0.0f};
    float sleepFade{0.0f};
    HudStorageStats storage{}; // F3 chunk storage counters (S7.5)
    std::string weather{};     // S8.9 F3 weather line (empty: not shown)
};

class HudRenderer : private core::NonCopyable {
public:
    HudRenderer() noexcept = default;
    ~HudRenderer() noexcept;

    [[nodiscard]] bool initialize();

    void render(
        int32_t screenWidth,
        int32_t screenHeight,
        const physics::Player& player,
        const world::World& world,
        const render::TextureAtlas& atlas,
        const core::EngineConfig& config,
        const HudDebugContext& ctx
    );

    // Creative palette clicks: equips the block in the selected hotbar slot and returns true.
    // Inventory, grid and result slots belong to game::InventoryCrafting (hitTestInventory).
    bool handleInventoryClick(
        int32_t mouseX,
        int32_t mouseY,
        int32_t screenWidth,
        int32_t screenHeight,
        physics::Player& player
    );

    // Inventory screen layout (window points), shared by drawing and click mapping.
    [[nodiscard]] static SlotRect inventorySlotRect(InventoryRegion region, size_t index, int32_t screenWidth,
                                                    int32_t screenHeight, InventoryLayoutKey layout) noexcept;
    // Grid screens only (2x2 inventory or 3x3 workbench).
    [[nodiscard]] static SlotRect inventorySlotRect(InventoryRegion region, size_t index, int32_t screenWidth,
                                                    int32_t screenHeight, int gridDim) noexcept;
    [[nodiscard]] static InventoryHit hitTestInventory(int32_t mouseX, int32_t mouseY, int32_t screenWidth,
                                                       int32_t screenHeight, InventoryLayoutKey layout) noexcept;
    // `withPalette` false (Survival): the palette is not drawn, so it is never hit either.
    [[nodiscard]] static InventoryHit hitTestInventory(int32_t mouseX, int32_t mouseY, int32_t screenWidth,
                                                       int32_t screenHeight, int gridDim,
                                                       bool withPalette = true) noexcept;

    // Same as hitTestInventory, but a point off the dialog panel reports Outside instead of
    // None (Beta drops the cursor stack there); None stays for gaps inside the panel.
    [[nodiscard]] static InventoryHit hitTestInventoryScreen(int32_t mouseX, int32_t mouseY, int32_t screenWidth,
                                                             int32_t screenHeight, InventoryLayoutKey layout) noexcept;

    // S3.7: TextRenderer::calculateGuiScale(w, h, pref) / 3.
    [[nodiscard]] static float hudScale(int32_t windowWidth, int32_t windowHeight, GuiScalePreference pref) noexcept;
    [[nodiscard]] static HudCanvas hudCanvas(int32_t windowWidth, int32_t windowHeight,
                                             GuiScalePreference pref) noexcept;

    // S3.6: the lines of the tooltip shown when hovering `slot` with an empty cursor: the item
    // name, then "remaining / max" durability for a tool. Empty for an empty slot.
    [[nodiscard]] static std::vector<std::string> inventoryTooltip(const physics::InventorySlot& slot);

private:
    render::ShaderProgram m_uiShader;
    GLuint m_vao{0};
    GLuint m_vbo{0};
};

} // namespace betacraft::ui
