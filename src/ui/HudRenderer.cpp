#include "ui/HudRenderer.hpp"
#include "gameplay/Armor.hpp"
#include "gameplay/ItemStack.hpp"
#include "gameplay/MobSpawnRules.hpp"
#include "gameplay/ToolDefinitions.hpp"
#include "render/ItemAppearance.hpp"
#include "render/TextRenderer.hpp"
#include "ui/CreativePalette.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace betacraft::ui {

namespace {

constexpr const char* UI_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aUseTexture;

uniform mat4 uOrtho;

out vec2 vUV;
out vec4 vColor;
out float vUseTexture;

void main() {
    vUV = aUV;
    vColor = aColor;
    vUseTexture = aUseTexture;
    gl_Position = uOrtho * vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char* UI_FS = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
in float vUseTexture;

uniform sampler2D uAtlas;

out vec4 FragColor;

void main() {
    if (vUseTexture > 0.5) {
        vec4 tex = texture(uAtlas, vUV);
        if (tex.a < 0.25) {
            discard;
        }
        FragColor = tex * vColor;
    } else {
        FragColor = vColor;
    }
}
)";

constexpr float kPanelW = 720.0f;
constexpr float kPanelH = 460.0f;
constexpr float kSlot = 38.0f;
constexpr float kResultSlot = 48.0f;

[[nodiscard]] constexpr ScreenKind gridKind(int gridDim) noexcept {
    return gridDim >= 3 ? ScreenKind::Workbench3x3 : ScreenKind::Inventory2x2;
}

[[nodiscard]] size_t containerSlotCount(ScreenKind kind) noexcept {
    switch (kind) {
        case ScreenKind::Furnace: return kFurnaceSlotCount;
        case ScreenKind::Chest: return 27;
        default: return 0;
    }
}

void pushRect(
    std::vector<render::UiQuadVertex>& out,
    float x0,
    float y0,
    float x1,
    float y1,
    const glm::vec4& col
) {
    out.push_back({{x0, y0}, {0.0f, 0.0f}, col, 0.0f});
    out.push_back({{x1, y0}, {0.0f, 0.0f}, col, 0.0f});
    out.push_back({{x1, y1}, {0.0f, 0.0f}, col, 0.0f});
    out.push_back({{x0, y0}, {0.0f, 0.0f}, col, 0.0f});
    out.push_back({{x1, y1}, {0.0f, 0.0f}, col, 0.0f});
    out.push_back({{x0, y1}, {0.0f, 0.0f}, col, 0.0f});
}

void pushBeveledSlot(
    std::vector<render::UiQuadVertex>& out,
    float x,
    float y,
    float size,
    bool highlighted
) {
    const glm::vec4 outerCol = highlighted
        ? glm::vec4(0.96f, 0.96f, 0.85f, 0.98f)
        : glm::vec4(0.22f, 0.22f, 0.24f, 0.92f);
    const glm::vec4 innerCol = highlighted
        ? glm::vec4(0.45f, 0.45f, 0.48f, 0.92f)
        : glm::vec4(0.34f, 0.34f, 0.36f, 0.88f);

    pushRect(out, x, y, x + size, y + size, outerCol);
    pushRect(out, x + 2.0f, y + 2.0f, x + size - 2.0f, y + size - 2.0f, innerCol);
}

void pushItemIcon(
    std::vector<render::UiQuadVertex>& out,
    gameplay::ItemId id,
    float x,
    float y,
    float size
) {
    if (id == gameplay::kNoItem) return;
    const render::ItemAppearance look = render::itemAppearance(id);
    const uint8_t tile = look.iconTile();
    const glm::vec4 uv = render::TextureAtlas::getTileUV(tile);

    glm::vec4 tint(look.tint, 1.0f);
    if (look.tintGrassTop && tile == 0) {
        tint = glm::vec4(0.52f, 0.80f, 0.34f, 1.0f);
    } else if (look.tintFoliage) {
        tint = glm::vec4(0.42f, 0.72f, 0.26f, 1.0f);
    }

    out.push_back({{x, y},               {uv.x, uv.y}, tint, 1.0f});
    out.push_back({{x + size, y},        {uv.z, uv.y}, tint, 1.0f});
    out.push_back({{x + size, y + size}, {uv.z, uv.w}, tint, 1.0f});
    out.push_back({{x, y},               {uv.x, uv.y}, tint, 1.0f});
    out.push_back({{x + size, y + size}, {uv.z, uv.w}, tint, 1.0f});
    out.push_back({{x, y + size},        {uv.x, uv.w}, tint, 1.0f});
}

// Item icon plus Beta's stack decorations: the count, and a durability bar on worn tools.
void pushSlotContents(std::vector<render::UiQuadVertex>& out, const physics::InventorySlot& slot, float x, float y,
                      float size, float inset, float countScale) {
    if (slot.isEmpty()) return;
    pushItemIcon(out, slot.id, x + inset, y + inset, size - 2.0f * inset);
    const gameplay::ItemStack stack = gameplay::makeStack(slot.id, 1);
    if (stack.maxDamage > 0 && slot.damage > 0) {
        const float frac = 1.0f - static_cast<float>(std::clamp(slot.damage, 0, stack.maxDamage)) /
                                      static_cast<float>(stack.maxDamage);
        const float bx0 = x + inset;
        const float bx1 = x + size - inset;
        const float by = y + size - inset - 1.0f;
        pushRect(out, bx0, by, bx1, by + 3.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.95f));
        pushRect(out, bx0, by, bx0 + (bx1 - bx0) * frac, by + 2.0f,
                 glm::vec4(1.0f - frac, frac, 0.0f, 1.0f));
    }
    if (slot.count > 1) {
        const std::string cnt = std::to_string(slot.count);
        const float cw = render::BitmapFont::measureTextWidth(cnt, countScale);
        render::BitmapFont::appendText(out, cnt, x + size - cw - 4.0f, y + size - 8.0f * countScale - 2.0f, countScale,
                               glm::vec4(1.0f));
    }
}

void pushHeartIcon(std::vector<render::UiQuadVertex>& out, float x, float y, float s, int fillState, bool blink = false) {
    // fillState: 0 = empty, 1 = half, 2 = full
    static constexpr uint8_t heartMask[7] = {
        0b0110110,
        0b1111111,
        0b1111111,
        0b1111111,
        0b0111110,
        0b0011100,
        0b0001000
    };
    for (int ry = 0; ry < 7; ++ry) {
        for (int rx = 0; rx < 7; ++rx) {
            if ((heartMask[ry] & (1U << (6 - rx))) != 0U) {
                const bool border = (ry == 0 || ry == 6 || rx == 0 || rx == 6);
                glm::vec4 col = blink ? glm::vec4(1.0f, 1.0f, 1.0f, 0.95f) : glm::vec4(0.12f, 0.12f, 0.12f, 0.92f);
                if (!border) {
                    if (fillState == 2 || (fillState == 1 && rx <= 3)) {
                        col = (rx == 2 && ry == 2)
                            ? glm::vec4(1.0f, 0.75f, 0.75f, 0.98f)
                            : glm::vec4(0.90f, 0.14f, 0.14f, 0.98f);
                    } else {
                        col = glm::vec4(0.25f, 0.08f, 0.08f, 0.85f);
                    }
                }
                pushRect(out, x + rx * s, y + ry * s, x + (rx + 1) * s, y + (ry + 1) * s, col);
            }
        }
    }
}

// S6.7 armor bar icon (a chestplate): fillState 0 = empty, 1 = half, 2 = full.
void pushArmorIcon(std::vector<render::UiQuadVertex>& out, float x, float y, float s, int fillState) {
    static constexpr uint8_t plateMask[7] = {
        0b1100011,
        0b1111111,
        0b1111111,
        0b0111110,
        0b0111110,
        0b0111110,
        0b0111110
    };
    for (int ry = 0; ry < 7; ++ry) {
        for (int rx = 0; rx < 7; ++rx) {
            if ((plateMask[ry] & (1U << (6 - rx))) == 0U) continue;
            const bool lit = fillState == 2 || (fillState == 1 && rx <= 3);
            const glm::vec4 col = lit ? glm::vec4(0.82f, 0.84f, 0.88f, 0.98f) : glm::vec4(0.18f, 0.18f, 0.20f, 0.80f);
            pushRect(out, x + rx * s, y + ry * s, x + (rx + 1) * s, y + (ry + 1) * s, col);
        }
    }
}

// One atlas tile drawn with a flat tint (the ghost outline of an empty armor slot).
void pushTileIcon(std::vector<render::UiQuadVertex>& out, uint8_t tile, float x, float y, float size, const glm::vec4& tint) {
    const glm::vec4 uv = render::TextureAtlas::getTileUV(tile);
    out.push_back({{x, y},               {uv.x, uv.y}, tint, 1.0f});
    out.push_back({{x + size, y},        {uv.z, uv.y}, tint, 1.0f});
    out.push_back({{x + size, y + size}, {uv.z, uv.w}, tint, 1.0f});
    out.push_back({{x, y},               {uv.x, uv.y}, tint, 1.0f});
    out.push_back({{x + size, y + size}, {uv.z, uv.w}, tint, 1.0f});
    out.push_back({{x, y + size},        {uv.x, uv.w}, tint, 1.0f});
}

void pushBubbleIcon(std::vector<render::UiQuadVertex>& out, float x, float y, float s) {
    pushRect(out, x + s, y, x + 5 * s, y + 6 * s, glm::vec4(0.25f, 0.65f, 0.98f, 0.90f));
    pushRect(out, x, y + s, x + 6 * s, y + 5 * s, glm::vec4(0.25f, 0.65f, 0.98f, 0.90f));
    pushRect(out, x + s, y + s, x + 3 * s, y + 3 * s, glm::vec4(0.85f, 0.96f, 1.0f, 0.95f));
}

} // namespace

std::string storageDebugLine(const HudStorageStats& storage) {
    std::ostringstream out;
    out << "Storage: " << storage.bundlesWritten << " bundles written  " << storage.unsolicitedWrites
        << " without player edit";
    return out.str();
}

std::string streamingDebugLine(const HudStorageStats& storage) {
    std::ostringstream out;
    out << "Chunks: " << storage.loading << " read  " << storage.generating << " gen  " << storage.integrating << " integrate  " << storage.writing
        << " write  Integration: " << std::fixed << std::setprecision(2) << storage.lastIntegrationMs << "/"
        << storage.maxIntegrationMs << " ms  Regions: " << storage.openRegions << " ("
        << (storage.openRegionBytes + 1023) / 1024 << " KiB)";
    return out.str();
}

std::string mobDebugLine(const HudMobStats& mobs, int32_t feetLight) {
    std::ostringstream out;
    out << "Mobs: " << mobs.passive << "/" << mobs.passiveCap << " passive  " << mobs.hostile << "/"
        << mobs.hostileCap << " hostile  Feet light: " << feetLight;
    return out.str();
}

HudRenderer::~HudRenderer() noexcept {
    if (m_vao != 0 && bc_glDeleteVertexArrays) {
        bc_glDeleteVertexArrays(1, &m_vao);
        bc_glDeleteBuffers(1, &m_vbo);
    }
}

bool HudRenderer::initialize() {
    if (!m_uiShader.compileFromSource(UI_VS, UI_FS)) {
        return false;
    }

    bc_glGenVertexArrays(1, &m_vao);
    bc_glGenBuffers(1, &m_vbo);
    bc_glBindVertexArray(m_vao);
    bc_glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    bc_glEnableVertexAttribArray(0);
    bc_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(render::UiQuadVertex), reinterpret_cast<const void*>(offsetof(render::UiQuadVertex, pos)));
    bc_glEnableVertexAttribArray(1);
    bc_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(render::UiQuadVertex), reinterpret_cast<const void*>(offsetof(render::UiQuadVertex, uv)));
    bc_glEnableVertexAttribArray(2);
    bc_glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(render::UiQuadVertex), reinterpret_cast<const void*>(offsetof(render::UiQuadVertex, color)));
    bc_glEnableVertexAttribArray(3);
    bc_glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(render::UiQuadVertex), reinterpret_cast<const void*>(offsetof(render::UiQuadVertex, useTexture)));

    bc_glBindVertexArray(0);
    return true;
}

void HudRenderer::render(
    int32_t screenWidth,
    int32_t screenHeight,
    const physics::Player& player,
    const world::World& world,
    const render::TextureAtlas& atlas,
    const core::EngineConfig& config,
    const HudDebugContext& ctx
) {
    const float sw = static_cast<float>(screenWidth);
    const float sh = static_cast<float>(screenHeight);

    std::vector<render::UiQuadVertex> verts;
    verts.reserve(16384);

    // 1. Underwater blue tint overlay
    if (player.isHeadUnderWater()) {
        pushRect(verts, 0.0f, 0.0f, sw, sh, glm::vec4(0.06f, 0.22f, 0.58f, 0.42f));
    }
    // Burning: orange veil; a fresh hit: red flash fading over gameplay::kHurtFlashTicks
    if (!ctx.creative && ctx.burning) {
        pushRect(verts, 0.0f, 0.0f, sw, sh, glm::vec4(1.0f, 0.45f, 0.05f, 0.18f));
    }
    if (!ctx.creative && ctx.hurtFlash > 0.0f) {
        pushRect(verts, 0.0f, 0.0f, sw, sh, glm::vec4(0.85f, 0.0f, 0.0f, 0.25f * std::clamp(ctx.hurtFlash, 0.0f, 1.0f)));
    }

    // S5.8 sleeping: the screen fades to black over the 100 sleep ticks (Beta GuiIngame
    // sleepTimer overlay).
    if (ctx.sleepFade > 0.0f) {
        pushRect(verts, 0.0f, 0.0f, sw, sh, glm::vec4(0.02f, 0.02f, 0.05f, 0.85f * std::clamp(ctx.sleepFade, 0.0f, 1.0f)));
    }
    if (!ctx.statusMessage.empty() && ctx.statusAlpha > 0.0f) {
        constexpr float kMsgScale = 2.0f;
        const float w = render::BitmapFont::measureTextWidth(ctx.statusMessage, kMsgScale);
        const float a = std::clamp(ctx.statusAlpha, 0.0f, 1.0f);
        const float x = (sw - w) * 0.5f;
        const float y = sh - 140.0f;
        pushRect(verts, x - 6.0f, y - 4.0f, x + w + 6.0f, y + 8.0f * kMsgScale + 4.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.45f * a));
        render::BitmapFont::appendText(verts, ctx.statusMessage, x, y, kMsgScale, glm::vec4(1.0f, 1.0f, 1.0f, a));
    }

    // 2. Iconic Top-Left "Minecraft Beta 1.7.3" Watermark & F3 Debug Screen
    constexpr float fontScale = 2.0f;
    if (!ctx.showDebug) {
        render::BitmapFont::appendText(verts, "Minecraft Beta 1.7.3", 10.0f, 10.0f, fontScale, glm::vec4(1.0f));
        render::BitmapFont::appendText(
            verts,
            ctx.creative ? "[F3] Debug  [E] Inventory/Craft  [F] Fly  [T] Time  [L] Smooth Light  [M] Music"
                         : "[F3] Debug  [E] Inventory/Craft  [L] Smooth Light  [M] Music",
            10.0f,
            30.0f,
            1.5f,
            glm::vec4(0.82f, 0.85f, 0.88f, 0.85f)
        );
    } else {
        const glm::vec3 p = player.getPosition();
        const int32_t bx = static_cast<int32_t>(std::floor(p.x));
        const int32_t by = static_cast<int32_t>(std::floor(p.y));
        const int32_t bz = static_cast<int32_t>(std::floor(p.z));
        const int32_t cx = core::floorDiv(bx, world::Chunk::WIDTH);
        const int32_t cz = core::floorDiv(bz, world::Chunk::DEPTH);
        const world::BiomeId biome = world.getBiomeAt(bx, bz);
        const uint8_t skyL = world.getSkyLight(bx, by + 1, bz);
        const uint8_t blkL = world.getBlockLight(bx, by + 1, bz);

        const uint8_t feetLight = gameplay::effectiveLight(world.getSkyLight(bx, by, bz), world.getBlockLight(bx, by, bz),
                                                           gameplay::skylightSubtracted(ctx.timeOfDay));
        std::ostringstream l1, l2, l3, l4, l5, l6;
        l1 << "Minecraft Beta 1.7.3 (" << static_cast<int>(std::round(ctx.fps)) << " fps, "
           << ctx.renderStats.visibleChunks << "/" << ctx.renderStats.totalChunks << " chunks)";
        l2 << "Vertices: " << (ctx.renderStats.totalVertices / 1000) << "k  Frame: "
           << std::fixed << std::setprecision(2) << ctx.frameTimeMs << " ms  Mesh queue: "
           << ctx.renderStats.pendingMeshJobs;
        l3 << "x: " << std::fixed << std::setprecision(2) << p.x
           << "  y: " << p.y << "  z: " << p.z << "  (Chunk " << cx << ", " << cz << ")";
        l4 << "Biome: " << world::biomeName(biome)
           << "  Light: " << static_cast<int>(skyL) << " sky / " << static_cast<int>(blkL) << " block";
        l5 << "Smooth Lighting [L]: " << (config.smoothLighting ? "ON" : "OFF")
           << "  Fly [F]: " << (player.isFlying() ? "ON" : "OFF")
           << "  Music [M]: " << (ctx.musicEnabled ? "ON" : "OFF");
        if (ctx.targetHit.hit) {
            const auto& tDef = world::BlockRegistry::get(ctx.targetHit.blockId);
            l6 << "Target: " << tDef.name << " (" << ctx.targetHit.blockPos.x << ", "
               << ctx.targetHit.blockPos.y << ", " << ctx.targetHit.blockPos.z << ")";
        } else {
            l6 << "Target: None  |  Time [T]: " << std::fixed << std::setprecision(2) << ctx.timeOfDay;
        }

        render::BitmapFont::appendText(verts, l1.str(), 10.0f, 10.0f, fontScale, glm::vec4(1.0f));
        render::BitmapFont::appendText(verts, l2.str(), 10.0f, 30.0f, fontScale, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
        render::BitmapFont::appendText(verts, l3.str(), 10.0f, 50.0f, fontScale, glm::vec4(0.95f, 0.95f, 0.7f, 1.0f));
        render::BitmapFont::appendText(verts, l4.str(), 10.0f, 70.0f, fontScale, glm::vec4(0.75f, 0.95f, 0.75f, 1.0f));
        render::BitmapFont::appendText(verts, l5.str(), 10.0f, 90.0f, fontScale, glm::vec4(0.75f, 0.88f, 1.0f, 1.0f));
        render::BitmapFont::appendText(verts, l6.str(), 10.0f, 110.0f, fontScale, glm::vec4(0.95f, 0.82f, 0.65f, 1.0f));
        render::BitmapFont::appendText(verts, mobDebugLine(ctx.mobs, feetLight), 10.0f, 130.0f, fontScale,
                               glm::vec4(0.95f, 0.7f, 0.7f, 1.0f));
        // Red as soon as a chunk was saved without any player edit around it (bug 8).
        render::BitmapFont::appendText(verts, storageDebugLine(ctx.storage), 10.0f, 150.0f, fontScale,
                               ctx.storage.unsolicitedWrites == 0 ? glm::vec4(0.8f, 0.8f, 0.95f, 1.0f)
                                                                  : glm::vec4(1.0f, 0.3f, 0.3f, 1.0f));
        // S7.10: red while an integration over the last seconds broke the 8 ms budget.
        render::BitmapFont::appendText(verts, streamingDebugLine(ctx.storage), 10.0f, 170.0f, fontScale,
                               ctx.storage.maxIntegrationMs <= kIntegrationHitchMs ? glm::vec4(0.8f, 0.9f, 0.95f, 1.0f)
                                                                                    : glm::vec4(1.0f, 0.3f, 0.3f, 1.0f));
        if (!ctx.weather.empty()) {
            render::BitmapFont::appendText(verts, ctx.weather, 10.0f, 190.0f, fontScale, glm::vec4(0.7f, 0.8f, 1.0f, 1.0f));
        }
    }

    // 3. Center Crosshair (when inventory is closed)
    if (!ctx.inventoryOpen) {
        const float cx = sw * 0.5f;
        const float cy = sh * 0.5f;
        pushRect(verts, cx - 9.0f, cy - 1.0f, cx + 9.0f, cy + 1.0f, glm::vec4(0.95f, 0.95f, 0.95f, 0.85f));
        pushRect(verts, cx - 1.0f, cy - 9.0f, cx + 1.0f, cy + 9.0f, glm::vec4(0.95f, 0.95f, 0.95f, 0.85f));
    }

    // 4. Bottom 9-slot Hotbar + Hearts + Air Bubbles
    constexpr float slotSize = 44.0f;
    constexpr float hotbarWidth = slotSize * 9.0f;
    const float hotbarX = (sw - hotbarWidth) * 0.5f;
    const float hotbarY = sh - slotSize - 10.0f;

    // 10 Hearts above left side of hotbar (Survival only, as in Beta)
    if (!ctx.creative) {
        const int32_t hp = player.getHealth();
        for (int i = 0; i < 10; ++i) {
            int fill = 0;
            if (hp >= (i + 1) * 2) fill = 2;
            else if (hp == i * 2 + 1) fill = 1;
            // Beta GuiIngame: at 4 health or less each heart drops by a random 0..1 GUI pixel,
            // reseeded every tick; here 0..2 window pixels from a per-heart hash.
            float jitter = 0.0f;
            if (ctx.lowHealth) {
                uint32_t h = ctx.jitterSeed ^ (static_cast<uint32_t>(i) * 0x9E3779B9U);
                h ^= h >> 16;
                h *= 0x7FEB352DU;
                h ^= h >> 15;
                jitter = static_cast<float>(h % 3U);
            }
            pushHeartIcon(verts, hotbarX + static_cast<float>(i) * 18.0f, hotbarY - 22.0f + jitter, 2.2f, fill,
                          ctx.heartsBlink);
        }
    }

    // S6.7 armor bar above the hearts (Beta GuiIngame): 10 icons of 2 points, drawn only
    // while some armor is worn.
    const int32_t armorPoints = ctx.creative ? 0 : player.armorValue();
    for (int i = 0; armorPoints > 0 && i < 10; ++i) {
        const int fill = armorPoints >= (i + 1) * 2 ? 2 : (armorPoints == i * 2 + 1 ? 1 : 0);
        pushArmorIcon(verts, hotbarX + static_cast<float>(i) * 18.0f, hotbarY - 40.0f, 2.2f, fill);
    }

    // 10 Air Bubbles above right side of hotbar when underwater
    if (!ctx.creative && player.isHeadUnderWater()) {
        const int bubbles = static_cast<int>(std::ceil(player.getAirRatio() * 10.0f));
        for (int i = 0; i < bubbles; ++i) {
            pushBubbleIcon(verts, hotbarX + hotbarWidth - static_cast<float>(i + 1) * 18.0f, hotbarY - 22.0f, 2.2f);
        }
    }

    // Held item tooltip above hearts
    const physics::InventorySlot held = player.getHeldItem();
    if (!held.isEmpty()) {
        const std::string name = render::itemAppearance(held.id).name;
        const float tw = render::BitmapFont::measureTextWidth(name, 2.0f);
        const float nameY = hotbarY - (armorPoints > 0 ? 60.0f : 42.0f); // Above the armor bar
        render::BitmapFont::appendText(verts, name, (sw - tw) * 0.5f, nameY, 2.0f, glm::vec4(1.0f, 0.98f, 0.82f, 1.0f));
    }

    // Hotbar slots
    for (size_t i = 0; i < physics::Player::HOTBAR_SIZE; ++i) {
        const float sx = hotbarX + static_cast<float>(i) * slotSize;
        const bool selected = (static_cast<int32_t>(i) == player.getSelectedSlot());
        pushBeveledSlot(verts, sx, hotbarY, slotSize, selected);

        pushSlotContents(verts, player.getHotbarSlot(i), sx, hotbarY, slotSize, 8.0f, 1.7f);
    }

    // 5. Inventory & Crafting Screen (E, or right-click a workbench)
    if (ctx.inventoryOpen) {
        // Dim background
        pushRect(verts, 0.0f, 0.0f, sw, sh, glm::vec4(0.05f, 0.05f, 0.07f, 0.72f));

        const CraftingScreenView& craft = ctx.crafting;
        const bool container = isContainerScreen(craft.kind);
        const InventoryLayoutKey layout{craft.gridDim, craft.kind, ctx.creative && !container};
        const SlotRect panel = inventorySlotRect(InventoryRegion::None, 0, screenWidth, screenHeight, layout);
        const float px = panel.x;
        const float py = panel.y;
        const float mx = static_cast<float>(ctx.mouseX);
        const float my = static_cast<float>(ctx.mouseY);
        const auto hovered = [&](const SlotRect& r) {
            return mx >= r.x && mx < r.x + r.size && my >= r.y && my < r.y + r.size;
        };

        // Classic Beta gray dialog window
        pushRect(verts, px - 3.0f, py - 3.0f, px + kPanelW + 3.0f, py + kPanelH + 3.0f, glm::vec4(0.12f, 0.12f, 0.14f, 0.96f));
        pushRect(verts, px, py, px + kPanelW, py + kPanelH, glm::vec4(0.55f, 0.55f, 0.58f, 0.95f));

        const glm::vec4 titleCol(1.0f, 0.96f, 0.72f, 1.0f);
        const char* title = craft.gridDim >= 3 ? "WORKBENCH (3X3)" : "CRAFTING (2X2)";
        if (craft.kind == ScreenKind::Furnace) title = "FURNACE";
        if (craft.kind == ScreenKind::Chest) title = "CHEST";
        render::BitmapFont::appendText(verts, title, px + 16.0f, py + 12.0f, 2.0f, titleCol);
        if (layout.palette) {
            render::BitmapFont::appendText(verts, "BETA 1.7.3 BLOCK PALETTE (CLICK TO EQUIP)", px + 340.0f, py + 12.0f, 2.0f, titleCol);
        }

        // Container screens (S2.6): the block entity's slots in the palette area.
        const glm::vec4 gaugeBack(0.30f, 0.30f, 0.32f, 0.95f);
        if (craft.kind == ScreenKind::Chest) {
            for (size_t i = 0; i < containerSlotCount(craft.kind); ++i) {
                const SlotRect r = inventorySlotRect(InventoryRegion::Container, i, screenWidth, screenHeight, layout);
                pushBeveledSlot(verts, r.x, r.y, r.size, hovered(r));
                pushSlotContents(verts, craft.container[i], r.x, r.y, r.size, 6.0f, 1.5f);
            }
        } else if (craft.kind == ScreenKind::Furnace) {
            const SlotRect in = inventorySlotRect(InventoryRegion::Container, kFurnaceInputSlot, screenWidth,
                                                  screenHeight, layout);
            const SlotRect out = inventorySlotRect(InventoryRegion::Container, kFurnaceOutputSlot, screenWidth,
                                                   screenHeight, layout);
            // Flame between input and fuel, filled from the bottom (Beta GuiFurnace).
            const float fx0 = in.x + in.size * 0.5f - 9.0f;
            const float fy0 = in.y + in.size + 6.0f;
            const float fy1 = in.y + 2.0f * in.size - 6.0f;
            const float burn = std::clamp(craft.burnProgress, 0.0f, 1.0f);
            pushRect(verts, fx0, fy0, fx0 + 18.0f, fy1, gaugeBack);
            pushRect(verts, fx0, fy1 - (fy1 - fy0) * burn, fx0 + 18.0f, fy1, glm::vec4(1.0f, 0.55f, 0.10f, 0.98f));
            // Arrow towards the output, filled from the left.
            const float ay = out.y + out.size * 0.5f;
            const float ax0 = in.x + in.size + 18.0f;
            const float ax1 = out.x - 12.0f;
            const float cook = std::clamp(craft.cookProgress, 0.0f, 1.0f);
            pushRect(verts, ax0, ay - 4.0f, ax1, ay + 4.0f, gaugeBack);
            pushRect(verts, ax0, ay - 4.0f, ax0 + (ax1 - ax0) * cook, ay + 4.0f, glm::vec4(0.95f, 0.95f, 0.95f, 0.98f));
            for (size_t i = 0; i < kFurnaceSlotCount; ++i) {
                const SlotRect r = inventorySlotRect(InventoryRegion::Container, i, screenWidth, screenHeight, layout);
                pushBeveledSlot(verts, r.x, r.y, r.size, hovered(r));
                pushSlotContents(verts, craft.container[i], r.x, r.y, r.size, i == kFurnaceOutputSlot ? 7.0f : 6.0f,
                                 1.5f);
            }
        }

        // Left: crafting grid -> result
        const size_t cells = container ? 0 : static_cast<size_t>(craft.gridDim * craft.gridDim);
        for (size_t i = 0; i < cells && i < craft.grid.size(); ++i) {
            const SlotRect r = inventorySlotRect(InventoryRegion::CraftingGrid, i, screenWidth, screenHeight, craft.gridDim);
            pushBeveledSlot(verts, r.x, r.y, r.size, hovered(r));
            pushSlotContents(verts, craft.grid[i], r.x, r.y, r.size, 6.0f, 1.5f);
        }
        if (!container) {
            const SlotRect res = inventorySlotRect(InventoryRegion::CraftingResult, 0, screenWidth, screenHeight, layout);
            const float arrowY = res.y + res.size * 0.5f;
            pushRect(verts, res.x - 40.0f, arrowY - 3.0f, res.x - 14.0f, arrowY + 3.0f, glm::vec4(0.85f, 0.85f, 0.85f, 0.95f));
            pushRect(verts, res.x - 16.0f, arrowY - 8.0f, res.x - 10.0f, arrowY + 8.0f, glm::vec4(0.85f, 0.85f, 0.85f, 0.95f));
            pushBeveledSlot(verts, res.x, res.y, res.size, hovered(res));
            pushSlotContents(verts, craft.result, res.x, res.y, res.size, 7.0f, 1.6f);
            if (!craft.result.isEmpty()) {
                render::BitmapFont::appendText(verts, render::itemAppearance(craft.result.id).name, px + 16.0f,
                                       res.y + res.size + 14.0f, 1.6f, glm::vec4(0.45f, 1.0f, 0.45f, 1.0f));
            }
        }
        render::BitmapFont::appendText(verts, "LEFT: TAKE/PLACE STACK  RIGHT: HALF/ONE", px + 16.0f, py + kPanelH - 26.0f, 1.4f,
                               glm::vec4(0.92f, 0.92f, 0.92f, 0.9f));

        // Right Top: 36-item Creative / Block Palette (9x4 grid)
        const auto palette = creativePalette();
        for (size_t i = 0; layout.palette && i < palette.size(); ++i) {
            const SlotRect r = inventorySlotRect(InventoryRegion::Palette, i, screenWidth, screenHeight, layout);
            pushBeveledSlot(verts, r.x, r.y, r.size, hovered(r));
            pushItemIcon(verts, paletteItem(i), r.x + 6.0f, r.y + 6.0f, r.size - 12.0f);
        }

        // Right Bottom: Player Inventory (3 rows of 9 backpack slots + 1 row of 9 hotbar slots)
        render::BitmapFont::appendText(verts, "PLAYER INVENTORY", px + 340.0f, py + 205.0f, 1.7f, glm::vec4(0.95f));
        for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
            const SlotRect r = inventorySlotRect(InventoryRegion::Inventory, i, screenWidth, screenHeight, layout);
            pushBeveledSlot(verts, r.x, r.y, r.size, hovered(r));
            pushSlotContents(verts, player.getInventorySlot(i), r.x, r.y, r.size, 6.0f, 1.5f);
        }

        // S6.7 armor column; an empty slot shows the faint outline of its piece.
        for (size_t i = 0; craft.kind == ScreenKind::Inventory2x2 && i < physics::Player::ARMOR_SLOT_COUNT; ++i) {
            const SlotRect r = inventorySlotRect(InventoryRegion::Armor, i, screenWidth, screenHeight, layout);
            pushBeveledSlot(verts, r.x, r.y, r.size, hovered(r));
            const physics::InventorySlot piece = player.getArmorSlot(i);
            if (piece.isEmpty()) {
                pushTileIcon(verts, render::kArmorIconTiles[i], r.x + 6.0f, r.y + 6.0f, r.size - 12.0f,
                             glm::vec4(0.22f, 0.22f, 0.24f, 0.55f));
            } else {
                pushSlotContents(verts, piece, r.x, r.y, r.size, 6.0f, 1.5f);
            }
        }

        // The stack held by the cursor follows the mouse, above everything else.
        if (!craft.carried.isEmpty()) {
            constexpr float carriedSize = 38.0f;
            pushSlotContents(verts, craft.carried, mx - carriedSize * 0.5f, my - carriedSize * 0.5f, carriedSize, 4.0f, 1.5f);
        }

        // S3.6: hovering a filled slot with an empty cursor shows its name (and the remaining
        // durability of a tool) in a dark frame next to the mouse (Beta GuiContainer).
        if (craft.carried.isEmpty()) {
            const InventoryHit hit = hitTestInventory(ctx.mouseX, ctx.mouseY, screenWidth, screenHeight, layout);
            physics::InventorySlot under{};
            switch (hit.region) {
                case InventoryRegion::Inventory: under = player.getInventorySlot(hit.index); break;
                case InventoryRegion::CraftingGrid: under = hit.index < craft.grid.size() ? craft.grid[hit.index] : under; break;
                case InventoryRegion::CraftingResult: under = craft.result; break;
                case InventoryRegion::Container:
                    under = hit.index < craft.container.size() ? craft.container[hit.index] : under;
                    break;
                case InventoryRegion::Palette: under = {paletteItem(hit.index), 1}; break;
                case InventoryRegion::Armor: under = player.getArmorSlot(hit.index); break;
                default: break;
            }
            const std::vector<std::string> lines = inventoryTooltip(under);
            if (!lines.empty()) {
                constexpr float kTipScale = 1.6f;
                constexpr float kLineH = 8.0f * kTipScale + 4.0f;
                float tw = 0.0f;
                for (const std::string& line : lines) tw = std::max(tw, render::BitmapFont::measureTextWidth(line, kTipScale));
                const float th = kLineH * static_cast<float>(lines.size());
                const float tx = std::min(mx + 14.0f, sw - tw - 8.0f);
                const float ty = std::max(my - th - 6.0f, 4.0f);
                pushRect(verts, tx - 5.0f, ty - 5.0f, tx + tw + 5.0f, ty + th + 1.0f, glm::vec4(0.16f, 0.0f, 0.33f, 0.92f));
                pushRect(verts, tx - 4.0f, ty - 4.0f, tx + tw + 4.0f, ty + th, glm::vec4(0.06f, 0.0f, 0.06f, 0.94f));
                for (size_t i = 0; i < lines.size(); ++i) {
                    const glm::vec4 col = i == 0 ? glm::vec4(1.0f) : glm::vec4(0.66f, 0.66f, 0.66f, 1.0f);
                    render::BitmapFont::appendText(verts, lines[i], tx, ty + static_cast<float>(i) * kLineH, kTipScale, col);
                }
            }
        }
    }

    // Render all UI quads in orthographic screen space
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const glm::mat4 ortho = glm::ortho(0.0f, sw, sh, 0.0f, -1.0f, 1.0f);
    m_uiShader.bind();
    atlas.bind(0);
    m_uiShader.setInt("uAtlas", 0);
    m_uiShader.setMat4("uOrtho", ortho);

    bc_glBindVertexArray(m_vao);
    bc_glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    bc_glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(verts.size() * sizeof(render::UiQuadVertex)),
        verts.data(),
        GL_DYNAMIC_DRAW
    );
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    bc_glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

float HudRenderer::hudScale(int32_t windowWidth, int32_t windowHeight, render::GuiScalePreference pref) noexcept {
    return static_cast<float>(render::TextRenderer::calculateGuiScale(windowWidth, windowHeight, pref)) / 3.0f;
}

HudCanvas HudRenderer::hudCanvas(int32_t windowWidth, int32_t windowHeight, render::GuiScalePreference pref) noexcept {
    const float scale = hudScale(windowWidth, windowHeight, pref);
    const auto toCanvas = [scale](int32_t v) {
        return std::max<int32_t>(1, static_cast<int32_t>(std::lround(static_cast<float>(v) / scale)));
    };
    return {toCanvas(windowWidth), toCanvas(windowHeight), scale, windowWidth, windowHeight};
}

std::vector<std::string> HudRenderer::inventoryTooltip(const physics::InventorySlot& slot) {
    if (slot.isEmpty()) {
        return {};
    }
    std::vector<std::string> lines{std::string(render::itemAppearance(slot.id).name)};
    if (const std::optional<gameplay::ToolDef> tool = gameplay::findTool(slot.id); tool && tool->maxDamage > 0) {
        const int32_t left = tool->maxDamage - std::clamp(slot.damage, 0, tool->maxDamage);
        lines.push_back(std::to_string(left) + " / " + std::to_string(tool->maxDamage));
    } else if (const std::optional<gameplay::ArmorInfo> armor = gameplay::armorInfo(slot.id)) {
        const int32_t left = armor->maxDamage - std::clamp(slot.damage, 0, armor->maxDamage);
        lines.push_back(std::to_string(left) + " / " + std::to_string(armor->maxDamage));
    }
    return lines;
}

SlotRect HudRenderer::inventorySlotRect(InventoryRegion region, size_t index, int32_t screenWidth,
                                        int32_t screenHeight, int gridDim) noexcept {
    return inventorySlotRect(region, index, screenWidth, screenHeight, InventoryLayoutKey{gridDim, gridKind(gridDim), true});
}

SlotRect HudRenderer::inventorySlotRect(InventoryRegion region, size_t index, int32_t screenWidth,
                                        int32_t screenHeight, InventoryLayoutKey layout) noexcept {
    const int gridDim = layout.gridDim;
    const float px = (static_cast<float>(screenWidth) - kPanelW) * 0.5f;
    const float py = (static_cast<float>(screenHeight) - kPanelH) * 0.5f - 15.0f;
    const float rightX = px + 340.0f;
    const float dim = static_cast<float>(std::clamp(gridDim, 2, 3));
    switch (region) {
        case InventoryRegion::None: // The dialog panel itself
            return {px, py, kPanelW};
        case InventoryRegion::Outside:
            return {};
        case InventoryRegion::CraftingGrid: {
            const auto d = static_cast<size_t>(dim);
            // Centred vertically in the area above the hint line, same left edge for 2x2 and 3x3.
            const float gridY = py + 60.0f + (3.0f - dim) * kSlot * 0.5f;
            return {px + 30.0f + static_cast<float>(index % d) * kSlot, gridY + static_cast<float>(index / d) * kSlot, kSlot};
        }
        case InventoryRegion::CraftingResult: {
            const float gridY = py + 60.0f + (3.0f - dim) * kSlot * 0.5f;
            return {px + 30.0f + dim * kSlot + 56.0f, gridY + (dim * kSlot - kResultSlot) * 0.5f, kResultSlot};
        }
        case InventoryRegion::Palette:
            return {rightX + static_cast<float>(index % 9) * kSlot, py + 36.0f + static_cast<float>(index / 9) * kSlot, kSlot};
        case InventoryRegion::Container: {
            // The palette area: 9 columns of kSlot fit it exactly.
            const float top = py + 36.0f;
            if (layout.kind == ScreenKind::Chest) {
                if (index >= containerSlotCount(layout.kind)) return {};
                return {rightX + static_cast<float>(index % 9) * kSlot, top + static_cast<float>(index / 9) * kSlot, kSlot};
            }
            if (layout.kind == ScreenKind::Furnace) {
                // Beta GuiFurnace: input above the flame above the fuel, arrow, larger output.
                const float colX = rightX + 2.5f * kSlot;
                switch (index) {
                    case kFurnaceInputSlot: return {colX, top, kSlot};
                    case kFurnaceFuelSlot: return {colX, top + 2.0f * kSlot, kSlot};
                    case kFurnaceOutputSlot:
                        return {rightX + 6.0f * kSlot, top + 1.5f * kSlot - kResultSlot * 0.5f, kResultSlot};
                    default: return {};
                }
            }
            return {};
        }
        case InventoryRegion::Armor:
            // S6.7: a column left of the inventory rows, helmet at the top (Beta GuiInventory).
            if (layout.kind != ScreenKind::Inventory2x2 || index >= physics::Player::ARMOR_SLOT_COUNT) return {};
            return {rightX - kSlot - 14.0f, py + 228.0f + static_cast<float>(index) * kSlot, kSlot};
        case InventoryRegion::Inventory: {
            // Slots 9..35 in the top 3 rows, the hotbar 0..8 in a separate bottom row.
            const size_t row = index < 9 ? 3 : (index - 9) / 9;
            const float extra = row == 3 ? 10.0f : 0.0f;
            return {rightX + static_cast<float>(index % 9) * kSlot, py + 228.0f + static_cast<float>(row) * kSlot + extra, kSlot};
        }
    }
    return {};
}

InventoryHit HudRenderer::hitTestInventory(int32_t mouseX, int32_t mouseY, int32_t screenWidth, int32_t screenHeight,
                                           int gridDim, bool withPalette) noexcept {
    return hitTestInventory(mouseX, mouseY, screenWidth, screenHeight,
                            InventoryLayoutKey{gridDim, gridKind(gridDim), withPalette});
}

InventoryHit HudRenderer::hitTestInventory(int32_t mouseX, int32_t mouseY, int32_t screenWidth, int32_t screenHeight,
                                           InventoryLayoutKey layout) noexcept {
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);
    const auto inside = [&](const SlotRect& r) {
        return mx >= r.x && mx < r.x + r.size && my >= r.y && my < r.y + r.size;
    };
    const auto scan = [&](InventoryRegion region, size_t count) -> std::optional<InventoryHit> {
        for (size_t i = 0; i < count; ++i) {
            if (inside(inventorySlotRect(region, i, screenWidth, screenHeight, layout))) {
                return InventoryHit{region, i};
            }
        }
        return std::nullopt;
    };
    if (isContainerScreen(layout.kind)) {
        if (const auto hit = scan(InventoryRegion::Container, containerSlotCount(layout.kind))) return *hit;
    } else {
        const auto dim = static_cast<size_t>(std::clamp(layout.gridDim, 2, 3));
        if (const auto hit = scan(InventoryRegion::CraftingGrid, dim * dim)) return *hit;
        if (const auto hit = scan(InventoryRegion::CraftingResult, 1)) return *hit;
        if (layout.palette) {
            if (const auto hit = scan(InventoryRegion::Palette, kCreativePalette.size())) return *hit;
        }
    }
    if (const auto hit = scan(InventoryRegion::Inventory, physics::Player::TOTAL_INVENTORY_SIZE)) return *hit;
    if (layout.kind == ScreenKind::Inventory2x2) {
        if (const auto hit = scan(InventoryRegion::Armor, physics::Player::ARMOR_SLOT_COUNT)) return *hit;
    }
    return {};
}

InventoryHit HudRenderer::hitTestInventoryScreen(int32_t mouseX, int32_t mouseY, int32_t screenWidth,
                                                 int32_t screenHeight, InventoryLayoutKey layout) noexcept {
    const InventoryHit hit = hitTestInventory(mouseX, mouseY, screenWidth, screenHeight, layout);
    if (hit.region != InventoryRegion::None) {
        return hit;
    }
    const SlotRect panel = inventorySlotRect(InventoryRegion::None, 0, screenWidth, screenHeight, layout);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);
    const bool inside = mx >= panel.x && mx < panel.x + kPanelW && my >= panel.y && my < panel.y + kPanelH;
    return inside ? hit : InventoryHit{InventoryRegion::Outside, 0};
}

bool HudRenderer::handleInventoryClick(
    int32_t mouseX,
    int32_t mouseY,
    int32_t screenWidth,
    int32_t screenHeight,
    physics::Player& player
) {
    const InventoryHit hit = hitTestInventory(mouseX, mouseY, screenWidth, screenHeight, 2);
    if (hit.region != InventoryRegion::Palette) {
        return false;
    }
    const gameplay::ItemId item = paletteItem(hit.index);
    player.setHeldItem(physics::InventorySlot{item, gameplay::maxStackSize(item)});
    return true;
}

} // namespace betacraft::ui
