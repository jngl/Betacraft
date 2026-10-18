// S4.8: cube models of the Sprint 4 mobs (render/MobModels.hpp), built on the CPU without GL;
// S6.3 adds the skeleton, spider, creeper and cow, the creeper's swell and the S6.2 arrow.
#include "gameplay/MobDefs.hpp"
#include "render/MobModels.hpp"
#include "render/TextureAtlas.hpp"
#include "tests/test_helpers.hpp"
#include "world/Block.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace betacraft;
using namespace betacraft::render;
using gameplay::MobKind;

namespace {

struct Bounds {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};
};

Bounds boundsOf(const std::vector<ChunkVertex>& verts, size_t first = 0, size_t count = SIZE_MAX) {
    Bounds b;
    const size_t last = std::min(verts.size(), count == SIZE_MAX ? verts.size() : first + count);
    for (size_t i = first; i < last; ++i) {
        b.min = glm::min(b.min, verts[i].position);
        b.max = glm::max(b.max, verts[i].position);
    }
    return b;
}

class MobModelsTest : public ::testing::Test {
protected:
    void SetUp() override {
        world::BlockRegistry::initialize();
        m_world = std::make_unique<world::World>(173ULL, m_dir.path());
        m_world->ensureChunkLoaded({0, 0});
        for (int y = 100; y < 104; ++y) {
            m_world->setSkyLight(kX, y, kZ, 15);
            m_world->setBlockLight(kX, y, kZ, 0);
        }
    }

    [[nodiscard]] std::vector<ChunkVertex> build(const MobRenderInstance& mob) const {
        std::vector<ChunkVertex> verts;
        appendMobModel(verts, *m_world, mob);
        return verts;
    }

    [[nodiscard]] static MobRenderInstance at(MobKind kind) {
        MobRenderInstance mob;
        mob.pos = kFeet;
        mob.kind = kind;
        return mob;
    }

    static constexpr int kX = 8;
    static constexpr int kZ = 8;
    static inline const glm::vec3 kFeet{8.5f, 100.0f, 8.5f};

    test::TempDir m_dir;
    std::unique_ptr<world::World> m_world;
};

constexpr MobKind kKinds[] = {MobKind::Pig,     MobKind::Sheep,   MobKind::Zombie, MobKind::Skeleton,
                              MobKind::Spider,  MobKind::Creeper, MobKind::Cow};

// Index of the head box: last for the bipeds, second for everything else.
size_t headBoxOf(MobKind kind) {
    return kind == MobKind::Zombie || kind == MobKind::Skeleton ? 5 : 1;
}

} // namespace

TEST_F(MobModelsTest, EveryKindBuildsWholeBoxes) {
    for (const MobKind kind : kKinds) {
        const std::vector<ChunkVertex> verts = build(at(kind));
        EXPECT_EQ(mobModelBoxCount(kind), kind == MobKind::Spider ? 11U : 6U) << static_cast<int>(kind);
        EXPECT_EQ(verts.size(), mobModelBoxCount(kind) * 36U) << static_cast<int>(kind);
    }
    EXPECT_EQ(mobModelBoxCount(MobKind::Count), 0U);
    EXPECT_TRUE(build(at(MobKind::Count)).empty());
}

TEST_F(MobModelsTest, AppendsAfterExistingVertices) {
    std::vector<ChunkVertex> verts(5);
    appendMobModel(verts, *m_world, at(MobKind::Pig));
    EXPECT_EQ(verts.size(), 5U + 6U * 36U);
}

TEST_F(MobModelsTest, ModelStandsOnTheFeetAndMatchesTheHitboxHeight) {
    for (const MobKind kind : kKinds) {
        const gameplay::MobDef& def = gameplay::mobDef(kind);
        const Bounds b = boundsOf(build(at(kind)));
        EXPECT_NEAR(b.min.y, kFeet.y, 1e-4f) << def.name;
        // Beta models are a little taller than their hitbox (the biped is 2 blocks for 1.8).
        EXPECT_GT(b.max.y - b.min.y, def.height * 0.9f) << def.name;
        EXPECT_LT(b.max.y - b.min.y, def.height * 1.25f) << def.name;
        // Centred on the feet within about one block.
        EXPECT_LT(std::abs((b.min.x + b.max.x) * 0.5f - kFeet.x), 0.5f) << def.name;
        EXPECT_LT(std::abs((b.min.z + b.max.z) * 0.5f - kFeet.z), 0.5f) << def.name;
    }
}

TEST_F(MobModelsTest, YawTurnsTheModelLikeTransformComp) {
    // yaw 0 faces +X: a pig's head reaches further along +X than its rump does along -X.
    MobRenderInstance pig = at(MobKind::Pig);
    Bounds b = boundsOf(build(pig));
    EXPECT_GT(b.max.x - kFeet.x, kFeet.x - b.min.x);
    EXPECT_LT(b.max.z - b.min.z, b.max.x - b.min.x);

    pig.yaw = 90.0f; // faces +Z
    b = boundsOf(build(pig));
    EXPECT_GT(b.max.z - kFeet.z, kFeet.z - b.min.z);
    EXPECT_LT(b.max.x - b.min.x, b.max.z - b.min.z);

    // The zombie's raised arms point the way it faces.
    MobRenderInstance zombie = at(MobKind::Zombie);
    zombie.yaw = 180.0f; // faces -X
    b = boundsOf(build(zombie));
    EXPECT_GT(kFeet.x - b.min.x, 0.5f);
    EXPECT_LT(b.max.x - kFeet.x, 0.3f); // Back of the head
}

TEST_F(MobModelsTest, HurtAndDyingMobsAreTintedRed) {
    for (const MobKind kind : kKinds) {
        const std::vector<ChunkVertex> normal = build(at(kind));
        MobRenderInstance hurtMob = at(kind);
        hurtMob.hurt = 0.5f;
        const std::vector<ChunkVertex> hurt = build(hurtMob);
        ASSERT_EQ(normal.size(), hurt.size());
        for (size_t i = 0; i < normal.size(); ++i) {
            EXPECT_NEAR(hurt[i].color.r, normal[i].color.r * kMobHurtTint.r, 1e-5f);
            EXPECT_NEAR(hurt[i].color.g, normal[i].color.g * kMobHurtTint.g, 1e-5f);
            EXPECT_NEAR(hurt[i].color.b, normal[i].color.b * kMobHurtTint.b, 1e-5f);
        }
        MobRenderInstance dying = at(kind);
        dying.death = 0.3f;
        EXPECT_NEAR(build(dying)[0].color.g, normal[0].color.g * 0.4f, 1e-5f);
    }
}

TEST_F(MobModelsTest, DeathTipsTheBodyOverByNinetyDegrees) {
    MobRenderInstance zombie = at(MobKind::Zombie);
    const Bounds standing = boundsOf(build(zombie));
    zombie.death = 1.0f;
    const Bounds lying = boundsOf(build(zombie));
    // Lying on its side: the 2-block body now spans Z, and still rests on the ground; only
    // its shoulder width (1 block, arm to arm) stays vertical.
    EXPECT_LT(lying.max.y - lying.min.y, 1.05f);
    EXPECT_GT(lying.max.z - lying.min.z, 1.9f);
    EXPECT_NEAR(lying.min.y, kFeet.y, 1e-4f);
    EXPECT_GT(standing.max.y - standing.min.y, 1.9f);

    zombie.death = 0.5f;
    const Bounds halfway = boundsOf(build(zombie));
    EXPECT_LT(halfway.max.y, standing.max.y - 0.1f);
    EXPECT_GT(halfway.max.y, lying.max.y + 0.1f);
    EXPECT_NEAR(halfway.min.y, kFeet.y, 1e-4f);
}

TEST_F(MobModelsTest, WalkPhaseSwingsTheLegsOnly) {
    for (const MobKind kind : kKinds) {
        MobRenderInstance mob = at(kind);
        const std::vector<ChunkVertex> rest = build(mob);
        mob.walkPhase = 1.5707963f; // Full stride
        const std::vector<ChunkVertex> stride = build(mob);
        ASSERT_EQ(rest.size(), stride.size());

        // The legs reach out along the facing axis...
        EXPECT_GT(boundsOf(stride).max.x - boundsOf(stride).min.x, boundsOf(rest).max.x - boundsOf(rest).min.x - 1e-4f);
        size_t moved = 0;
        for (size_t i = 0; i < rest.size(); ++i) {
            if (glm::length(rest[i].position - stride[i].position) > 1e-4f) ++moved;
        }
        EXPECT_GT(moved, 0U) << static_cast<int>(kind);
        // ...while the head (the last box of the zombie, the second of the quadrupeds) stays put.
        const size_t headBox = headBoxOf(kind);
        for (size_t i = headBox * 36; i < (headBox + 1) * 36; ++i) {
            EXPECT_NEAR(glm::length(rest[i].position - stride[i].position), 0.0f, 1e-4f);
        }
    }
}

TEST_F(MobModelsTest, LitFromTheCellAtMidHeight) {
    MobRenderInstance zombie = at(MobKind::Zombie); // middle at y = 100.9
    m_world->setBlockLight(kX, 100, kZ, 12);
    m_world->setSkyLight(kX, 100, kZ, 4);
    for (const ChunkVertex& v : build(zombie)) {
        EXPECT_FLOAT_EQ(v.light.x, 4.0f / 15.0f);
        EXPECT_FLOAT_EQ(v.light.y, 12.0f / 15.0f);
    }
    MobRenderInstance pig = at(MobKind::Pig);
    pig.pos.y = 102.0f; // middle at y = 102.45
    for (const ChunkVertex& v : build(pig)) {
        EXPECT_FLOAT_EQ(v.light.x, 1.0f);
        EXPECT_FLOAT_EQ(v.light.y, 0.0f);
    }
}

TEST_F(MobModelsTest, UsesOnlyTheProceduralMobTiles) {
    const std::vector<uint8_t> atlas = TextureAtlas::generateProceduralAtlasRGBA();
    for (int t = 0; t < 256; ++t) {
        if (isMobTile(static_cast<uint8_t>(t))) {
            EXPECT_FALSE(TextureAtlas::isPlaceholderTile(atlas, static_cast<uint8_t>(t))) << t;
        }
    }
    for (const MobKind kind : kKinds) {
        for (const ChunkVertex& v : build(at(kind))) {
            bool inMobTile = false;
            for (int t = 0; t < 256 && !inMobTile; ++t) {
                if (!isMobTile(static_cast<uint8_t>(t))) continue;
                const glm::vec4 uv = TextureAtlas::getTileUV(static_cast<uint8_t>(t));
                inMobTile = v.uv.x >= uv.x - 1e-6f && v.uv.x <= uv.z + 1e-6f &&
                            v.uv.y >= uv.y - 1e-6f && v.uv.y <= uv.w + 1e-6f;
            }
            EXPECT_TRUE(inMobTile);
        }
    }
}

TEST_F(MobModelsTest, NewMobTilesAvoidLavaAndTheCrackOverlays) {
    EXPECT_FALSE(isMobTile(237)); // Lava
    for (uint8_t t = 240; t < 250; ++t) EXPECT_FALSE(isMobTile(t)) << int(t);
    for (const uint8_t t : {kSkeletonBoneTile, kSkeletonFaceTile, kSpiderBodyTile, kSpiderFaceTile, kCreeperSkinTile,
                            kCreeperFaceTile, kCowHideTile, kCowFaceTile}) {
        EXPECT_TRUE(isMobTile(t)) << int(t);
    }
}

TEST_F(MobModelsTest, SpiderIsWideAndLowWithLegsOnTheGround) {
    const Bounds b = boundsOf(build(at(MobKind::Spider)));
    EXPECT_NEAR(b.min.y, kFeet.y, 1e-4f);
    EXPECT_LT(b.max.y - b.min.y, 1.0f);
    // The legs spread wider than the 1.4 block hitbox is deep... roughly.
    EXPECT_GT(b.max.z - b.min.z, 1.2f);
    // Walking sweeps the legs but keeps them on the ground.
    MobRenderInstance walking = at(MobKind::Spider);
    walking.walkPhase = 1.0f;
    EXPECT_NEAR(boundsOf(build(walking)).min.y, kFeet.y, 1e-4f);
}

TEST_F(MobModelsTest, CreeperSwellsAndFlashesWhenLit) {
    const std::vector<ChunkVertex> calm = build(at(MobKind::Creeper));
    MobRenderInstance lit = at(MobKind::Creeper);
    lit.swell = 1.0f;
    lit.flash = 1.0f;
    const std::vector<ChunkVertex> swollen = build(lit);
    ASSERT_EQ(calm.size(), swollen.size());
    const Bounds a = boundsOf(calm);
    const Bounds b = boundsOf(swollen);
    EXPECT_NEAR((b.max.y - b.min.y) / (a.max.y - a.min.y), kCreeperMaxSwell, 1e-3f);
    EXPECT_NEAR((b.max.z - b.min.z) / (a.max.z - a.min.z), kCreeperMaxSwell, 1e-3f);
    EXPECT_NEAR(b.min.y, kFeet.y, 1e-4f); // Still standing on its feet
    for (size_t i = 0; i < calm.size(); ++i) {
        EXPECT_NEAR(swollen[i].color.g, calm[i].color.g * 3.0f, 1e-4f); // Over-bright white flash
        EXPECT_FLOAT_EQ(swollen[i].light.x, 1.0f);
    }
}

TEST_F(MobModelsTest, ArrowPointsAlongItsFlight) {
    std::vector<ChunkVertex> verts;
    ArrowRenderInstance arrow{kFeet + glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f};
    appendArrowModel(verts, *m_world, arrow);
    ASSERT_EQ(verts.size(), arrowModelBoxCount() * 36U);
    Bounds b = boundsOf(verts);
    EXPECT_NEAR(b.max.x - b.min.x, kArrowModelLength + 1.0f / 32.0f, 0.02f);
    EXPECT_LT(b.max.z - b.min.z, 0.2f);

    verts.clear();
    arrow.yaw = 90.0f; // along +Z
    appendArrowModel(verts, *m_world, arrow);
    b = boundsOf(verts);
    EXPECT_GT(b.max.z - b.min.z, kArrowModelLength);
    EXPECT_LT(b.max.x - b.min.x, 0.2f);

    verts.clear();
    arrow.yaw = 0.0f;
    arrow.pitch = 90.0f; // straight up
    appendArrowModel(verts, *m_world, arrow);
    b = boundsOf(verts);
    EXPECT_GT(b.max.y - b.min.y, kArrowModelLength);
    EXPECT_LT(b.max.x - b.min.x, 0.2f);
}
