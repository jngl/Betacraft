#include "render/TextureAtlas.hpp"
#include "core/Logger.hpp"
#include "render/MobModels.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#if defined(BETACRAFT_HAS_LIBPNG)
#include <png.h>
#endif

namespace betacraft::render {

namespace {

constexpr uint32_t pixelHash(int x, int y, uint32_t seed) noexcept {
    const uint32_t ux = static_cast<uint32_t>(x);
    const uint32_t uy = static_cast<uint32_t>(y);
    uint32_t h = (ux * 374761393U + uy * 668265263U) ^ (seed * 0x9E3779B9U);
    h = (h ^ (h >> 13U)) * 1274126177U;
    return h ^ (h >> 16U);
}

void putPixel(std::vector<uint8_t>& img, int tileIdx, int px, int py, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept {
    if (px < 0 || px >= 16 || py < 0 || py >= 16) return;
    const int tx = (tileIdx % 16) * 16 + px;
    const int ty = (tileIdx / 16) * 16 + py;
    const size_t idx = static_cast<size_t>((ty * 256 + tx) * 4);
    img[idx + 0] = r;
    img[idx + 1] = g;
    img[idx + 2] = b;
    img[idx + 3] = a;
}

uint8_t clampByte(int v) noexcept {
    return static_cast<uint8_t>(std::clamp(v, 0, 255));
}

void drawOreTile(std::vector<uint8_t>& img, int tileIdx, uint8_t oreR, uint8_t oreG, uint8_t oreB, uint32_t seed) noexcept {
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const uint32_t h = pixelHash(x, y / 2, 101);
            const int n = static_cast<int>(h % 22U) - 11;
            const uint8_t s = clampByte(124 + n);
            putPixel(img, tileIdx, x, y, s, s, s, 255);
        }
    }

    // Scatter ore nuggets in interior [2..13]
    for (int y = 2; y < 14; ++y) {
        for (int x = 2; x < 14; ++x) {
            const uint32_t h = pixelHash(x / 2, y / 2, seed);
            const uint32_t fine = pixelHash(x, y, seed + 7);
            if ((h % 11U) < 2U && (fine % 4U) != 0U) {
                const int shade = static_cast<int>(fine % 30U) - 15;
                putPixel(
                    img, tileIdx, x, y,
                    clampByte(static_cast<int>(oreR) + shade),
                    clampByte(static_cast<int>(oreG) + shade),
                    clampByte(static_cast<int>(oreB) + shade),
                    255
                );
            }
        }
    }
}

void drawPickaxeIcon(std::vector<uint8_t>& img, int tileIdx, uint8_t headR, uint8_t headG, uint8_t headB) noexcept {
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            putPixel(img, tileIdx, x, y, 0, 0, 0, 0);
        }
    }
    // Wooden diagonal handle from bottom-left (2,13) to top-right (11,4)
    for (int i = 0; i < 10; ++i) {
        const int hx = 2 + i;
        const int hy = 13 - i;
        putPixel(img, tileIdx, hx, hy, 138, 102, 54, 255);
        putPixel(img, tileIdx, hx + 1, hy, 102, 74, 38, 255);
    }
    // Curved pickaxe head at top-right
    static constexpr int headCoords[14][2] = {
        {7, 2}, {8, 2}, {9, 2}, {10, 2}, {11, 2},
        {8, 3}, {9, 3}, {10, 3}, {11, 3}, {12, 3},
        {12, 4}, {13, 5}, {13, 6}, {13, 7}
    };
    for (const auto& pt : headCoords) {
        putPixel(img, tileIdx, pt[0], pt[1], headR, headG, headB, 255);
    }
}

void clearTile(std::vector<uint8_t>& img, int tileIdx) noexcept {
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            putPixel(img, tileIdx, x, y, 0, 0, 0, 0);
        }
    }
}

// Diagonal wooden handle from the bottom-left corner up to (2 + len - 1, 13 - len + 1).
void drawToolHandle(std::vector<uint8_t>& img, int tileIdx, int len) noexcept {
    for (int i = 0; i < len; ++i) {
        putPixel(img, tileIdx, 2 + i, 13 - i, 138, 102, 54, 255);
        putPixel(img, tileIdx, 3 + i, 13 - i, 102, 74, 38, 255);
    }
}

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

// Head colours in gameplay::ToolMaterial order: wood, stone, iron, gold, diamond.
constexpr Rgb kToolMaterialColors[5] = {
    {162, 130, 78}, {138, 138, 142}, {225, 228, 232}, {250, 222, 60}, {88, 238, 245}
};

void putHead(std::vector<uint8_t>& img, int tileIdx, int x, int y, Rgb c) noexcept {
    putPixel(img, tileIdx, x, y, c.r, c.g, c.b, 255);
}

void drawAxeIcon(std::vector<uint8_t>& img, int tileIdx, Rgb c) noexcept {
    clearTile(img, tileIdx);
    drawToolHandle(img, tileIdx, 10);
    static constexpr int head[][2] = {
        {8, 2}, {9, 2}, {10, 2}, {7, 3}, {8, 3}, {9, 3}, {10, 3}, {11, 3},
        {7, 4}, {8, 4}, {9, 4}, {10, 4}, {8, 5}, {9, 5}, {7, 6}, {8, 6}
    };
    for (const auto& pt : head) putHead(img, tileIdx, pt[0], pt[1], c);
}

void drawShovelIcon(std::vector<uint8_t>& img, int tileIdx, Rgb c) noexcept {
    clearTile(img, tileIdx);
    drawToolHandle(img, tileIdx, 8);
    static constexpr int head[][2] = {
        {11, 2}, {12, 2}, {10, 3}, {11, 3}, {12, 3}, {13, 3}, {10, 4}, {11, 4},
        {12, 4}, {13, 4}, {10, 5}, {11, 5}, {12, 5}, {9, 6}, {10, 6}
    };
    for (const auto& pt : head) putHead(img, tileIdx, pt[0], pt[1], c);
}

void drawSwordIcon(std::vector<uint8_t>& img, int tileIdx, Rgb c) noexcept {
    clearTile(img, tileIdx);
    // Grip and pommel, then a crossguard and the blade up to the top-right corner.
    putPixel(img, tileIdx, 2, 13, 102, 74, 38, 255);
    putPixel(img, tileIdx, 3, 12, 138, 102, 54, 255);
    putPixel(img, tileIdx, 4, 11, 138, 102, 54, 255);
    static constexpr int guard[][2] = {{3, 9}, {4, 10}, {6, 12}, {7, 13}, {5, 10}, {5, 11}, {6, 11}};
    for (const auto& pt : guard) putPixel(img, tileIdx, pt[0], pt[1], 92, 66, 34, 255);
    for (int i = 0; i < 8; ++i) {
        putHead(img, tileIdx, 6 + i, 9 - i, c);
        putHead(img, tileIdx, 6 + i, 8 - i, c);
        putPixel(img, tileIdx, 7 + i, 9 - i, clampByte(c.r - 40), clampByte(c.g - 40), clampByte(c.b - 40), 255);
    }
}

void drawHoeIcon(std::vector<uint8_t>& img, int tileIdx, Rgb c) noexcept {
    clearTile(img, tileIdx);
    drawToolHandle(img, tileIdx, 10);
    static constexpr int head[][2] = {{7, 2}, {8, 2}, {9, 2}, {10, 2}, {11, 2}, {7, 3}, {8, 3}, {7, 4}};
    for (const auto& pt : head) putHead(img, tileIdx, pt[0], pt[1], c);
}

// Mob skins (render/MobModels.hpp): noisy base colours, plus faces drawn on the head tiles.
void fillNoisyTile(std::vector<uint8_t>& img, int tileIdx, Rgb c, int spread, uint32_t seed) noexcept {
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, seed) % static_cast<uint32_t>(spread * 2 + 1)) - spread;
            putPixel(img, tileIdx, x, y, clampByte(c.r + n), clampByte(c.g + n), clampByte(c.b + n), 255);
        }
    }
}

void fillRect(std::vector<uint8_t>& img, int tileIdx, int x0, int y0, int x1, int y1, Rgb c) noexcept {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            putPixel(img, tileIdx, x, y, c.r, c.g, c.b, 255);
        }
    }
}

// S6.7 armor icons (helmet, chestplate, leggings, boots): light grey with a dark outline so
// ItemAppearance tints them per material. '#' body, '+' highlight, 'o' outline.
void drawArmorIcon(std::vector<uint8_t>& img, int tileIdx, const std::array<const char*, 16>& rows) noexcept {
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const char c = rows[static_cast<size_t>(y)][x];
            const int n = static_cast<int>(pixelHash(x, y, 23800U + static_cast<uint32_t>(tileIdx)) % 12U) - 6;
            switch (c) {
                case '#': putPixel(img, tileIdx, x, y, clampByte(196 + n), clampByte(196 + n), clampByte(196 + n), 255); break;
                case '+': putPixel(img, tileIdx, x, y, 250, 250, 250, 255); break;
                case 'o': putPixel(img, tileIdx, x, y, 52, 52, 56, 255); break;
                default: putPixel(img, tileIdx, x, y, 0, 0, 0, 0); break;
            }
        }
    }
}

void drawArmorTiles(std::vector<uint8_t>& img) noexcept {
    drawArmorIcon(img, 238, {"                ",
                             "                ",
                             "                ",
                             "   oooooooooo   ",
                             "  o+++#######o  ",
                             "  o+#########o  ",
                             "  o##########o  ",
                             "  o##oooooo##o  ",
                             "  o##o    o##o  ",
                             "  o##o    o##o  ",
                             "  oooo    oooo  ",
                             "                ",
                             "                ",
                             "                ",
                             "                ",
                             "                "});
    drawArmorIcon(img, 239, {"                ",
                             "  oooo    oooo  ",
                             " o+##o    o##+o ",
                             " o+###oooo###+o ",
                             " o############o ",
                             " oooo######oooo ",
                             "    o+#####o    ",
                             "    o+#####o    ",
                             "    o######o    ",
                             "    o######o    ",
                             "    o######o    ",
                             "    o######o    ",
                             "    o######o    ",
                             "    oooooooo    ",
                             "                ",
                             "                "});
    drawArmorIcon(img, 254, {"                ",
                             "                ",
                             "   oooooooooo   ",
                             "   o+########o  ",
                             "   o+########o  ",
                             "   o####o###o   ",
                             "   o+##o o##o   ",
                             "   o+##o o##o   ",
                             "   o###o o##o   ",
                             "   o###o o##o   ",
                             "   o###o o##o   ",
                             "   o###o o##o   ",
                             "   ooooo oooo   ",
                             "                ",
                             "                ",
                             "                "});
    drawArmorIcon(img, 223, {"                ",
                             "                ",
                             "                ",
                             "                ",
                             "                ",
                             "                ",
                             "  oooo    oooo  ",
                             "  o+#o    o+#o  ",
                             "  o+#o    o+#o  ",
                             "  o##o    o##o  ",
                             " o###o   o###o  ",
                             " o###o   o###o  ",
                             " ooooo   ooooo  ",
                             "                ",
                             "                ",
                             "                "});
}

// S6.10 mob spawner (Beta terrain tile 65): a dark iron cage around a black interior.
void drawMobSpawnerTile(std::vector<uint8_t>& img) noexcept {
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool bar = x == 0 || x == 15 || y == 0 || y == 15 || x == 5 || x == 10 || y == 5 || y == 10;
            if (bar) {
                const int n = static_cast<int>(pixelHash(x, y, 6500) % 20U) - 10;
                const bool lit = (x + y) % 5 == 0;
                putPixel(img, 65, x, y, clampByte((lit ? 96 : 54) + n), clampByte((lit ? 104 : 62) + n),
                         clampByte((lit ? 118 : 74) + n), 255);
            } else {
                putPixel(img, 65, x, y, 10, 12, 16, 0); // See-through between the bars
            }
        }
    }
}

void drawMobSkinTiles(std::vector<uint8_t>& img) noexcept {
    constexpr Rgb kPigSkin{240, 168, 166};
    constexpr Rgb kSheepWool{234, 234, 228};
    constexpr Rgb kSheepSkin{214, 186, 160};
    constexpr Rgb kZombieSkin{82, 142, 70};
    constexpr Rgb kWhite{245, 245, 245};
    constexpr Rgb kBlack{24, 22, 22};

    fillNoisyTile(img, kPigSkinTile, kPigSkin, 8, 22400);
    fillNoisyTile(img, kPigFaceTile, kPigSkin, 8, 22500);
    fillRect(img, kPigFaceTile, 2, 5, 3, 6, kWhite);
    fillRect(img, kPigFaceTile, 12, 5, 13, 6, kWhite);
    fillRect(img, kPigFaceTile, 3, 5, 3, 6, kBlack);
    fillRect(img, kPigFaceTile, 12, 5, 12, 6, kBlack);
    fillRect(img, kPigFaceTile, 5, 8, 10, 12, {226, 132, 142});
    fillRect(img, kPigFaceTile, 6, 10, 6, 10, {120, 62, 72});
    fillRect(img, kPigFaceTile, 9, 10, 9, 10, {120, 62, 72});

    fillNoisyTile(img, kSheepWoolTile, kSheepWool, 12, 22600);
    fillNoisyTile(img, kSheepSkinTile, kSheepSkin, 8, 22800);
    fillNoisyTile(img, kSheepFaceTile, kSheepSkin, 8, 22700);
    fillRect(img, kSheepFaceTile, 0, 0, 15, 2, kSheepWool);
    fillRect(img, kSheepFaceTile, 3, 6, 4, 7, kWhite);
    fillRect(img, kSheepFaceTile, 11, 6, 12, 7, kWhite);
    fillRect(img, kSheepFaceTile, 4, 6, 4, 7, kBlack);
    fillRect(img, kSheepFaceTile, 11, 6, 11, 7, kBlack);
    fillRect(img, kSheepFaceTile, 6, 11, 9, 12, {186, 150, 132});

    fillNoisyTile(img, kZombieSkinTile, kZombieSkin, 12, 22900);
    fillNoisyTile(img, kZombieFaceTile, kZombieSkin, 12, 23000);
    fillRect(img, kZombieFaceTile, 0, 0, 15, 2, {48, 92, 44});
    fillRect(img, kZombieFaceTile, 3, 6, 5, 7, kBlack);
    fillRect(img, kZombieFaceTile, 10, 6, 12, 7, kBlack);
    fillRect(img, kZombieFaceTile, 5, 11, 10, 11, {40, 70, 36});
    fillNoisyTile(img, kZombieShirtTile, {40, 160, 170}, 10, 23100);
    fillNoisyTile(img, kZombieTrousersTile, {62, 58, 142}, 8, 23200);

    // S6.3: skeleton bone and skull, spider body and its red eyes, creeper skin and face, cow
    // hide (white with black patches) and face.
    constexpr Rgb kBone{196, 196, 190};
    constexpr Rgb kSpiderBody{52, 44, 40};
    constexpr Rgb kSpiderEye{196, 24, 24};
    constexpr Rgb kCreeperSkin{88, 176, 72};
    constexpr Rgb kCowHide{236, 232, 226};
    constexpr Rgb kCowPatch{58, 46, 40};
    fillNoisyTile(img, kSkeletonBoneTile, kBone, 10, 23300);
    fillNoisyTile(img, kSkeletonFaceTile, kBone, 10, 23400);
    fillRect(img, kSkeletonFaceTile, 3, 6, 5, 8, kBlack);
    fillRect(img, kSkeletonFaceTile, 10, 6, 12, 8, kBlack);
    fillRect(img, kSkeletonFaceTile, 7, 9, 8, 10, {90, 90, 86});
    fillRect(img, kSkeletonFaceTile, 4, 12, 11, 12, {90, 90, 86});
    fillNoisyTile(img, kSpiderBodyTile, kSpiderBody, 10, 23500);
    fillNoisyTile(img, kSpiderFaceTile, kSpiderBody, 10, 23600);
    fillRect(img, kSpiderFaceTile, 3, 6, 5, 7, kSpiderEye);
    fillRect(img, kSpiderFaceTile, 10, 6, 12, 7, kSpiderEye);
    fillRect(img, kSpiderFaceTile, 6, 4, 6, 4, kSpiderEye);
    fillRect(img, kSpiderFaceTile, 9, 4, 9, 4, kSpiderEye);
    fillNoisyTile(img, kCreeperSkinTile, kCreeperSkin, 24, 25000);
    fillNoisyTile(img, kCreeperFaceTile, kCreeperSkin, 24, 25100);
    fillRect(img, kCreeperFaceTile, 2, 4, 5, 7, kBlack);
    fillRect(img, kCreeperFaceTile, 10, 4, 13, 7, kBlack);
    fillRect(img, kCreeperFaceTile, 6, 8, 9, 11, kBlack);
    fillRect(img, kCreeperFaceTile, 4, 10, 5, 14, kBlack);
    fillRect(img, kCreeperFaceTile, 10, 10, 11, 14, kBlack);
    fillNoisyTile(img, kCowHideTile, kCowHide, 8, 25200);
    fillRect(img, kCowHideTile, 1, 2, 6, 6, kCowPatch);
    fillRect(img, kCowHideTile, 9, 9, 14, 14, kCowPatch);
    fillRect(img, kCowHideTile, 11, 1, 13, 3, kCowPatch);
    fillNoisyTile(img, kCowFaceTile, kCowPatch, 8, 25300);
    fillRect(img, kCowFaceTile, 4, 2, 11, 8, kCowHide);
    fillRect(img, kCowFaceTile, 3, 5, 4, 6, kBlack);
    fillRect(img, kCowFaceTile, 11, 5, 12, 6, kBlack);
    fillRect(img, kCowFaceTile, 4, 10, 11, 14, {200, 160, 150});
    fillRect(img, kCowFaceTile, 6, 12, 6, 12, kBlack);
    fillRect(img, kCowFaceTile, 9, 12, 9, 12, kBlack);
}

} // namespace

TextureAtlas::~TextureAtlas() noexcept {
    if (m_textureId != 0) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
}

glm::vec4 TextureAtlas::getTileUV(uint8_t tileIndex) noexcept {
    const float tx = static_cast<float>(tileIndex % TILES_PER_ROW);
    const float ty = static_cast<float>(tileIndex / TILES_PER_ROW);
    constexpr float step = 1.0f / static_cast<float>(TILES_PER_ROW);
    // Slight half-texel inset prevents bleeding at distance
    constexpr float eps = 0.08f / static_cast<float>(ATLAS_SIZE);
    return glm::vec4(
        tx * step + eps,
        ty * step + eps,
        (tx + 1.0f) * step - eps,
        (ty + 1.0f) * step - eps
    );
}

std::vector<uint8_t> TextureAtlas::generateProceduralAtlasRGBA() {
    std::vector<uint8_t> img(static_cast<size_t>(ATLAS_SIZE * ATLAS_SIZE * 4), 0);

    // Default fallback tile pattern
    for (int t = 0; t < 256; ++t) {
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                putPixel(img, t, x, y, 128, 128, 128, 255);
            }
        }
    }

    // Tile 0: Grass Top (grayscale so biome grassTint colors it smoothly!)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, 10) % 34U) - 17;
            const uint8_t v = clampByte(205 + n);
            putPixel(img, 0, x, y, v, v, v, 255);
        }
    }

    // Tile 1: Stone (classic stratified Beta gray stone)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const uint32_t h1 = pixelHash(x / 2, y, 101);
            const uint32_t h2 = pixelHash(x, y, 102);
            const int n = static_cast<int>(h1 % 20U) + static_cast<int>(h2 % 12U) - 16;
            const uint8_t v = clampByte(125 + n);
            putPixel(img, 1, x, y, v, v, v, 255);
        }
    }

    // Tile 2: Dirt
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const uint32_t h = pixelHash(x, y, 202);
            const int n = static_cast<int>(h % 28U) - 14;
            putPixel(img, 2, x, y, clampByte(134 + n), clampByte(96 + n * 2 / 3), clampByte(67 + n / 2), 255);
        }
    }

    // Tile 3: Grass Side (Dirt + green grass top overhang)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int grassDepth = 2 + static_cast<int>(pixelHash(x, 0, 303) % 4U);
            const int n = static_cast<int>(pixelHash(x, y, 202) % 26U) - 13;
            if (y < grassDepth) {
                putPixel(img, 3, x, y, clampByte(106 + n / 2), clampByte(168 + n), clampByte(64 + n / 2), 255);
            } else {
                putPixel(img, 3, x, y, clampByte(134 + n), clampByte(96 + n * 2 / 3), clampByte(67 + n / 2), 255);
            }
        }
    }

    // Tile 4: Wooden Planks (4 horizontal boards)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool seamH = (y % 4 == 0);
            const int boardRow = y / 4;
            const bool seamV = (x == ((boardRow * 7 + 3) % 16));
            const int grain = static_cast<int>(pixelHash(x / 3, y, 404) % 18U) - 9;
            const int diff = (seamH || seamV) ? -26 : grain;
            putPixel(img, 4, x, y, clampByte(162 + diff), clampByte(130 + diff), clampByte(78 + diff), 255);
        }
    }

    // Tile 5 & 6: Stone Slab Side & Top
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool border = (x == 0 || x == 15 || y == 0 || y == 15);
            const int n = static_cast<int>(pixelHash(x, y, 505) % 12U) - 6;
            const uint8_t topV = clampByte((border ? 142 : 165) + n);
            putPixel(img, 6, x, y, topV, topV, topV, 255);

            const bool midSeam = (y == 7 || y == 8);
            const uint8_t sideV = clampByte((midSeam ? 110 : 162) + n);
            putPixel(img, 5, x, y, sideV, sideV, sideV, 255);
        }
    }

    // Tile 7: Bricks
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int row = y / 4;
            const bool mortarH = (y % 4 == 0);
            const bool mortarV = ((x + (row % 2) * 4) % 8 == 0);
            const int n = static_cast<int>(pixelHash(x, y, 707) % 18U) - 9;
            if (mortarH || mortarV) {
                const uint8_t m = clampByte(168 + n);
                putPixel(img, 7, x, y, m, clampByte(m - 4), clampByte(m - 10), 255);
            } else {
                putPixel(img, 7, x, y, clampByte(156 + n), clampByte(74 + n / 2), clampByte(56 + n / 2), 255);
            }
        }
    }

    // Tile 8, 9, 10: TNT Side, Top, Bottom
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int stick = x / 4;
            const int n = static_cast<int>(pixelHash(x, y, 808) % 16U) - 8;
            if (y >= 5 && y <= 10) {
                // White band with dark TNT stripes
                const bool letter = (y >= 6 && y <= 9) && (x == 3 || x == 7 || x == 12);
                const uint8_t v = letter ? 30 : clampByte(228 + n);
                putPixel(img, 8, x, y, v, v, v, 255);
            } else {
                const int stickShade = (stick % 2 == 0) ? 0 : -16;
                putPixel(img, 8, x, y, clampByte(205 + stickShade + n), clampByte(46 + n / 2), clampByte(32 + n / 2), 255);
            }
            putPixel(img, 9, x, y, clampByte(185 + n), clampByte(52 + n), clampByte(40 + n), 255);
            putPixel(img, 10, x, y, clampByte(165 + n), clampByte(48 + n), clampByte(38 + n), 255);
        }
    }

    // Tile 12 (Rose) & Tile 13 (Dandelion) & Tile 15 (Sapling)
    for (int t : {12, 13, 15}) {
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                putPixel(img, t, x, y, 0, 0, 0, 0);
            }
        }
    }
    // Stem for flowers
    for (int y = 8; y < 16; ++y) {
        putPixel(img, 12, 7, y, 48, 128, 36, 255);
        putPixel(img, 12, 8, y, 38, 108, 28, 255);
        putPixel(img, 13, 7, y, 52, 136, 38, 255);
        putPixel(img, 13, 8, y, 42, 114, 30, 255);
    }
    // Rose blossom (Tile 12) & Dandelion blossom (Tile 13)
    for (int y = 3; y <= 8; ++y) {
        for (int x = 5; x <= 10; ++x) {
            const int dist = std::abs(x - 7) + std::abs(y - 5);
            if (dist <= 3) {
                putPixel(img, 12, x, y, (dist <= 1) ? 235 : 185, 24, 28, 255);
                putPixel(img, 13, x, y, 248, (dist <= 1) ? 235 : 198, 28, 255);
            }
        }
    }
    // Sapling (Tile 15)
    for (int y = 10; y < 16; ++y) {
        putPixel(img, 15, 7, y, 108, 78, 42, 255);
        putPixel(img, 15, 8, y, 92, 64, 34, 255);
    }
    for (int y = 3; y <= 10; ++y) {
        for (int x = 4; x <= 11; ++x) {
            if (std::abs(x - 7) + std::abs(y - 6) <= 4) {
                const int n = static_cast<int>(pixelHash(x, y, 1515) % 30U);
                putPixel(img, 15, x, y, clampByte(58 + n), clampByte(152 + n), clampByte(40 + n / 2), 255);
            }
        }
    }

    // Tile 16: Cobblestone (cellular Voronoi-like stones)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const uint32_t cell = pixelHash((x + (y & 1)) / 3, y / 3, 1616);
            const bool mortar = ((x + static_cast<int>(cell & 1U)) % 4 == 0) || ((y + static_cast<int>((cell >> 1U) & 1U)) % 4 == 0);
            const int n = static_cast<int>(pixelHash(x, y, 1617) % 24U) - 12;
            const uint8_t v = clampByte((mortar ? 78 : 132) + n);
            putPixel(img, 16, x, y, v, v, v, 255);
        }
    }

    // Tile 17: Bedrock
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const uint8_t v = static_cast<uint8_t>(32 + (pixelHash(x, y, 1717) % 72U));
            putPixel(img, 17, x, y, v, v, v, 255);
        }
    }

    // Tile 18: Sand
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, 1818) % 20U) - 10;
            putPixel(img, 18, x, y, clampByte(218 + n), clampByte(210 + n), clampByte(158 + n), 255);
        }
    }

    // Tile 19: Gravel
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, 1919) % 38U) - 19;
            putPixel(img, 19, x, y, clampByte(132 + n), clampByte(126 + n), clampByte(124 + n), 255);
        }
    }

    // Tile 20: Oak Log Side & Tile 21: Log Top
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int ridge = static_cast<int>(pixelHash(x, y / 4, 2020) % 26U) - 13;
            putPixel(img, 20, x, y, clampByte(104 + ridge), clampByte(82 + ridge), clampByte(50 + ridge / 2), 255);

            // Concentric growth rings on log top
            const int dist = std::max(std::abs(x - 7), std::abs(y - 7));
            if (dist >= 7) {
                putPixel(img, 21, x, y, 102, 80, 48, 255);
            } else {
                const int ring = (dist % 2 == 0) ? 12 : -6;
                putPixel(img, 21, x, y, clampByte(168 + ring), clampByte(136 + ring), clampByte(84 + ring), 255);
            }
        }
    }

    // Ores: Gold (32), Iron (33), Coal (34), Diamond (50), Redstone (51), Lapis (160)
    drawOreTile(img, 32, 245, 220, 50, 3200);
    drawOreTile(img, 33, 216, 175, 147, 3300);
    drawOreTile(img, 34, 34, 34, 36, 3400);
    drawOreTile(img, 50, 92, 238, 245, 5000);
    drawOreTile(img, 51, 225, 32, 32, 5100);
    drawOreTile(img, 160, 36, 78, 210, 16000);

    // Tile 35: Bookshelf Side
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (y < 2 || y == 7 || y == 8 || y >= 14 || x == 0 || x == 15) {
                const int n = static_cast<int>(pixelHash(x, y, 3535) % 14U) - 7;
                putPixel(img, 35, x, y, clampByte(162 + n), clampByte(130 + n), clampByte(78 + n), 255);
            } else {
                const int bookIdx = (x / 2) + (y / 8) * 7;
                static constexpr uint8_t bookColors[5][3] = {
                    {175, 42, 42}, {46, 118, 56}, {48, 82, 168}, {182, 148, 44}, {122, 56, 142}
                };
                const auto& c = bookColors[bookIdx % 5];
                const int spineShade = (x % 2 == 0) ? 8 : -10;
                putPixel(img, 35, x, y, clampByte(c[0] + spineShade), clampByte(c[1] + spineShade), clampByte(c[2] + spineShade), 255);
            }
        }
    }

    // Tile 36: Mossy Cobblestone
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const uint32_t h = pixelHash(x, y, 3636);
            if ((h % 5U) < 2U) {
                const int n = static_cast<int>(h % 20U) - 10;
                putPixel(img, 36, x, y, clampByte(74 + n), clampByte(124 + n), clampByte(52 + n), 255);
            } else {
                const uint8_t v = clampByte(118 + static_cast<int>(h % 26U) - 13);
                putPixel(img, 36, x, y, v, v, v, 255);
            }
        }
    }

    // Tile 37: Obsidian
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, 3737) % 24U);
            putPixel(img, 37, x, y, clampByte(20 + n), clampByte(16 + n * 2 / 3), clampByte(34 + n), 255);
        }
    }

    // Tile 39: Tall Grass (grayscale blades with alpha cutout, tinted by biome grassTint!)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            putPixel(img, 39, x, y, 0, 0, 0, 0);
            const int bladeHeight = 4 + static_cast<int>(pixelHash(x, 0, 3939) % 11U);
            if (x >= 1 && x <= 14 && y >= bladeHeight && ((x + (y / 3)) % 2 == 0)) {
                const uint8_t v = clampByte(185 + static_cast<int>(pixelHash(x, y, 3940) % 35U));
                putPixel(img, 39, x, y, v, v, v, 255);
            }
        }
    }

    // Tile 43, 59, 60: Crafting Table Top, Side, Front
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            // Top (43): 3x3 crafting grid
            const bool border = (x < 2 || x > 13 || y < 2 || y > 13);
            const bool gridLine = (x == 5 || x == 10 || y == 5 || y == 10);
            if (border) {
                putPixel(img, 43, x, y, 132, 98, 56, 255);
            } else if (gridLine) {
                putPixel(img, 43, x, y, 76, 54, 30, 255);
            } else {
                putPixel(img, 43, x, y, 176, 140, 88, 255);
            }

            // Side (59) & Front (60)
            const int n = static_cast<int>(pixelHash(x, y, 5959) % 16U) - 8;
            if (y < 4) {
                putPixel(img, 59, x, y, clampByte(142 + n), clampByte(96 + n), clampByte(58 + n), 255);
                putPixel(img, 60, x, y, clampByte(142 + n), clampByte(96 + n), clampByte(58 + n), 255);
            } else {
                const bool toolMark = (x >= 5 && x <= 10 && y >= 6 && y <= 12 && ((x + y) % 3 == 0));
                const uint8_t r = toolMark ? 72 : clampByte(158 + n);
                const uint8_t g = toolMark ? 76 : clampByte(124 + n);
                const uint8_t b = toolMark ? 82 : clampByte(74 + n);
                putPixel(img, 59, x, y, r, g, b, 255);
                putPixel(img, 60, x, y, r, g, b, 255);
            }
        }
    }

    // Tile 44, 45, 62: Furnace Front, Side, Top
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, 4444) % 20U) - 10;
            const bool border = (x == 0 || x == 15 || y == 0 || y == 15);
            const uint8_t base = clampByte((border ? 92 : 128) + n);
            putPixel(img, 45, x, y, base, base, base, 255);
            putPixel(img, 62, x, y, base, base, base, 255);

            const bool opening = (x >= 4 && x <= 11) && ((y >= 3 && y <= 6) || (y >= 10 && y <= 13));
            const uint8_t frontV = opening ? 28 : base;
            putPixel(img, 44, x, y, frontV, frontV, frontV, 255);

            // Tile 61: Lit Furnace Front (the lower opening glows)
            if (opening && y >= 10) {
                const int glow = static_cast<int>(pixelHash(x, y, 6161) % 40U);
                putPixel(img, 61, x, y, 255, clampByte(150 + glow), clampByte(30 + glow / 2), 255);
            } else {
                putPixel(img, 61, x, y, frontV, frontV, frontV, 255);
            }
        }
    }

    // Tile 25, 26, 27: Chest Top, Side, Front (planks with a dark rim and an iron latch)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y / 2, 2525) % 16U) - 8;
            const bool rim = (x == 0 || x == 15 || y == 0 || y == 15);
            const uint8_t r = rim ? 74 : clampByte(164 + n);
            const uint8_t g = rim ? 50 : clampByte(116 + n);
            const uint8_t b = rim ? 24 : clampByte(56 + n / 2);
            putPixel(img, 25, x, y, r, g, b, 255);
            const bool lidSeam = (y == 5);
            putPixel(img, 26, x, y, lidSeam ? 74 : r, lidSeam ? 50 : g, lidSeam ? 24 : b, 255);
            const bool latch = (x >= 7 && x <= 8 && y >= 4 && y <= 7);
            if (latch) {
                putPixel(img, 27, x, y, 200, 200, 206, 255);
            } else {
                putPixel(img, 27, x, y, lidSeam ? 74 : r, lidSeam ? 50 : g, lidSeam ? 24 : b, 255);
            }
        }
    }

    // Tile 49: Glass (crisp cyan-white frame + shine streaks on transparent interior)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool frame = (x == 0 || x == 15 || y == 0 || y == 15);
            const bool streak = ((x + y == 5) && x >= 2 && x <= 4) || ((x + y == 9) && x >= 3 && x <= 6);
            if (frame || streak) {
                putPixel(img, 49, x, y, 225, 248, 255, 255);
            } else {
                putPixel(img, 49, x, y, 0, 0, 0, 0);
            }
        }
    }

    // Tile 52: Leaves (grayscale cluster with alpha holes, tinted by biome foliageTint!)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const uint32_t h = pixelHash(x, y, 5252);
            if ((h % 7U) == 0U) {
                putPixel(img, 52, x, y, 0, 0, 0, 0);
            } else {
                const uint8_t v = clampByte(168 + static_cast<int>(h % 55U) - 25);
                putPixel(img, 52, x, y, v, v, v, 255);
            }
        }
    }

    // Tile 55: Dead Bush
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            putPixel(img, 55, x, y, 0, 0, 0, 0);
            if (y >= 4 && (std::abs(x - 7) == (15 - y) / 2 || (x == 7 && y >= 8))) {
                putPixel(img, 55, x, y, 138, 102, 58, 255);
            }
        }
    }

    // Tile 64: White Wool, Tile 66: Snow, Tile 67: Ice, Tile 68: Snow Grass Side
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, 6464) % 16U) - 8;
            putPixel(img, 64, x, y, clampByte(232 + n), clampByte(232 + n), clampByte(230 + n), 255);
            putPixel(img, 66, x, y, clampByte(244 + n / 2), clampByte(250 + n / 2), 255, 255);

            // Ice (67)
            const bool iceStreak = ((x + y) % 7 == 0);
            putPixel(img, 67, x, y, iceStreak ? 190 : 135, iceStreak ? 220 : 180, 255, 195);

            // Snow Grass Side (68)
            const int snowDepth = 3 + static_cast<int>(pixelHash(x, 0, 6868) % 3U);
            if (y < snowDepth) {
                putPixel(img, 68, x, y, 245, 250, 255, 255);
            } else {
                putPixel(img, 68, x, y, clampByte(134 + n), clampByte(96 + n), clampByte(67 + n), 255);
            }
        }
    }

    // Tile 69, 70, 71: Cactus Top, Side, Bottom
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, 7070) % 18U) - 9;
            const bool rib = (x % 4 == 0);
            const bool spike = (x % 4 == 2 && y % 5 == 2);
            if (spike) {
                putPixel(img, 70, x, y, 28, 38, 18, 255);
            } else {
                putPixel(img, 70, x, y, clampByte((rib ? 42 : 58) + n), clampByte((rib ? 112 : 142) + n), clampByte(36 + n / 2), 255);
            }
            putPixel(img, 69, x, y, clampByte(58 + n), clampByte(142 + n), clampByte(38 + n), 255);
            putPixel(img, 71, x, y, clampByte(88 + n), clampByte(152 + n), clampByte(62 + n), 255);
        }
    }

    // Tile 72: Clay & Tile 73: Sugar Cane
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, 7272) % 16U) - 8;
            putPixel(img, 72, x, y, clampByte(158 + n), clampByte(164 + n), clampByte(178 + n), 255);

            putPixel(img, 73, x, y, 0, 0, 0, 0);
            if (x == 4 || x == 5 || x == 10 || x == 11) {
                const bool node = (y % 5 == 0);
                putPixel(img, 73, x, y, node ? 145 : 108, node ? 195 : 172, 72, 255);
            }
        }
    }

    // Tile 80: Torch
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            putPixel(img, 80, x, y, 0, 0, 0, 0);
            if (x >= 7 && x <= 8 && y >= 6) {
                if (y == 6) {
                    putPixel(img, 80, x, y, 255, 250, 195, 255);
                } else if (y == 7) {
                    putPixel(img, 80, x, y, 255, 175, 42, 255);
                } else {
                    putPixel(img, 80, x, y, (x == 7) ? 148 : 116, (x == 7) ? 112 : 86, 52, 255);
                }
            }
        }
    }

    // S5.4 shapes. Tile 83: Ladder (two rails and four rungs on a transparent tile)
    clearTile(img, 83);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool rail = x == 2 || x == 3 || x == 12 || x == 13;
            const bool rung = (y % 4 == 1 || y % 4 == 2) && x > 3 && x < 12;
            if (rail || rung) {
                const int n = static_cast<int>(pixelHash(x, y, 8300) % 16U) - 8;
                const int edge = (y % 4 == 2 || x == 3 || x == 13) ? -22 : 0;
                putPixel(img, 83, x, y, clampByte(150 + n + edge), clampByte(112 + n + edge), clampByte(60 + n + edge), 255);
            }
        }
    }

    // Tiles 81 / 97: Wooden door, upper half (two panes) and lower half (panels)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int grain = static_cast<int>(pixelHash(x, y / 3, 8100) % 14U) - 7;
            const bool frame = x <= 1 || x >= 14 || y <= 1 || y >= 14;
            const bool upperWindow = !frame && y >= 3 && y <= 9 && x >= 3 && x <= 12 && x != 7 && x != 8;
            const bool lowerPanel = !frame && ((y >= 3 && y <= 6) || (y >= 9 && y <= 12)) && x >= 3 && x <= 12;
            const int base = frame ? -18 : 0;
            if (upperWindow) {
                putPixel(img, 81, x, y, 196, 222, 230, 255);
            } else {
                putPixel(img, 81, x, y, clampByte(150 + grain + base), clampByte(116 + grain + base), clampByte(66 + grain + base), 255);
            }
            const int panel = lowerPanel ? -12 : base;
            putPixel(img, 97, x, y, clampByte(150 + grain + panel), clampByte(116 + grain + panel), clampByte(66 + grain + panel), 255);
        }
    }

    // S8.5 redstone. Tiles 82 / 98: Iron door, upper half (window with bars) and lower half
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, 8200) % 12U) - 6;
            const bool frame = x <= 1 || x >= 14 || y <= 1 || y >= 14;
            const bool window = !frame && y >= 3 && y <= 9 && x >= 3 && x <= 12 && x % 3 != 0;
            const bool rivet = !frame && (y == 4 || y == 11) && (x == 4 || x == 11);
            const int base = frame ? -26 : 0;
            if (window) {
                putPixel(img, 82, x, y, 0, 0, 0, 0);
            } else {
                putPixel(img, 82, x, y, clampByte(200 + n + base), clampByte(200 + n + base), clampByte(204 + n + base), 255);
            }
            const int lower = rivet ? -50 : base;
            putPixel(img, 98, x, y, clampByte(200 + n + lower), clampByte(200 + n + lower), clampByte(204 + n + lower), 255);
        }
    }
    // Tile 96: Lever (a wooden stick, drawn on a transparent tile)
    clearTile(img, 96);
    for (int y = 3; y < 14; ++y) {
        for (int x = 7; x <= 8; ++x) {
            putPixel(img, 96, x, y, x == 7 ? 150 : 112, x == 7 ? 112 : 84, 54, 255);
        }
    }
    // Tiles 99 / 115: Redstone torch, lit (glowing red tip) and unlit (dark tip)
    for (const int tile : {99, 115}) {
        clearTile(img, tile);
        const bool lit = tile == 99;
        for (int y = 6; y < 16; ++y) {
            for (int x = 7; x <= 8; ++x) {
                if (y <= 7) {
                    putPixel(img, tile, x, y, lit ? 255 : 96, lit ? (y == 6 ? 120 : 30) : 20, lit ? (y == 6 ? 90 : 20) : 18, 255);
                } else {
                    putPixel(img, tile, x, y, x == 7 ? 148 : 116, x == 7 ? 112 : 86, 52, 255);
                }
            }
        }
    }
    // Tiles 164 / 165: Redstone wire, powered (bright red) and unpowered (dark red): a cross of
    // dust on a transparent tile.
    for (const int tile : {164, 165}) {
        clearTile(img, tile);
        const bool on = tile == 164;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                if ((x >= 6 && x <= 9) || (y >= 6 && y <= 9)) {
                    const int n = static_cast<int>(pixelHash(x, y, 16400) % 30U) - 15;
                    putPixel(img, tile, x, y, clampByte((on ? 230 : 110) + n), on ? 20 : 8, on ? 12 : 6, 255);
                }
            }
        }
    }

    // Tiles 86 / 87: Farmland top, wet (darker) and dry, with furrows
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, 8600) % 20U) - 10;
            const int furrow = (y % 4 == 0) ? -20 : 0;
            putPixel(img, 87, x, y, clampByte(128 + n + furrow), clampByte(90 + n + furrow), clampByte(60 + n / 2 + furrow), 255);
            putPixel(img, 86, x, y, clampByte(82 + n + furrow), clampByte(56 + n + furrow), clampByte(36 + n / 2 + furrow), 255);
        }
    }

    // Tiles 88..95: Crops, growth stages 0 (green sprouts) to 7 (tall golden wheat)
    for (int stage = 0; stage < 8; ++stage) {
        const int tile = 88 + stage;
        clearTile(img, tile);
        const int height = 3 + stage * 13 / 7;
        for (int x = 1; x < 16; x += 3) {
            const int h = std::min(16, height - static_cast<int>(pixelHash(x, stage, 8800) % 3U));
            for (int y = 16 - h; y < 16; ++y) {
                const int t = stage * 255 / 7;
                const bool ear = stage >= 5 && y < 16 - h + 3;
                const uint8_t r = clampByte(60 + t * 3 / 4 + (ear ? 20 : 0));
                const uint8_t g = clampByte(160 - t / 6);
                const uint8_t b = clampByte(40 - t / 10);
                putPixel(img, tile, x, y, r, g, b, 255);
                if (ear) {
                    putPixel(img, tile, std::min(15, x + 1), y, r, g, b, 255);
                }
            }
        }
    }

    // Tiles 100..103: Bed, top of the head half (pillow) and foot half, sides of both
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, 10000) % 10U) - 5;
            const bool border = x == 0 || x == 15;
            const uint8_t redR = clampByte(170 + n - (border ? 30 : 0));
            const uint8_t redG = clampByte(34 + n / 2);
            const uint8_t redB = clampByte(34 + n / 2);
            const bool pillow = y >= 2 && y <= 6 && x >= 2 && x <= 13;
            if (pillow) {
                putPixel(img, 100, x, y, clampByte(232 + n), clampByte(232 + n), clampByte(232 + n), 255);
            } else {
                putPixel(img, 100, x, y, redR, redG, redB, 255);
            }
            putPixel(img, 101, x, y, redR, redG, redB, 255);
            // Sides: blanket over a wooden frame, legs at the ends
            const bool wood = y >= 10;
            const bool gap = y >= 13 && x >= 3 && x <= 12;
            for (const int side : {102, 103}) {
                if (gap) {
                    putPixel(img, side, x, y, 0, 0, 0, 0);
                } else if (wood) {
                    putPixel(img, side, x, y, clampByte(140 + n), clampByte(104 + n), clampByte(58 + n), 255);
                } else if (side == 102 && y < 4) {
                    putPixel(img, side, x, y, clampByte(232 + n), clampByte(232 + n), clampByte(232 + n), 255);
                } else {
                    putPixel(img, side, x, y, redR, redG, redB, 255);
                }
            }
        }
    }

    // Tile 105: Glowstone
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const uint32_t h = pixelHash(x, y, 10500);
            if ((h % 5U) == 0U) {
                putPixel(img, 105, x, y, 255, 252, 185, 255);
            } else if ((h % 3U) == 0U) {
                putPixel(img, 105, x, y, 232, 184, 78, 255);
            } else {
                putPixel(img, 105, x, y, 152, 108, 46, 255);
            }
        }
    }

    // Tile 107, 108, 109: Piston Top, Side, Bottom
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, 10700) % 16U) - 8;
            const bool bolt = ((x == 2 || x == 13) && (y == 2 || y == 13));
            if (bolt) {
                putPixel(img, 107, x, y, 170, 170, 175, 255);
            } else {
                putPixel(img, 107, x, y, clampByte(168 + n), clampByte(132 + n), clampByte(82 + n), 255);
            }

            if (y < 4) {
                putPixel(img, 108, x, y, clampByte(168 + n), clampByte(132 + n), clampByte(82 + n), 255);
            } else {
                const uint8_t c = clampByte(115 + n);
                putPixel(img, 108, x, y, c, c, c, 255);
            }
            const uint8_t b = clampByte(110 + n);
            putPixel(img, 109, x, y, b, b, b, 255);
        }
    }

    // Tile 116: Pine Log & Tile 117: Birch Log
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y / 3, 11600) % 16U) - 8;
            putPixel(img, 116, x, y, clampByte(58 + n), clampByte(42 + n), clampByte(26 + n / 2), 255);

            const bool birchMark = ((pixelHash(x / 3, y, 11700) % 11U) == 0U);
            if (birchMark) {
                putPixel(img, 117, x, y, 48, 46, 44, 255);
            } else {
                const uint8_t v = clampByte(226 + n);
                putPixel(img, 117, x, y, v, v, clampByte(v - 6), 255);
            }
        }
    }

    // Items & Pickaxes (130..135)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            putPixel(img, 130, x, y, 0, 0, 0, 0);
            putPixel(img, 131, x, y, 0, 0, 0, 0);
            if (std::abs((15 - y) - x) <= 1 && x >= 3 && x <= 12) {
                putPixel(img, 130, x, y, 148, 110, 58, 255);
            }
            if ((x - 8) * (x - 8) + (y - 8) * (y - 8) <= 20) {
                const uint8_t c = clampByte(35 + static_cast<int>(pixelHash(x, y, 13100) % 30U));
                putPixel(img, 131, x, y, c, c, clampByte(c + 4), 255);
            }
        }
    }
    drawPickaxeIcon(img, 132, 162, 130, 78);  // Wood Pickaxe
    drawPickaxeIcon(img, 133, 138, 138, 142); // Stone Pickaxe
    drawPickaxeIcon(img, 134, 225, 228, 232); // Iron Pickaxe
    drawPickaxeIcon(img, 135, 88, 238, 245);  // Diamond Pickaxe

    // Tools without a BlockDef (render/ItemAppearance.hpp): gold pickaxe 136, then axes,
    // shovels, swords and hoes from 137, five materials each.
    drawPickaxeIcon(img, 136, kToolMaterialColors[3].r, kToolMaterialColors[3].g, kToolMaterialColors[3].b);
    for (int m = 0; m < 5; ++m) {
        drawAxeIcon(img, 137 + m, kToolMaterialColors[m]);
        drawShovelIcon(img, 142 + m, kToolMaterialColors[m]);
        drawSwordIcon(img, 147 + m, kToolMaterialColors[m]);
        drawHoeIcon(img, 152 + m, kToolMaterialColors[m]);
    }

    // Tile 157: Glowstone dust (scattered warm specks); tile 158: generic item blob, drawn
    // light so per-item tints colour it.
    clearTile(img, 157);
    clearTile(img, 158);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (x >= 3 && x <= 12 && y >= 4 && y <= 13 && pixelHash(x, y, 15700) % 3U == 0U) {
                putPixel(img, 157, x, y, 255, clampByte(200 + static_cast<int>(pixelHash(x, y, 15701) % 50U)), 90, 255);
            }
            const int dx = x - 8;
            const int dy = y - 8;
            const int d2 = dx * dx + dy * dy;
            if (d2 <= 25) {
                const uint8_t v = d2 >= 18 ? 150 : clampByte(225 - (dx + dy) * 4);
                putPixel(img, 158, x, y, v, v, v, 255);
            }
        }
    }

    // Tile 176, 192, 208: Sandstone Top, Side, Bottom
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int n = static_cast<int>(pixelHash(x, y, 17600) % 14U) - 7;
            putPixel(img, 176, x, y, clampByte(222 + n), clampByte(214 + n), clampByte(162 + n), 255);
            putPixel(img, 208, x, y, clampByte(208 + n), clampByte(198 + n), clampByte(148 + n), 255);
            const int band = (y < 4) ? 10 : ((y == 4 || y == 11) ? -14 : 0);
            putPixel(img, 192, x, y, clampByte(216 + band + n), clampByte(206 + band + n), clampByte(154 + band + n), 255);
        }
    }

    // Tile 205: Water (translucent royal blue with wave highlights)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int wave = static_cast<int>(std::sin((x + y * 0.5) * 0.8) * 14.0);
            putPixel(img, 205, x, y, clampByte(38 + wave), clampByte(96 + wave), clampByte(235 + wave / 2), 188);
        }
    }

    // Tile 237: Lava (opaque molten orange with darker crust veins)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int swirl = static_cast<int>(std::sin((x * 0.7) + std::cos(y * 0.6) * 2.0) * 22.0);
            const int n = static_cast<int>(pixelHash(x, y, 23700) % 18U) - 9;
            putPixel(img, 237, x, y, clampByte(228 + swirl / 2 + n), clampByte(96 + swirl + n), clampByte(18 + n / 2), 255);
        }
    }

    // Tiles 240..249: 10-Stage Block Breaking Crack Overlays
    for (int stage = 0; stage < 10; ++stage) {
        const int tileIdx = 240 + stage;
        const int maxRadius = 2 + stage;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                putPixel(img, tileIdx, x, y, 0, 0, 0, 0);
                const int dx = x - 8;
                const int dy = y - 8;
                const int dist = static_cast<int>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
                if (dist <= maxRadius) {
                    const bool onCrackBranch =
                        (std::abs(dx - dy) <= 1) ||
                        (std::abs(dx + dy) <= 1) ||
                        (stage >= 4 && (x == 8 || y == 8)) ||
                        (stage >= 7 && ((x + y) % 3 == 0));
                    if (onCrackBranch && ((pixelHash(x, y, 2400U + static_cast<uint32_t>(stage)) % 3U) != 0U)) {
                        putPixel(img, tileIdx, x, y, 18, 18, 18, 185);
                    }
                }
            }
        }
    }

    // Tiles 224..236 and 250..253: mob skins
    drawMobSkinTiles(img);
    // S6.7 armor icons (238, 239, 254, 223) and the S6.10 mob spawner (65)
    drawArmorTiles(img);
    drawMobSpawnerTile(img);

    return img;
}

bool TextureAtlas::loadPngRGBA(const std::filesystem::path& path, std::vector<uint8_t>& outRgba) {
#if defined(BETACRAFT_HAS_LIBPNG)
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return false;
    }

    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::fclose(fp);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        std::fclose(fp);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    const png_uint_32 width = png_get_image_width(png, info);
    const png_uint_32 height = png_get_image_height(png, info);
    const png_byte colorType = png_get_color_type(png, info);
    const png_byte bitDepth = png_get_bit_depth(png, info);

    if (width != ATLAS_SIZE || height != ATLAS_SIZE) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        return false;
    }

    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    png_read_update_info(png, info);

    outRgba.resize(static_cast<size_t>(ATLAS_SIZE * ATLAS_SIZE * 4));
    std::vector<png_bytep> rowPointers(ATLAS_SIZE);
    for (int y = 0; y < ATLAS_SIZE; ++y) {
        rowPointers[static_cast<size_t>(y)] = outRgba.data() + y * ATLAS_SIZE * 4;
    }

    png_read_image(png, rowPointers.data());
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(fp);
    return true;
#else
    (void)path;
    (void)outRgba;
    return false;
#endif
}

bool TextureAtlas::savePngRGBA(const std::filesystem::path& path, const std::vector<uint8_t>& rgba) {
#if defined(BETACRAFT_HAS_LIBPNG)
    if (rgba.size() != static_cast<size_t>(ATLAS_SIZE * ATLAS_SIZE * 4)) {
        return false;
    }
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::fclose(fp);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        std::fclose(fp);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_set_IHDR(
        png, info, ATLAS_SIZE, ATLAS_SIZE, 8,
        PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT
    );
    png_write_info(png, info);

    std::vector<png_bytep> rowPointers(ATLAS_SIZE);
    for (int y = 0; y < ATLAS_SIZE; ++y) {
        rowPointers[static_cast<size_t>(y)] = const_cast<png_bytep>(rgba.data() + y * ATLAS_SIZE * 4);
    }

    png_write_image(png, rowPointers.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(fp);
    return true;
#else
    (void)path;
    (void)rgba;
    return false;
#endif
}

namespace {

constexpr size_t kAtlasBytes = static_cast<size_t>(TextureAtlas::ATLAS_SIZE) * TextureAtlas::ATLAS_SIZE * 4U;

[[nodiscard]] size_t tilePixelOffset(uint8_t tile, int x, int y) noexcept {
    const int tx = (tile % TextureAtlas::TILES_PER_ROW) * TextureAtlas::TILE_SIZE + x;
    const int ty = (tile / TextureAtlas::TILES_PER_ROW) * TextureAtlas::TILE_SIZE + y;
    return (static_cast<size_t>(ty) * TextureAtlas::ATLAS_SIZE + static_cast<size_t>(tx)) * 4U;
}

} // namespace

bool TextureAtlas::isPlaceholderTile(const std::vector<uint8_t>& rgba, uint8_t tile) {
    if (rgba.size() != kAtlasBytes) {
        return false;
    }
    for (int y = 0; y < TILE_SIZE; ++y) {
        for (int x = 0; x < TILE_SIZE; ++x) {
            const size_t i = tilePixelOffset(tile, x, y);
            if (rgba[i] != 128 || rgba[i + 1] != 128 || rgba[i + 2] != 128 || rgba[i + 3] != 255) {
                return false;
            }
        }
    }
    return true;
}

size_t TextureAtlas::fillPlaceholderTiles(std::vector<uint8_t>& loaded, const std::vector<uint8_t>& procedural) {
    if (loaded.size() != kAtlasBytes || procedural.size() != kAtlasBytes) {
        return 0;
    }
    size_t filled = 0;
    for (int t = 0; t < TILES_PER_ROW * TILES_PER_ROW; ++t) {
        const auto tile = static_cast<uint8_t>(t);
        if (!isPlaceholderTile(loaded, tile) || isPlaceholderTile(procedural, tile)) {
            continue;
        }
        for (int y = 0; y < TILE_SIZE; ++y) {
            const size_t row = tilePixelOffset(tile, 0, y);
            std::copy_n(procedural.begin() + static_cast<std::ptrdiff_t>(row), TILE_SIZE * 4,
                        loaded.begin() + static_cast<std::ptrdiff_t>(row));
        }
        ++filled;
    }
    return filled;
}

bool TextureAtlas::initialize(const std::filesystem::path& externalPngPath) {
    std::vector<uint8_t> rgba;
    if (loadPngRGBA(externalPngPath, rgba)) {
        core::Logger::info("TextureAtlas", "Loaded external texture atlas from ", externalPngPath.string());
        if (const size_t filled = fillPlaceholderTiles(rgba, generateProceduralAtlasRGBA()); filled > 0) {
            core::Logger::info("TextureAtlas", "Filled ", filled, " placeholder tiles with procedural icons.");
        }
    } else {
        rgba = generateProceduralAtlasRGBA();
        if (savePngRGBA(externalPngPath, rgba)) {
            core::Logger::info("TextureAtlas", "Generated procedural atlas and saved to ", externalPngPath.string());
        } else {
            core::Logger::info("TextureAtlas", "Generated procedural atlas in memory (256x256 RGBA).");
        }
    }

    if (m_textureId != 0) {
        glDeleteTextures(1, &m_textureId);
    }

    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);

    // Authentic pixel-art nearest-neighbor filtering
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA8,
        ATLAS_SIZE, ATLAS_SIZE, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, rgba.data()
    );

    glBindTexture(GL_TEXTURE_2D, 0);
    return m_textureId != 0;
}

void TextureAtlas::bind(GLuint unit) const noexcept {
    bc_glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
}

} // namespace betacraft::render
