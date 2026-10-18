#include "audio/AudioEngine.hpp"
#include "core/Types.hpp"
#include "game/StarterKit.hpp"
#include "math/PerlinNoise.hpp"
#include "physics/AABB.hpp"
#include "physics/Player.hpp"
#include "physics/Raycaster.hpp"
#include "render/ChunkMesher.hpp"
#include "render/TextureAtlas.hpp"
#include "ui/CraftingSystem.hpp"
#include "world/Block.hpp"
#include "world/Chunk.hpp"
#include "world/World.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

int g_failedTests = 0;

void check(bool condition, const char* expr, int line) {
    if (!condition) {
        std::cerr << "[FAIL] Line " << line << ": " << expr << '\n';
        ++g_failedTests;
    }
}

#define BC_ASSERT(cond) check((cond), #cond, __LINE__)

void testFloorDivMod() {
    using namespace betacraft::core;
    BC_ASSERT(floorDiv(0, 16) == 0);
    BC_ASSERT(floorDiv(15, 16) == 0);
    BC_ASSERT(floorDiv(16, 16) == 1);
    BC_ASSERT(floorDiv(-1, 16) == -1);
    BC_ASSERT(floorDiv(-16, 16) == -1);
    BC_ASSERT(floorDiv(-17, 16) == -2);

    BC_ASSERT(floorMod(-1, 16) == 15);
    BC_ASSERT(floorMod(-16, 16) == 0);
    BC_ASSERT(floorMod(-17, 16) == 15);
}

void testPerlinDeterminism() {
    betacraft::math::PerlinNoise n1(173173ULL);
    betacraft::math::PerlinNoise n2(173173ULL);

    for (int i = -20; i <= 20; ++i) {
        const double v1 = n1.fbm2D(i * 0.13, i * 0.27, 4);
        const double v2 = n2.fbm2D(i * 0.13, i * 0.27, 4);
        BC_ASSERT(std::abs(v1 - v2) < 1e-12);
        BC_ASSERT(v1 >= -1.0 && v1 <= 1.0);
    }
}

void testBlockRegistry() {
    using namespace betacraft::world;
    BlockRegistry::initialize();

    BC_ASSERT(!BlockRegistry::isSolid(BlockId::Air));
    BC_ASSERT(BlockRegistry::isSolid(BlockId::Stone));
    BC_ASSERT(BlockRegistry::isSolid(BlockId::Grass));
    BC_ASSERT(!BlockRegistry::isSolid(BlockId::Water));
    BC_ASSERT(!BlockRegistry::isSolid(BlockId::TallGrass));
    BC_ASSERT(BlockRegistry::get(BlockId::Torch).lightEmission == 14);
    BC_ASSERT(BlockRegistry::get(BlockId::Glowstone).lightEmission == 15);
    BC_ASSERT(BlockRegistry::getPickaxeSpeedMultiplier(BlockId::DiamondPickaxe, BlockId::Stone) > 5.0f);
}

void testChunkSerializationAndLighting() {
    using namespace betacraft::world;
    Chunk c1({2, -3});
    c1.setBlock(4, 10, 4, BlockId::Stone);
    c1.setBlock(5, 10, 4, BlockId::Torch);
    c1.setBlock(8, 64, 8, BlockId::Grass);
    c1.recalculateLighting();

    // Torch at (5,10,4) emits 14 block light, neighbor (6,10,4) gets 13
    BC_ASSERT(c1.getBlockLight(5, 10, 4) == 14);
    BC_ASSERT(c1.getBlockLight(6, 10, 4) == 13);
    BC_ASSERT(c1.getSkyLight(8, 120, 8) == 15);
    BC_ASSERT(c1.getSkyLight(8, 63, 8) < 15); // Shaded below opaque Grass at Y=64

    const auto bytes = c1.serializeRLE();
    BC_ASSERT(!bytes.empty());

    Chunk c2({2, -3});
    BC_ASSERT(c2.deserializeRLE(bytes));
    BC_ASSERT(c2.getBlock(4, 10, 4) == BlockId::Stone);
    BC_ASSERT(c2.getBlock(5, 10, 4) == BlockId::Torch);
    BC_ASSERT(c2.getBlock(8, 64, 8) == BlockId::Grass);
}

void testWorldRaycastingAndPiston() {
    using namespace betacraft;
    const auto tempSave = std::filesystem::temp_directory_path() / "betacraft_unit_test_save";
    std::filesystem::remove_all(tempSave);

    world::World w(173173ULL, tempSave);
    w.ensureChunkLoaded({0, 0});

    // Clear test chamber in sky at Y=105
    for (int z = 2; z <= 12; ++z) {
        for (int x = 2; x <= 12; ++x) {
            for (int y = 102; y <= 110; ++y) {
                w.setBlock(x, y, z, world::BlockId::Air);
            }
        }
    }

    w.setBlock(5, 105, 5, world::BlockId::Cobblestone);
    const auto hit = physics::Raycaster::cast(w, {5.5f, 105.5f, 2.5f}, {0.0f, 0.0f, 1.0f}, 5.0f);
    BC_ASSERT(hit.hit);
    BC_ASSERT(hit.blockPos == (core::BlockPos{5, 105, 5}));
    BC_ASSERT(hit.adjacentPos == (core::BlockPos{5, 105, 4}));
    BC_ASSERT(hit.face == core::FaceDir::NegZ);

    // Test Beta 1.7 Piston pushing Cobblestone along +Z
    w.setBlock(5, 105, 4, world::BlockId::Piston);
    BC_ASSERT(w.triggerPistonPush({5, 105, 4}, core::FaceDir::PosZ));
    BC_ASSERT(w.getBlock(5, 105, 5) == world::BlockId::Air);
    BC_ASSERT(w.getBlock(5, 105, 6) == world::BlockId::Cobblestone);

    // Test ChunkMesher builds non-empty Smooth Lighting mesh
    const auto* chunkPtr = w.getChunk({0, 0});
    BC_ASSERT(chunkPtr != nullptr);
    const auto mesh = render::ChunkMesher::buildMesh(w, *chunkPtr, true);
    BC_ASSERT(!mesh.opaqueVertices.empty());

    std::filesystem::remove_all(tempSave);
}

void testCraftingAndTextureAtlas() {
    using namespace betacraft;
    physics::Player player;
    // A new Player is empty-handed (Survival); the Creative kit holds the logs crafted here.
    game::applyStarterKit(player, core::GameMode::Creative);
    const int32_t initialPlanks = player.countItem(world::BlockId::OakPlanks);
    const auto recipes = ui::CraftingSystem::getRecipes();
    BC_ASSERT(!recipes.empty());

    // First recipe crafts 4 Oak Planks from 1 Oak Log (player starts with 32 OakLog in slot 9)
    BC_ASSERT(ui::CraftingSystem::canCraft(recipes[0], player));
    BC_ASSERT(ui::CraftingSystem::craft(recipes[0], player));
    BC_ASSERT(player.countItem(world::BlockId::OakPlanks) == initialPlanks + 4);

    // Procedural 256x256 RGBA atlas generation
    const auto rgba = render::TextureAtlas::generateProceduralAtlasRGBA();
    BC_ASSERT(rgba.size() == 256 * 256 * 4);
}

void testWaterExitJump() {
    using namespace betacraft;
    const auto tempSave = std::filesystem::temp_directory_path() / "betacraft_water_jump_test";
    std::filesystem::remove_all(tempSave);

    world::World w(173173ULL, tempSave);
    w.ensureChunkLoaded({0, 0});

    // Build a clean water pool at x=5,z=5 (y=63..64) and a 1-block-above-water shore at x=6,z=5 (y=65, top=66.0)
    for (int z = 3; z <= 8; ++z) {
        for (int x = 3; x <= 8; ++x) {
            for (int y = 60; y <= 75; ++y) {
                w.setBlock(x, y, z, world::BlockId::Air);
            }
            w.setBlock(x, 62, z, world::BlockId::Stone);
        }
    }
    w.setBlock(5, 63, 5, world::BlockId::Water);
    w.setBlock(5, 64, 5, world::BlockId::Water);
    for (int sx = 6; sx <= 12; ++sx) {
        w.setBlock(sx, 63, 5, world::BlockId::Stone);
        w.setBlock(sx, 64, 5, world::BlockId::Stone);
        w.setBlock(sx, 65, 5, world::BlockId::Grass); // Top at y = 66.0f (1 full block above water level)
    }

    physics::Player player;
    player.setPosition({5.5f, 63.2f, 5.5f});
    player.setRotation(0.0f, 0.0f); // Facing +X toward shore at x=6

    physics::PlayerInput input{};
    input.forward = true;
    input.jump = true;

    float maxReachedY = player.getPosition().y;
    for (int step = 0; step < 90; ++step) {
        (void)player.update(1.0f / 60.0f, input, w);
        maxReachedY = std::max(maxReachedY, player.getPosition().y);
    }

    // Player must have jumped above y=66.0 and climbed onto the shore block at x >= 6.0
    BC_ASSERT(maxReachedY > 66.0f);
    BC_ASSERT(player.getPosition().x > 6.0f);
    BC_ASSERT(player.getPosition().y >= 66.0f);

    std::filesystem::remove_all(tempSave);
}

} // namespace

int main() {
    testFloorDivMod();
    testPerlinDeterminism();
    testBlockRegistry();
    testChunkSerializationAndLighting();
    testWorldRaycastingAndPiston();
    testCraftingAndTextureAtlas();
    testWaterExitJump();

    if (g_failedTests == 0) {
        std::cout << "[PASS] All BetaCraft 1.7.3 unit tests passed successfully.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "[FAIL] " << g_failedTests << " test assertion(s) failed.\n";
    return EXIT_FAILURE;
}
