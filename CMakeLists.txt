cmake_minimum_required(VERSION 3.20)
project(Betacraft VERSION 0.1.0 LANGUAGES CXX)

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

pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
# Region files compress chunk bundles with zstd level 3 when available; without it they
# are stored uncompressed (compression byte 0), which every build can read.
option(BETACRAFT_WITH_ZSTD "Compress region file chunks with zstd" ON)
if(BETACRAFT_WITH_ZSTD)
    pkg_check_modules(ZSTD IMPORTED_TARGET libzstd)
endif()
# TextureAtlas exports assets/terrain.png and loads a custom one with libpng when available;
# without it the atlas is still generated procedurally, just never exported nor overridden.
option(BETACRAFT_WITH_PNG "Export and load assets/terrain.png with libpng" ON)
if(BETACRAFT_WITH_PNG)
    find_package(PNG QUIET)
endif()

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

# Engine modules (modules/<m>, ADR 0001): each one is its own static library and test executable.
include(cmake/BetacraftModule.cmake)
add_subdirectory(modules/core)
add_subdirectory(modules/math)
add_subdirectory(modules/voxel)
add_subdirectory(modules/gameplay)
add_subdirectory(modules/fluids)
add_subdirectory(modules/redstone)
add_subdirectory(modules/worldgen)
add_subdirectory(modules/storage)
add_subdirectory(modules/lighting)
add_subdirectory(modules/world)
add_subdirectory(modules/physics)
add_subdirectory(modules/crafting)
add_subdirectory(modules/ai)
add_subdirectory(modules/ecs)
add_subdirectory(modules/streaming)
add_subdirectory(modules/acoustics)
add_subdirectory(modules/audio)
add_subdirectory(modules/render)
add_subdirectory(modules/ui)
add_subdirectory(modules/game)

# The game executable: app/main.cpp on top of the game module.
add_executable(betacraft app/main.cpp)
target_link_libraries(betacraft PRIVATE betacraft::game betacraft_warnings)

file(GLOB_RECURSE INTEGRATION_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/*.cpp)

set(E2E_TEST_SOURCES
    tests/e2e/test_e2e_application.cpp
    tests/e2e/test_e2e_cli.cpp
    tests/e2e/test_e2e_spec_scenarios.cpp
)

set(MOCK_TEST_SOURCES
    tests/mock/test_mock_observers.cpp
)

# 1. Integration Tests target
add_executable(betacraft_integration_tests ${INTEGRATION_TEST_SOURCES})
target_include_directories(betacraft_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(betacraft_integration_tests PRIVATE betacraft::game betacraft_warnings betacraft_test_support GTest::gtest GTest::gtest_main GTest::gmock)
# One ctest entry per test (Integration.<Suite>.<Test>, label `integration`) so `ctest -j` spreads the
# suite over every core; as a single entry it ran alone on one core long after the others.
include(GoogleTest)
gtest_discover_tests(betacraft_integration_tests
    TEST_PREFIX "Integration."
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS integration
)

# 2. E2E Tests target
add_executable(betacraft_e2e_tests ${E2E_TEST_SOURCES})
target_include_directories(betacraft_e2e_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(betacraft_e2e_tests PRIVATE BETACRAFT_BIN_PATH="$<TARGET_FILE:betacraft>")
target_link_libraries(betacraft_e2e_tests PRIVATE betacraft::game betacraft_warnings betacraft_test_support GTest::gtest GTest::gtest_main GTest::gmock)
add_dependencies(betacraft_e2e_tests betacraft)
add_test(NAME E2ETests COMMAND betacraft_e2e_tests)

# Frozen reference saves (tests/data/saves/vN, CLAUDE.md conventions).
foreach(t betacraft_integration_tests betacraft_e2e_tests)
    target_compile_definitions(${t} PRIVATE BETACRAFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endforeach()

# 3. GMock Tests target
add_executable(betacraft_mock_tests ${MOCK_TEST_SOURCES})
target_include_directories(betacraft_mock_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(betacraft_mock_tests PRIVATE betacraft::game betacraft_warnings betacraft_test_support GTest::gtest GTest::gtest_main GTest::gmock)
add_test(NAME MockTests COMMAND betacraft_mock_tests)

# 4. All-in-one test suite target
get_property(MODULE_TEST_SOURCES GLOBAL PROPERTY BETACRAFT_MODULE_TEST_SOURCES)
add_executable(betacraft_all_tests
    ${MODULE_TEST_SOURCES}
    ${INTEGRATION_TEST_SOURCES}
    ${E2E_TEST_SOURCES}
    ${MOCK_TEST_SOURCES}
)
target_include_directories(betacraft_all_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(betacraft_all_tests PRIVATE BETACRAFT_BIN_PATH="$<TARGET_FILE:betacraft>"
                           BETACRAFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_link_libraries(betacraft_all_tests PRIVATE betacraft::game betacraft_warnings betacraft_test_support GTest::gtest GTest::gtest_main GTest::gmock)
add_dependencies(betacraft_all_tests betacraft)
add_test(NAME AllTests COMMAND betacraft_all_tests)

# Legacy tests target
add_executable(betacraft_legacy_tests tests/test_main.cpp)
target_link_libraries(betacraft_legacy_tests PRIVATE betacraft::game betacraft_warnings)
add_test(NAME BetaCraftLegacyTests COMMAND betacraft_legacy_tests)

# Wall-clock budgets are benchmarks: skipped unless BETACRAFT_PERF_TESTS is set, run with
# `ctest -L benchmark` (CI runs `ctest -LE benchmark`). Module budgets are registered by their
# module (Module_acoustics_perf, Module_streaming_perf).

# One TNT blast under 15 ms in Release.
add_test(NAME Integration_beta_content_perf COMMAND betacraft_integration_tests --gtest_filter=BetaContent.*Budget*)
set_tests_properties(Integration_beta_content_perf PROPERTIES LABELS benchmark ENVIRONMENT BETACRAFT_PERF_TESTS=1)
