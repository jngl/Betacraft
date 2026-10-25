#pragma once

#include "gameplay/MobDefs.hpp"
#include "render/ChunkMesher.hpp"
#include "world/World.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace betacraft::render {

// Procedural mob skin tiles (TextureAtlas.cpp), on indices no block or item uses. Faces are
// drawn on the PosX side of the head boxes, the side a mob looks towards at yaw 0.
inline constexpr uint8_t kPigSkinTile = 224;
inline constexpr uint8_t kPigFaceTile = 225;
inline constexpr uint8_t kSheepWoolTile = 226;
inline constexpr uint8_t kSheepFaceTile = 227;
inline constexpr uint8_t kSheepSkinTile = 228;
inline constexpr uint8_t kZombieSkinTile = 229;
inline constexpr uint8_t kZombieFaceTile = 230;
inline constexpr uint8_t kZombieShirtTile = 231;
inline constexpr uint8_t kZombieTrousersTile = 232;
// Skins: 233..236 are the first four mobs' tiles, the creeper and the cow use 250..253 (237
// is lava, 240..249 the crack overlays).
inline constexpr uint8_t kSkeletonBoneTile = 233;
inline constexpr uint8_t kSkeletonFaceTile = 234;
inline constexpr uint8_t kSpiderBodyTile = 235;
inline constexpr uint8_t kSpiderFaceTile = 236;
inline constexpr uint8_t kCreeperSkinTile = 250;
inline constexpr uint8_t kCreeperFaceTile = 251;
inline constexpr uint8_t kCowHideTile = 252;
inline constexpr uint8_t kCowFaceTile = 253;
inline constexpr uint8_t kFirstMobTile = kPigSkinTile;
inline constexpr uint8_t kLastMobTile = kSpiderFaceTile;
inline constexpr uint8_t kFirstExtraMobTile = kCreeperSkinTile;
inline constexpr uint8_t kLastExtraMobTile = kCowFaceTile;

// True for the tiles of the two mob skin ranges.
[[nodiscard]] constexpr bool isMobTile(uint8_t tile) noexcept {
    return (tile >= kFirstMobTile && tile <= kLastMobTile) || (tile >= kFirstExtraMobTile && tile <= kLastExtraMobTile);
}

// Beta RenderLiving: a hurt or dying mob is drawn with a red overlay.
inline constexpr glm::vec3 kMobHurtTint{1.0f, 0.4f, 0.4f};

// A mob to draw this frame.
struct MobRenderInstance {
    glm::vec3 pos{0.0f};  // Render-interpolated feet position
    float yaw{0.0f};      // Degrees, ecs::TransformComp convention: 0 faces +X, 90 faces +Z
    gameplay::MobKind kind{gameplay::MobKind::Pig};
    float hurt{0.0f};     // 0..1 left of the hurt window; > 0 tints the model red
    float death{0.0f};    // 0..1 through the death animation; the body tips over by death * 90 deg
    float walkPhase{0.0f}; // Radians; legs (and zombie arms) swing by sin(walkPhase), 0 = at rest
    float swell{0.0f};     // creeper fuse, 0..1: the model grows up to kCreeperMaxSwell
    float flash{0.0f};     // creeper fuse, 0..1: white flash (full light, over-bright tint)
};

// Beta RenderCreeper: a creeper about to explode swells by up to this factor.
inline constexpr float kCreeperMaxSwell = 1.2f;

inline constexpr float kArrowModelLength = 0.5f; // Blocks, tip to fletching

// An arrow to draw this frame: a thin shaft along its flight.
struct ArrowRenderInstance {
    glm::vec3 pos{0.0f}; // Render-interpolated position of the middle of the shaft
    float yaw{0.0f};     // Degrees, ecs::TransformComp convention
    float pitch{0.0f};   // Degrees, > 0 points the tip up
};

// Number of boxes appendMobModel builds for `kind` (36 vertices each).
[[nodiscard]] size_t mobModelBoxCount(gameplay::MobKind kind) noexcept;

// Appends the mob as unindexed triangles in world space: zombies and skeletons get a head,
// torso, outstretched arms and swinging legs; pigs, sheep and cows a body, a head and four
// legs; creepers a body, a head and four short legs; spiders a thorax, a head, an abdomen and
// eight sloping legs. Lit like dropped items, from the sky and block light of the cell at the
// middle of the mob.
void appendMobModel(std::vector<ChunkVertex>& out, const world::World& world, const MobRenderInstance& mob);

// Number of boxes appendArrowModel builds (36 vertices each).
[[nodiscard]] size_t arrowModelBoxCount() noexcept;

// Appends the arrow (shaft, head and fletching, Beta RenderArrow) turned along its yaw and
// pitch, lit from the cell it is in.
void appendArrowModel(std::vector<ChunkVertex>& out, const world::World& world, const ArrowRenderInstance& arrow);

} // namespace betacraft::render
