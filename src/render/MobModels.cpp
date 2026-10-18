#include "render/MobModels.hpp"
#include "render/TextureAtlas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace betacraft::render {

namespace {

using gameplay::MobKind;

// Model space is in Beta model pixels (1/16 block): +X is the facing direction, +Y up, +Z
// the mob's right-hand side, the origin between the feet.
constexpr float kPixel = 1.0f / 16.0f;
constexpr float kHalfPi = 1.5707963f;

// Limb swing amplitudes (radians) at full stride; Beta ModelQuadruped/ModelBiped swing by
// up to 1.4 * limbSwingAmount, which a walking mob rarely reaches.
constexpr float kQuadrupedLegSwing = 0.8f;
constexpr float kBipedLegSwing = 0.7f;

struct ModelBox {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    std::array<uint8_t, 6> tiles{}; // PosX, NegX, PosY, NegY, PosZ, NegZ
    glm::vec3 pivot{0.0f};          // Swing pivot (a hip or a shoulder)
    float swing{0.0f};              // Radians about the side (Z) axis; > 0 moves the far end forward
    // S6.3 spider legs, applied before `swing` about the same pivot: `roll` about the facing
    // (X) axis (> 0 lowers the +Z end), then `turn` about the vertical axis (> 0 moves the +Z
    // end forward).
    float roll{0.0f};
    float turn{0.0f};
};

constexpr std::array<uint8_t, 6> uniformTiles(uint8_t tile) noexcept {
    return {tile, tile, tile, tile, tile, tile};
}

constexpr std::array<uint8_t, 6> headTiles(uint8_t face, uint8_t sides, uint8_t top, uint8_t bottom) noexcept {
    return {face, sides, top, bottom, sides, sides};
}

// Same face shades as the dropped-item cubes (WorldRenderer pushCubeFaces).
void emitBox(std::vector<ChunkVertex>& out, const ModelBox& box, const glm::vec3& tint, const glm::vec2& light) {
    const glm::vec3& lo = box.min;
    const glm::vec3& hi = box.max;
    auto emitFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, uint8_t tile, float shade) {
        const glm::vec4 uv = TextureAtlas::getTileUV(tile);
        const glm::vec3 col = tint * shade;
        out.push_back({a, {uv.x, uv.w}, col, light});
        out.push_back({b, {uv.z, uv.w}, col, light});
        out.push_back({c, {uv.z, uv.y}, col, light});
        out.push_back({a, {uv.x, uv.w}, col, light});
        out.push_back({c, {uv.z, uv.y}, col, light});
        out.push_back({d, {uv.x, uv.y}, col, light});
    };
    emitFace({hi.x, lo.y, hi.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, box.tiles[0], 0.65f);
    emitFace({lo.x, lo.y, lo.z}, {lo.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {lo.x, hi.y, lo.z}, box.tiles[1], 0.65f);
    emitFace({lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z}, box.tiles[2], 1.00f);
    emitFace({lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z}, box.tiles[3], 0.50f);
    emitFace({lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}, box.tiles[4], 0.85f);
    emitFace({hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z}, box.tiles[5], 0.85f);

    if (box.roll != 0.0f || box.turn != 0.0f) {
        const float rc = std::cos(box.roll);
        const float rs = std::sin(box.roll);
        const float tc = std::cos(box.turn);
        const float ts = std::sin(box.turn);
        for (auto it = out.end() - 36; it != out.end(); ++it) {
            glm::vec3 r = it->position - box.pivot;
            r = glm::vec3(r.x, rc * r.y - rs * r.z, rs * r.y + rc * r.z);
            r = glm::vec3(tc * r.x + ts * r.z, r.y, -ts * r.x + tc * r.z);
            it->position = box.pivot + r;
        }
    }
    if (box.swing != 0.0f) {
        const float c = std::cos(box.swing);
        const float s = std::sin(box.swing);
        for (auto it = out.end() - 36; it != out.end(); ++it) {
            const glm::vec3 r = it->position - box.pivot;
            it->position = box.pivot + glm::vec3(c * r.x - s * r.y, s * r.x + c * r.y, r.z);
        }
    }
}

// Four legs of `size` (x, y, z) under a quadruped body: legs at +-legX along the body and
// +-legZ to the sides. Diagonal pairs swing together, as in Beta ModelQuadruped.
void addQuadrupedLegs(std::vector<ModelBox>& boxes, float legX, float legZ, glm::vec3 size, uint8_t tile, float swing) {
    for (const float sx : {1.0f, -1.0f}) {
        for (const float sz : {-1.0f, 1.0f}) {
            const glm::vec3 centre(sx * legX, 0.0f, sz * legZ);
            ModelBox leg;
            leg.min = centre + glm::vec3(-size.x * 0.5f, 0.0f, -size.z * 0.5f);
            leg.max = centre + glm::vec3(size.x * 0.5f, size.y, size.z * 0.5f);
            leg.tiles = uniformTiles(tile);
            leg.pivot = glm::vec3(centre.x, size.y, centre.z);
            leg.swing = (sx * sz < 0.0f) ? swing : -swing;
            boxes.push_back(leg);
        }
    }
}

void buildModel(std::vector<ModelBox>& boxes, MobKind kind, float walkPhase) {
    const float stride = std::sin(walkPhase);
    switch (kind) {
    case MobKind::Pig:
        // Beta ModelPig: 10x8x16 body on 6 px legs, 8 px head ahead of it.
        boxes.push_back({{-8.0f, 6.0f, -5.0f}, {8.0f, 14.0f, 5.0f}, uniformTiles(kPigSkinTile), {}, 0.0f});
        boxes.push_back({{6.0f, 8.0f, -4.0f}, {14.0f, 16.0f, 4.0f},
                         headTiles(kPigFaceTile, kPigSkinTile, kPigSkinTile, kPigSkinTile), {}, 0.0f});
        addQuadrupedLegs(boxes, 5.0f, 3.0f, {4.0f, 6.0f, 4.0f}, kPigSkinTile, stride * kQuadrupedLegSwing);
        break;
    case MobKind::Sheep:
        // Beta ModelSheep1/2: fleeced body on 12 px legs, a woolly 6x6x8 head.
        boxes.push_back({{-9.0f, 11.0f, -5.5f}, {9.0f, 21.0f, 5.5f}, uniformTiles(kSheepWoolTile), {}, 0.0f});
        boxes.push_back({{7.0f, 15.0f, -3.0f}, {15.0f, 21.0f, 3.0f},
                         headTiles(kSheepFaceTile, kSheepWoolTile, kSheepWoolTile, kSheepSkinTile), {}, 0.0f});
        addQuadrupedLegs(boxes, 5.0f, 3.0f, {4.0f, 12.0f, 4.0f}, kSheepSkinTile, stride * kQuadrupedLegSwing);
        break;
    case MobKind::Zombie: {
        // Beta ModelZombie: a biped whose arms stay raised straight ahead.
        const float legSwing = stride * kBipedLegSwing;
        boxes.push_back({{-2.0f, 0.0f, -4.0f}, {2.0f, 12.0f, 0.0f}, uniformTiles(kZombieTrousersTile), {0.0f, 12.0f, -2.0f}, legSwing});
        boxes.push_back({{-2.0f, 0.0f, 0.0f}, {2.0f, 12.0f, 4.0f}, uniformTiles(kZombieTrousersTile), {0.0f, 12.0f, 2.0f}, -legSwing});
        boxes.push_back({{-2.0f, 12.0f, -4.0f}, {2.0f, 24.0f, 4.0f}, uniformTiles(kZombieShirtTile), {}, 0.0f});
        const float armBob = stride * 0.05f;
        boxes.push_back({{-2.0f, 12.0f, -8.0f}, {2.0f, 24.0f, -4.0f}, uniformTiles(kZombieSkinTile), {0.0f, 22.0f, -6.0f}, kHalfPi + armBob});
        boxes.push_back({{-2.0f, 12.0f, 4.0f}, {2.0f, 24.0f, 8.0f}, uniformTiles(kZombieSkinTile), {0.0f, 22.0f, 6.0f}, kHalfPi - armBob});
        boxes.push_back({{-4.0f, 24.0f, -4.0f}, {4.0f, 32.0f, 4.0f},
                         headTiles(kZombieFaceTile, kZombieSkinTile, kZombieSkinTile, kZombieSkinTile), {}, 0.0f});
        break;
    }
    case MobKind::Skeleton: {
        // Beta ModelSkeleton: the zombie's pose on 2 px limbs.
        const float legSwing = stride * kBipedLegSwing;
        boxes.push_back({{-1.0f, 0.0f, -3.0f}, {1.0f, 12.0f, -1.0f}, uniformTiles(kSkeletonBoneTile), {0.0f, 12.0f, -2.0f}, legSwing});
        boxes.push_back({{-1.0f, 0.0f, 1.0f}, {1.0f, 12.0f, 3.0f}, uniformTiles(kSkeletonBoneTile), {0.0f, 12.0f, 2.0f}, -legSwing});
        boxes.push_back({{-2.0f, 12.0f, -4.0f}, {2.0f, 24.0f, 4.0f}, uniformTiles(kSkeletonBoneTile), {}, 0.0f});
        const float armBob = stride * 0.05f;
        boxes.push_back({{-1.0f, 12.0f, -6.0f}, {1.0f, 24.0f, -4.0f}, uniformTiles(kSkeletonBoneTile), {0.0f, 22.0f, -5.0f}, kHalfPi + armBob});
        boxes.push_back({{-1.0f, 12.0f, 4.0f}, {1.0f, 24.0f, 6.0f}, uniformTiles(kSkeletonBoneTile), {0.0f, 22.0f, 5.0f}, kHalfPi - armBob});
        boxes.push_back({{-4.0f, 24.0f, -4.0f}, {4.0f, 32.0f, 4.0f},
                         headTiles(kSkeletonFaceTile, kSkeletonBoneTile, kSkeletonBoneTile, kSkeletonBoneTile), {}, 0.0f});
        break;
    }
    case MobKind::Spider: {
        // Beta ModelSpider: a thorax between the head and the abdomen, four legs per side
        // sloping down from the thorax to the ground and fanned out, sweeping as it walks.
        boxes.push_back({{-3.0f, 6.0f, -3.0f}, {3.0f, 12.0f, 3.0f}, uniformTiles(kSpiderBodyTile), {}, 0.0f});
        boxes.push_back({{3.0f, 5.0f, -4.0f}, {11.0f, 13.0f, 4.0f},
                         headTiles(kSpiderFaceTile, kSpiderBodyTile, kSpiderBodyTile, kSpiderBodyTile), {}, 0.0f});
        boxes.push_back({{-15.0f, 6.0f, -5.0f}, {-3.0f, 14.0f, 5.0f}, uniformTiles(kSpiderBodyTile), {}, 0.0f});
        constexpr float kHipY = 9.0f;
        constexpr float kSlope = 0.75f;
        // Long enough for the low far corner of the 2 px thick leg to touch the ground.
        const float length = (kHipY - std::cos(kSlope)) / std::sin(kSlope);
        constexpr std::array<float, 4> kHipX{2.0f, 0.7f, -0.7f, -2.0f};
        constexpr std::array<float, 4> kFan{0.6f, 0.2f, -0.2f, -0.6f};
        for (const float side : {1.0f, -1.0f}) {
            for (size_t i = 0; i < 4; ++i) {
                const float sweep = ((i % 2 == 0) == (side > 0.0f) ? 1.0f : -1.0f) * stride * 0.4f;
                ModelBox leg;
                leg.pivot = {kHipX[i], kHipY, side * 3.0f};
                leg.min = {kHipX[i] - 1.0f, kHipY - 1.0f, side > 0.0f ? 3.0f : -3.0f - length};
                leg.max = {kHipX[i] + 1.0f, kHipY + 1.0f, side > 0.0f ? 3.0f + length : -3.0f};
                leg.tiles = uniformTiles(kSpiderBodyTile);
                leg.roll = side * kSlope;
                leg.turn = side * (kFan[i] + sweep);
                boxes.push_back(leg);
            }
        }
        break;
    }
    case MobKind::Creeper:
        // Beta ModelCreeper: a 4 px deep body on four 6 px legs, an 8 px head on top.
        boxes.push_back({{-2.0f, 6.0f, -4.0f}, {2.0f, 18.0f, 4.0f}, uniformTiles(kCreeperSkinTile), {}, 0.0f});
        boxes.push_back({{-4.0f, 18.0f, -4.0f}, {4.0f, 26.0f, 4.0f},
                         headTiles(kCreeperFaceTile, kCreeperSkinTile, kCreeperSkinTile, kCreeperSkinTile), {}, 0.0f});
        addQuadrupedLegs(boxes, 4.0f, 2.0f, {4.0f, 6.0f, 4.0f}, kCreeperSkinTile, stride * kQuadrupedLegSwing);
        break;
    case MobKind::Cow:
        // Beta ModelCow: a 18 px long body on 12 px legs, a 6 px deep head ahead of it.
        boxes.push_back({{-9.0f, 12.0f, -6.0f}, {9.0f, 22.0f, 6.0f}, uniformTiles(kCowHideTile), {}, 0.0f});
        boxes.push_back({{8.0f, 14.0f, -4.0f}, {14.0f, 22.0f, 4.0f},
                         headTiles(kCowFaceTile, kCowHideTile, kCowHideTile, kCowHideTile), {}, 0.0f});
        addQuadrupedLegs(boxes, 6.0f, 4.0f, {4.0f, 12.0f, 4.0f}, kCowHideTile, stride * kQuadrupedLegSwing);
        break;
    case MobKind::Count:
        break;
    }
}

} // namespace

size_t mobModelBoxCount(gameplay::MobKind kind) noexcept {
    switch (kind) {
    case MobKind::Pig:
    case MobKind::Sheep:
    case MobKind::Zombie:
    case MobKind::Skeleton:
    case MobKind::Creeper:
    case MobKind::Cow:
        return 6;
    case MobKind::Spider:
        return 11;
    case MobKind::Count:
        break;
    }
    return 0;
}

void appendMobModel(std::vector<ChunkVertex>& out, const world::World& world, const MobRenderInstance& mob) {
    if (mob.kind >= MobKind::Count) {
        return;
    }
    std::vector<ModelBox> boxes;
    boxes.reserve(mobModelBoxCount(mob.kind));
    buildModel(boxes, mob.kind, mob.walkPhase);

    const gameplay::MobDef& def = gameplay::mobDef(mob.kind);
    const glm::vec3 middle = mob.pos + glm::vec3(0.0f, def.height * 0.5f, 0.0f);
    const auto cx = static_cast<int32_t>(std::floor(middle.x));
    const auto cy = static_cast<int32_t>(std::floor(middle.y));
    const auto cz = static_cast<int32_t>(std::floor(middle.z));
    glm::vec2 light(static_cast<float>(world.getSkyLight(cx, cy, cz)) / 15.0f,
                    static_cast<float>(world.getBlockLight(cx, cy, cz)) / 15.0f);
    const float death = std::clamp(mob.death, 0.0f, 1.0f);
    glm::vec3 tint = (mob.hurt > 0.0f || death > 0.0f) ? kMobHurtTint : glm::vec3(1.0f);
    // Beta RenderCreeper: a white overlay; here full light and an over-bright tint, as for TNT.
    const float flash = std::clamp(mob.flash, 0.0f, 1.0f);
    if (flash > 0.0f) {
        light = glm::mix(light, glm::vec2(1.0f), flash);
        tint *= 1.0f + 2.0f * flash;
    }
    const float scale = kPixel * (1.0f + (kCreeperMaxSwell - 1.0f) * std::clamp(mob.swell, 0.0f, 1.0f));

    const size_t first = out.size();
    out.reserve(first + boxes.size() * 36);
    for (const ModelBox& box : boxes) {
        emitBox(out, box, tint, light);
    }
    const std::span<ChunkVertex> verts(out.data() + first, out.size() - first);

    // Death: Beta RenderLiving rolls the body onto its side about its own forward axis, then
    // keeps it resting on the ground.
    float lift = 0.0f;
    const float roll = death * kHalfPi;
    const float rc = std::cos(roll);
    const float rs = std::sin(roll);
    for (ChunkVertex& v : verts) {
        glm::vec3 p = v.position * scale;
        if (death > 0.0f) {
            p = glm::vec3(p.x, rc * p.y - rs * p.z, rs * p.y + rc * p.z);
            lift = std::max(lift, -p.y);
        }
        v.position = p;
    }

    const float yaw = glm::radians(mob.yaw);
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    for (ChunkVertex& v : verts) {
        const glm::vec3 p = v.position + glm::vec3(0.0f, lift, 0.0f);
        v.position = mob.pos + glm::vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z);
    }
}

size_t arrowModelBoxCount() noexcept {
    return 3;
}

void appendArrowModel(std::vector<ChunkVertex>& out, const world::World& world, const ArrowRenderInstance& arrow) {
    // Model pixels along +X, centred on the middle of the shaft: a 1 px shaft, a stone head and
    // white fletching (block tiles: planks 4, stone 1, white wool 64).
    const float half = kArrowModelLength * 8.0f; // half the length, in pixels
    std::array<ModelBox, 3> boxes{};
    boxes[0] = {{-half, -0.5f, -0.5f}, {half, 0.5f, 0.5f}, uniformTiles(4), {}, 0.0f};
    boxes[1] = {{half - 1.5f, -1.0f, -1.0f}, {half + 0.5f, 1.0f, 1.0f}, uniformTiles(1), {}, 0.0f};
    boxes[2] = {{-half, -1.5f, -0.25f}, {-half + 3.0f, 1.5f, 0.25f}, uniformTiles(64), {}, 0.0f};

    const auto cx = static_cast<int32_t>(std::floor(arrow.pos.x));
    const auto cy = static_cast<int32_t>(std::floor(arrow.pos.y));
    const auto cz = static_cast<int32_t>(std::floor(arrow.pos.z));
    const glm::vec2 light(static_cast<float>(world.getSkyLight(cx, cy, cz)) / 15.0f,
                          static_cast<float>(world.getBlockLight(cx, cy, cz)) / 15.0f);
    const size_t first = out.size();
    out.reserve(first + boxes.size() * 36);
    for (const ModelBox& box : boxes) {
        emitBox(out, box, glm::vec3(1.0f), light);
    }
    const float pitch = glm::radians(arrow.pitch);
    const float pc = std::cos(pitch);
    const float ps = std::sin(pitch);
    const float yaw = glm::radians(arrow.yaw);
    const float yc = std::cos(yaw);
    const float ys = std::sin(yaw);
    for (size_t i = first; i < out.size(); ++i) {
        glm::vec3 p = out[i].position * kPixel;
        p = glm::vec3(pc * p.x - ps * p.y, ps * p.x + pc * p.y, p.z);
        out[i].position = arrow.pos + glm::vec3(yc * p.x - ys * p.z, p.y, ys * p.x + yc * p.z);
    }
}

} // namespace betacraft::render
