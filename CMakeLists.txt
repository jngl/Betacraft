cmake_minimum_required(VERSION 3.20)
project(BetaCraft173 VERSION 1.7.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(PNG QUIET)

pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
# S7.1 region files compress chunk bundles with zstd level 3 when available; without it they
# are stored uncompressed (compression byte 0), which every build can read.
option(BETACRAFT_WITH_ZSTD "Compress region file chunks with zstd" ON)
if(BETACRAFT_WITH_ZSTD)
    pkg_check_modules(ZSTD IMPORTED_TARGET libzstd)
endif()

# Separate third-party miniaudio compilation unit so our own code can use strict warnings
add_library(miniaudio_lib STATIC
    src/audio/miniaudio_impl.cpp
)
target_include_directories(miniaudio_lib SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(miniaudio_lib PRIVATE -w -O2)
endif()
target_link_libraries(miniaudio_lib PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)

set(BETACRAFT_CORE_SOURCES
    src/core/Logger.cpp
    src/math/PerlinNoise.cpp
    src/world/Block.cpp
    src/world/PalettedSection.cpp
    src/world/Chunk.cpp
    src/world/DifferentialLightingEngine.cpp
    src/world/WorldGen.cpp
    src/world/WorldStorage.cpp
    src/world/World.cpp
    src/world/BlockEntityStore.cpp
    src/world/BlockTicks.cpp
    src/world/ScheduledTicks.cpp
    src/world/Weather.cpp
    src/physics/Raycaster.cpp
    src/physics/Player.cpp
    src/render/GLHeaders.cpp
    src/render/Shader.cpp
    src/render/TextureAtlas.cpp
    src/render/PackedChunkVertex.cpp
    src/render/ChunkMesher.cpp
    src/render/SkyRenderer.cpp
    src/render/WeatherRenderer.cpp
    src/render/ParticleSystem.cpp
    src/render/WorldRenderer.cpp
    src/render/MobModels.cpp
    src/render/TextRenderer.cpp
    src/render/ItemAppearance.cpp
    src/audio/AudioEngine.cpp
    src/ui/BitmapFont.cpp
    src/ui/CraftingSystem.cpp
    src/ui/HudRenderer.cpp
    src/ui/InputManager.cpp
    src/ui/TitleScreen.cpp
    src/ui/DeathScreen.cpp
    src/ui/OptionsMenu.cpp
    src/ui/TextField.cpp
    src/ui/WorldSelectScreen.cpp
    src/ui/CreateWorldScreen.cpp
    src/game/Application.cpp
    src/game/DynamicLightCollector.cpp
    src/game/AcousticsController.cpp
    src/game/InventoryCrafting.cpp
    src/game/GameSession.cpp
    src/game/StarterKit.cpp
    src/game/PlayerPersistence.cpp
    src/game/MobSpawner.cpp
    src/game/MobBehaviour.cpp
    src/game/FallingBlocks.cpp
    src/game/ItemUse.cpp
    src/game/Projectiles.cpp
)

# Phase 2 modules (docs/IMPROVEMENTS_ROADMAP.md §9.2) are part of the engine core so that
# World / Application / renderers / audio can use them.
set(BETACRAFT_PHASE2_DIRS streaming ecs gameplay crafting fluids worldgen lighting acoustics storage ai redstone)
foreach(dir IN LISTS BETACRAFT_PHASE2_DIRS)
    file(GLOB p2_core_src CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${dir}/*.cpp)
    list(APPEND BETACRAFT_CORE_SOURCES ${p2_core_src})
endforeach()

add_library(betacraft_core STATIC ${BETACRAFT_CORE_SOURCES})

target_include_directories(betacraft_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(betacraft_core PUBLIC
        -Wall
        -Wextra
        -Wpedantic
        -Wno-missing-field-initializers
    )
endif()

target_link_libraries(betacraft_core PUBLIC
    PkgConfig::SDL2
    OpenGL::GL
    miniaudio_lib
    Threads::Threads
)

if(BETACRAFT_WITH_ZSTD AND ZSTD_FOUND)
    target_compile_definitions(betacraft_core PUBLIC BETACRAFT_HAS_ZSTD=1)
    target_link_libraries(betacraft_core PUBLIC PkgConfig::ZSTD)
endif()

if(PNG_FOUND)
    target_compile_definitions(betacraft_core PUBLIC BETACRAFT_HAS_LIBPNG=1)
    target_link_libraries(betacraft_core PUBLIC PNG::PNG)
endif()

add_executable(betacraft src/main.cpp)
target_link_libraries(betacraft PRIVATE betacraft_core)

enable_testing()

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(UNIT_TEST_SOURCES
    tests/unit/test_core.cpp
    tests/unit/test_math.cpp
    tests/unit/test_physics.cpp
    tests/unit/test_world_block.cpp
    tests/unit/test_world_chunk.cpp
    tests/unit/test_world_gen.cpp
    tests/unit/test_world_storage.cpp
    tests/unit/test_world.cpp
    tests/unit/test_audio.cpp
    tests/unit/test_ui.cpp
    tests/unit/test_render.cpp
    tests/unit/test_mob_models.cpp
    tests/unit/test_physics_fixed_step.cpp
    tests/unit/test_paletted_section.cpp
    tests/unit/test_packed_chunk_vertex.cpp
    tests/unit/test_shared_ebo.cpp
    tests/unit/test_differential_lighting.cpp
    tests/unit/test_adversarial_m2.cpp
    tests/unit/test_mpsc_ring_buffer.cpp
    tests/unit/test_msdf_font.cpp
    tests/unit/test_input_rebind.cpp
    tests/unit/test_adversarial_m3.cpp
    tests/unit/test_adversarial_tier5.cpp
    tests/unit/test_weather.cpp
)

set(INTEGRATION_TEST_SOURCES
    tests/integration/test_integration_player_world.cpp
    tests/integration/test_integration_mining_crafting.cpp
    tests/integration/test_integration_piston.cpp
    tests/integration/test_integration_persistence.cpp
    tests/integration/test_integration_lighting_mesher.cpp
    tests/integration/test_integration_phase1.cpp
)

file(GLOB PHASE2_INTEGRATION_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/phase2/*.cpp)
list(APPEND INTEGRATION_TEST_SOURCES ${PHASE2_INTEGRATION_TEST_SOURCES})

set(E2E_TEST_SOURCES
    tests/e2e/test_e2e_application.cpp
    tests/e2e/test_e2e_cli.cpp
    tests/e2e/test_e2e_phase1_scenarios.cpp
)

set(MOCK_TEST_SOURCES
    tests/mock/test_mock_observers.cpp
)

# 1. Unit Tests target
add_executable(betacraft_unit_tests ${UNIT_TEST_SOURCES})
target_include_directories(betacraft_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(betacraft_unit_tests PRIVATE betacraft_core GTest::gtest GTest::gtest_main GTest::gmock)
add_test(NAME UnitTests COMMAND betacraft_unit_tests)

# 2. Integration Tests target
add_executable(betacraft_integration_tests ${INTEGRATION_TEST_SOURCES})
target_include_directories(betacraft_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(betacraft_integration_tests PRIVATE betacraft_core GTest::gtest GTest::gtest_main GTest::gmock)
add_test(NAME IntegrationTests COMMAND betacraft_integration_tests)

# 3. E2E Tests target
add_executable(betacraft_e2e_tests ${E2E_TEST_SOURCES})
target_include_directories(betacraft_e2e_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(betacraft_e2e_tests PRIVATE BETACRAFT_BIN_PATH="$<TARGET_FILE:betacraft>")
target_link_libraries(betacraft_e2e_tests PRIVATE betacraft_core GTest::gtest GTest::gtest_main GTest::gmock)
add_dependencies(betacraft_e2e_tests betacraft)
add_test(NAME E2ETests COMMAND betacraft_e2e_tests)

# Frozen reference saves (tests/data/saves/vN, roadmap conventions §1.8).
foreach(t betacraft_integration_tests betacraft_e2e_tests)
    target_compile_definitions(${t} PRIVATE BETACRAFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endforeach()

# 4. GMock Tests target
add_executable(betacraft_mock_tests ${MOCK_TEST_SOURCES})
target_include_directories(betacraft_mock_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(betacraft_mock_tests PRIVATE betacraft_core GTest::gtest GTest::gtest_main GTest::gmock)
add_test(NAME MockTests COMMAND betacraft_mock_tests)

# 5. All-in-one test suite target
add_executable(betacraft_all_tests
    ${UNIT_TEST_SOURCES}
    ${INTEGRATION_TEST_SOURCES}
    ${E2E_TEST_SOURCES}
    ${MOCK_TEST_SOURCES}
)
target_include_directories(betacraft_all_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(betacraft_all_tests PRIVATE BETACRAFT_BIN_PATH="$<TARGET_FILE:betacraft>"
                           BETACRAFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_link_libraries(betacraft_all_tests PRIVATE betacraft_core GTest::gtest GTest::gtest_main GTest::gmock)
add_dependencies(betacraft_all_tests betacraft)
add_test(NAME AllTests COMMAND betacraft_all_tests)

# Legacy tests target
add_executable(betacraft_legacy_tests tests/test_main.cpp)
target_link_libraries(betacraft_legacy_tests PRIVATE betacraft_core)
add_test(NAME BetaCraftLegacyTests COMMAND betacraft_legacy_tests)

# Adversarial M2 stress harness
add_executable(adversarial_m2_stress tests/adversarial_m2_stress.cpp)
target_include_directories(adversarial_m2_stress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(adversarial_m2_stress PRIVATE betacraft_core GTest::gtest GTest::gtest_main GTest::gmock)

# Adversarial M3 stress harness
add_executable(adversarial_m3_stress tests/adversarial_m3_stress.cpp)
target_include_directories(adversarial_m3_stress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(adversarial_m3_stress PRIVATE betacraft_core GTest::gtest GTest::gtest_main GTest::gmock)

# Adversarial M3 UI & Options stress harness
add_executable(adversarial_m3_ui_stress tests/adversarial_m3_ui_stress.cpp)
target_include_directories(adversarial_m3_ui_stress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(adversarial_m3_ui_stress PRIVATE betacraft_core GTest::gtest GTest::gtest_main GTest::gmock)

# ---------------------------------------------------------------------------
# Phase 2 modules (docs/IMPROVEMENTS_ROADMAP.md §9.2). Each module is an isolated
# static library + GoogleTest executable so that it can be developed test-first.
# ---------------------------------------------------------------------------
set(BETACRAFT_PHASE2_MODULES
    "streaming:streaming"   # P1.1 AsyncChunkPipeline, S7.7 ChunkGenPipeline
    "ecs:ecs"               # P2.1 Sparse-set ECS
    "combat:gameplay"       # P2.5 Tools, durability, melee combat
    "crafting:crafting"     # P2.6 2x2 / 3x3 crafting grids
    "fluids:fluids"         # P2.3 Cellular-automaton fluids
    "worldgen:worldgen"     # P3.3 Simplex caves + P3.4 two-pass decoration
    "dynlight:lighting"     # P4.6 Held-torch dynamic lighting (UBO)
    "acoustics:acoustics"   # P4.2 DDA acoustic occlusion + shared FDN reverb bus
    "storage:storage"       # S7.1-S7.6 Region files, chunk bundles, legacy save migration
    "ai:ai"                 # S8.1-S8.4 Voxel A* pathfinding and ECS path following
    "redstone:redstone"     # S8.5-S8.8 Redstone wire, torches, levers, buttons, plates
)
set(BETACRAFT_PHASE2_LIBS)
foreach(entry IN LISTS BETACRAFT_PHASE2_MODULES)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 key)
    list(GET parts 1 dir)
    # Sources are compiled into betacraft_core; this target only names the module for its tests.
    add_library(betacraft_p2_${key} INTERFACE)
    target_link_libraries(betacraft_p2_${key} INTERFACE betacraft_core)
    list(APPEND BETACRAFT_PHASE2_LIBS betacraft_p2_${key})

    file(GLOB p2_tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/phase2/${key}/*.cpp)
    if(p2_tests)
        add_executable(betacraft_p2_${key}_tests ${p2_tests})
        target_include_directories(betacraft_p2_${key}_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(betacraft_p2_${key}_tests PRIVATE betacraft_p2_${key} GTest::gtest GTest::gtest_main GTest::gmock)
        target_compile_definitions(betacraft_p2_${key}_tests PRIVATE BETACRAFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
        add_test(NAME Phase2_${key} COMMAND betacraft_p2_${key}_tests)
    endif()
endforeach()

# Wall-clock budgets are benchmarks: skipped unless BETACRAFT_PERF_TESTS is set, run with
# `ctest -L benchmark` (CI runs `ctest -LE benchmark`).
if(TARGET betacraft_p2_acoustics_tests)
    add_test(NAME Phase2_acoustics_perf COMMAND betacraft_p2_acoustics_tests --gtest_filter=AcousticPipeline.*Budget*)
    set_tests_properties(Phase2_acoustics_perf PROPERTIES LABELS benchmark ENVIRONMENT BETACRAFT_PERF_TESTS=1)
endif()
# S6.6: one TNT blast under 15 ms in Release.
add_test(NAME Integration_beta_content_perf COMMAND betacraft_integration_tests --gtest_filter=S6BetaContent.*Budget*)
set_tests_properties(Integration_beta_content_perf PROPERTIES LABELS benchmark ENVIRONMENT BETACRAFT_PERF_TESTS=1)
