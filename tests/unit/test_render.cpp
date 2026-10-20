#include "render/ChunkMesher.hpp"
#include "render/ParticleSystem.hpp"
#include "render/Shader.hpp"
#include "render/SkyRenderer.hpp"
#include "render/TextureAtlas.hpp"
#include "render/WorldRenderer.hpp"
#include "tests/test_helpers.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace betacraft;
using namespace betacraft::render;
using namespace testing;

TEST(TextureAtlasTest, TileUVAndAtlasGeneration) {
    const glm::vec4 uv0 = TextureAtlas::getTileUV(0);
    // Verified half-texel inset for bleeding prevention
    EXPECT_NEAR(uv0.x, 0.0f, 1e-3f);
    EXPECT_NEAR(uv0.y, 0.0f, 1e-3f);
    EXPECT_NEAR(uv0.z, 1.0f / 16.0f, 1e-3f);
    EXPECT_NEAR(uv0.w, 1.0f / 16.0f, 1e-3f);
    EXPECT_GT(uv0.x, 0.0f);

    const auto rgba = TextureAtlas::generateProceduralAtlasRGBA();
    EXPECT_EQ(rgba.size(), static_cast<size_t>(256 * 256 * 4));

    // Verify some non-zero pixel in first tile
    bool hasNonZero = false;
    for (size_t i = 0; i < 16 * 16 * 4; ++i) {
        if (rgba[i] > 0) {
            hasNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(hasNonZero);
}

TEST(ChunkMesherTest, FaceCullingAndTranslucency) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;
    world::World world(173ULL, tempDir.path());
    world::Chunk& chunk = world.ensureChunkLoaded({0, 0});

    // Clear all blocks in chunk to isolate meshing test
    for (int y = 0; y < world::Chunk::HEIGHT; ++y) {
        for (int z = 0; z < world::Chunk::DEPTH; ++z) {
            for (int x = 0; x < world::Chunk::WIDTH; ++x) {
                chunk.setBlock(x, y, z, world::BlockId::Air, false);
            }
        }
    }

    // 1. Single Stone block -> 6 faces * 4 vertices = 24 vertices (32 bytes/quad)
    chunk.setBlock(5, 50, 5, world::BlockId::Stone);
    auto mesh1 = ChunkMesher::buildMesh(world, chunk, false);
    EXPECT_EQ(mesh1.opaqueVertices.size(), 24U);
    EXPECT_TRUE(mesh1.translucentVertices.empty());

    // 2. Two adjacent Stone blocks -> internal shared face culled -> (6 + 6 - 2) * 4 = 40 vertices
    chunk.setBlock(6, 50, 5, world::BlockId::Stone);
    auto mesh2 = ChunkMesher::buildMesh(world, chunk, false);
    EXPECT_EQ(mesh2.opaqueVertices.size(), 40U);

    // 3. Water block -> produces translucent vertices
    chunk.setBlock(10, 50, 10, world::BlockId::Water);
    auto mesh3 = ChunkMesher::buildMesh(world, chunk, false);
    EXPECT_FALSE(mesh3.translucentVertices.empty());

    // 4. Tall grass -> cross shape adds 16 vertices (4 quads * 4 vertices)
    const size_t prevOpaque = mesh2.opaqueVertices.size();
    chunk.setBlock(2, 50, 2, world::BlockId::TallGrass);
    auto mesh4 = ChunkMesher::buildMesh(world, chunk, false);
    EXPECT_EQ(mesh4.opaqueVertices.size(), prevOpaque + 16U);
}

TEST(ChunkMesherTest, CrossFloraMeshingAndUvOrientation) {
    world::World world(42, "test_flora_world");
    world::Chunk chunk(0, 0);

    // Place a Rose (38) and a Dandelion (37)
    chunk.setBlock(5, 60, 5, world::BlockId::Rose);
    chunk.setBlock(8, 60, 8, world::BlockId::Dandelion);

    const auto mesh = ChunkMesher::buildMesh(world, chunk, false);
    // 2 flora blocks * 16 vertices each = 32 vertices
    EXPECT_EQ(mesh.opaqueVertices.size(), 32U);

    for (const auto& v : mesh.opaqueVertices) {
        // Must use normalIdx = 6 (Cross flora)
        EXPECT_EQ(PackedChunkVertex::unpackNormal(v.word0), 6U);

        const uint32_t mat = PackedChunkVertex::unpackMatLayer(v.word0);
        EXPECT_TRUE(mat == 12U || mat == 13U); // Rose=12, Dandelion=13

        const uint32_t y = static_cast<uint32_t>(v.getLocalPosition().y);
        const uint32_t uvCorner = PackedChunkVertex::unpackUVCorner(v.word0);

        if (y == 60U) {
            // Ground/stem vertices must map to bottom of texture (UV corners 2 or 3)
            EXPECT_TRUE(uvCorner == 2U || uvCorner == 3U);
        } else if (y == 61U) {
            // Top/blossom vertices must map to top of texture (UV corners 0 or 1)
            EXPECT_TRUE(uvCorner == 0U || uvCorner == 1U);
        } else {
            FAIL() << "Unexpected Y coordinate for flora vertex: " << y;
        }
    }
}

TEST(ChunkMesherTest, GrassBlockSideAndTopTiles) {
    world::World world(42, "test_grass_world");
    world::Chunk chunk(0, 0);

    chunk.setBlock(5, 60, 5, world::BlockId::Grass);
    const auto mesh = ChunkMesher::buildMesh(world, chunk, false);

    // 6 faces * 4 vertices = 24 vertices
    EXPECT_EQ(mesh.opaqueVertices.size(), 24U);

    int topCount = 0;
    int bottomCount = 0;
    int sideCount = 0;

    for (const auto& v : mesh.opaqueVertices) {
        const uint32_t norm = PackedChunkVertex::unpackNormal(v.word0);
        const uint32_t mat = PackedChunkVertex::unpackMatLayer(v.word0);

        if (norm == 0U) { // +Y Top
            EXPECT_EQ(mat, 0U); // Tile 0: Grass Top
            ++topCount;
        } else if (norm == 1U) { // -Y Bottom
            EXPECT_EQ(mat, 2U); // Tile 2: Dirt
            ++bottomCount;
        } else if (norm >= 2U && norm <= 5U) { // Sides
            EXPECT_EQ(mat, 3U); // Tile 3: Grass Side
            ++sideCount;
        }
    }

    EXPECT_EQ(topCount, 4);
    EXPECT_EQ(bottomCount, 4);
    EXPECT_EQ(sideCount, 16);
}

TEST(ChunkMesherTest, LeavesBlockAllFacesFoliageTint) {
    world::World world(42, "test_leaves_world");
    world::Chunk chunk(0, 0);

    chunk.setBlock(5, 60, 5, world::BlockId::Leaves);
    const auto mesh = ChunkMesher::buildMesh(world, chunk, false);

    // 6 faces * 4 vertices = 24 vertices
    EXPECT_EQ(mesh.opaqueVertices.size(), 24U);

    for (const auto& v : mesh.opaqueVertices) {
        // Tile 52: Leaves
        EXPECT_EQ(PackedChunkVertex::unpackMatLayer(v.word0), 52U);
        // Foliage tint = 160
        EXPECT_EQ(PackedChunkVertex::unpackBiomeTint(v.word1), 160U);
        // Sway flag = true (def.tintFoliage)
        EXPECT_TRUE(PackedChunkVertex::unpackSway(v.word1));
    }
}

TEST(SkyRendererTest, AtmosphereEvaluation) {
    // Noon
    const auto noon = SkyRenderer::evaluateAtmosphere(0.25f, false);
    EXPECT_NEAR(noon.sunBrightness, 1.0f, 0.05f);
    EXPECT_GT(noon.sunDir.y, 0.9f);

    // Midnight
    const auto midnight = SkyRenderer::evaluateAtmosphere(0.75f, false);
    EXPECT_NEAR(midnight.sunBrightness, 0.16f, 0.05f);
    EXPECT_LT(midnight.sunDir.y, -0.9f);

    // Underwater
    const auto underWater = SkyRenderer::evaluateAtmosphere(0.25f, true);
    EXPECT_GT(underWater.zenithColor.b, underWater.zenithColor.r);
}

TEST(ParticleSystemTest, SpawningAndSimulation) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;
    world::World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    ParticleSystem ps;
    ps.spawnBlockBreakBurst(world, {5, 70, 5}, world::BlockId::Cobblestone, 16);
    ps.spawnMiningHitParticles(world, {5, 70, 5}, core::FaceDir::PosY, world::BlockId::Cobblestone, 4);

    std::vector<ChunkVertex> billboardVerts;
    ps.buildBillboardVertices({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, billboardVerts);
    // 20 particles * 6 vertices per billboard quad = 120 vertices
    EXPECT_EQ(billboardVerts.size(), 120U);

    // Run simulation until all particles expire (> 0.6s)
    for (int i = 0; i < 30; ++i) {
        ps.update(0.05f, world);
    }

    billboardVerts.clear();
    ps.buildBillboardVertices({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, billboardVerts);
    EXPECT_EQ(billboardVerts.size(), 0U);
}

TEST(RenderGLTest, ShaderAndRendererWithGLContext) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "OpenGL 3.3 Core context not available in this test environment.";
    }

    // Test ShaderProgram compilation
    ShaderProgram shader;
    const std::string_view vSrc = R"(#version 330 core
        layout(location = 0) in vec3 aPos;
        void main() { gl_Position = vec4(aPos, 1.0); }
    )";
    const std::string_view fSrc = R"(#version 330 core
        out vec4 FragColor;
        uniform vec4 uColor;
        void main() { FragColor = uColor; }
    )";

    EXPECT_TRUE(shader.compileFromSource(vSrc, fSrc));
    shader.bind();
    shader.setVec4("uColor", {1.0f, 0.0f, 0.0f, 1.0f});
    ShaderProgram::unbind();

    // Test WorldRenderer initialization
    WorldRenderer wr;
    EXPECT_TRUE(wr.initialize());
}
