#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "core/Types.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <sstream>
#include <unordered_set>

using namespace betacraft::core;
using namespace testing;

TEST(CoreTypesTest, FloorDivBasic) {
    EXPECT_EQ(floorDiv(0, 16), 0);
    EXPECT_EQ(floorDiv(15, 16), 0);
    EXPECT_EQ(floorDiv(16, 16), 1);
    EXPECT_EQ(floorDiv(31, 16), 1);
    EXPECT_EQ(floorDiv(32, 16), 2);

    EXPECT_EQ(floorDiv(-1, 16), -1);
    EXPECT_EQ(floorDiv(-16, 16), -1);
    EXPECT_EQ(floorDiv(-17, 16), -2);
    EXPECT_EQ(floorDiv(-32, 16), -2);
    EXPECT_EQ(floorDiv(-33, 16), -3);
}

TEST(CoreTypesTest, FloorModBasic) {
    EXPECT_EQ(floorMod(0, 16), 0);
    EXPECT_EQ(floorMod(1, 16), 1);
    EXPECT_EQ(floorMod(15, 16), 15);
    EXPECT_EQ(floorMod(16, 16), 0);
    EXPECT_EQ(floorMod(17, 16), 1);

    EXPECT_EQ(floorMod(-1, 16), 15);
    EXPECT_EQ(floorMod(-15, 16), 1);
    EXPECT_EQ(floorMod(-16, 16), 0);
    EXPECT_EQ(floorMod(-17, 16), 15);
}

TEST(CoreTypesTest, FloorDivModIdentity) {
    const int32_t b = 16;
    for (int32_t a = -100; a <= 100; ++a) {
        const int32_t q = floorDiv(a, b);
        const int32_t r = floorMod(a, b);
        EXPECT_EQ(q * b + r, a) << "Identity failed for a=" << a;
        EXPECT_GE(r, 0) << "Remainder must be non-negative for a=" << a;
        EXPECT_LT(r, b) << "Remainder must be strictly less than divisor for a=" << a;
    }
}

TEST(CoreTypesTest, ChunkCoordEqualityAndHash) {
    const ChunkCoord c1{5, -7};
    const ChunkCoord c2{5, -7};
    const ChunkCoord c3{-5, 7};

    EXPECT_EQ(c1, c2);
    EXPECT_FALSE(c1 == c3);

    std::hash<ChunkCoord> hasher;
    EXPECT_EQ(hasher(c1), hasher(c2));

    std::unordered_set<ChunkCoord> set;
    set.insert(c1);
    EXPECT_TRUE(set.contains(c2));
    EXPECT_FALSE(set.contains(c3));
    set.insert(c3);
    EXPECT_EQ(set.size(), 2U);
}

TEST(CoreTypesTest, BlockPosOffsetAndHash) {
    const BlockPos p1{10, 64, -20};
    const BlockPos p2{10, 64, -20};
    EXPECT_EQ(p1, p2);

    const BlockPos p3 = p1.offset(1, -2, 3);
    EXPECT_EQ(p3.x, 11);
    EXPECT_EQ(p3.y, 62);
    EXPECT_EQ(p3.z, -17);

    std::hash<BlockPos> hasher;
    EXPECT_EQ(hasher(p1), hasher(p2));

    std::unordered_set<BlockPos> posSet;
    posSet.insert(p1);
    posSet.insert(p3);
    EXPECT_EQ(posSet.size(), 2U);
}

TEST(CoreTypesTest, FaceNormals) {
    EXPECT_EQ(faceNormal(FaceDir::PosX), glm::ivec3(1, 0, 0));
    EXPECT_EQ(faceNormal(FaceDir::NegX), glm::ivec3(-1, 0, 0));
    EXPECT_EQ(faceNormal(FaceDir::PosY), glm::ivec3(0, 1, 0));
    EXPECT_EQ(faceNormal(FaceDir::NegY), glm::ivec3(0, -1, 0));
    EXPECT_EQ(faceNormal(FaceDir::PosZ), glm::ivec3(0, 0, 1));
    EXPECT_EQ(faceNormal(FaceDir::NegZ), glm::ivec3(0, 0, -1));

    // Opposite normals cancel
    EXPECT_EQ(faceNormal(FaceDir::PosX) + faceNormal(FaceDir::NegX), glm::ivec3(0));
    EXPECT_EQ(faceNormal(FaceDir::PosY) + faceNormal(FaceDir::NegY), glm::ivec3(0));
    EXPECT_EQ(faceNormal(FaceDir::PosZ) + faceNormal(FaceDir::NegZ), glm::ivec3(0));
}

TEST(CoreConfigTest, ConstantsAndDefaults) {
    EXPECT_EQ(EngineConfig::CHUNK_WIDTH, 16);
    EXPECT_EQ(EngineConfig::CHUNK_HEIGHT, 128);
    EXPECT_EQ(EngineConfig::CHUNK_DEPTH, 16);
    EXPECT_EQ(EngineConfig::CHUNK_VOLUME, 16 * 128 * 16);
    EXPECT_EQ(EngineConfig::SEA_LEVEL, 64);

    EXPECT_GT(EngineConfig::GRAVITY, 0.0f);
    EXPECT_GT(EngineConfig::JUMP_VELOCITY, 0.0f);
    EXPECT_GT(EngineConfig::WALK_SPEED, 0.0f);
    EXPECT_GT(EngineConfig::REACH_DISTANCE, 4.0f);

    EngineConfig cfg;
    EXPECT_EQ(cfg.windowWidth, 1280);
    EXPECT_EQ(cfg.windowHeight, 720);
    EXPECT_TRUE(cfg.vsync);
    EXPECT_FALSE(cfg.fullscreen);
    EXPECT_GT(cfg.renderDistanceChunks, 0);
    EXPECT_FLOAT_EQ(cfg.fovDegrees, 70.0f);
    EXPECT_EQ(cfg.worldSeed, 173173173ULL);
    // Beta's 24000-tick day, 20 minutes at 20 ticks per second.
    EXPECT_FLOAT_EQ(cfg.dayLengthSeconds, 1200.0f);
    EXPECT_FLOAT_EQ(cfg.dayLengthSeconds * static_cast<float>(EngineConfig::GAME_TICKS_PER_SECOND), 24000.0f);
}

TEST(CoreLoggerTest, MinLevelFiltering) {
    const LogLevel prev = Logger::getMinLevel();

    Logger::setMinLevel(LogLevel::Warn);
    EXPECT_EQ(Logger::getMinLevel(), LogLevel::Warn);

    // Redirect cout and cerr buffers
    std::stringstream bufferOut;
    std::stringstream bufferErr;
    std::streambuf* oldCout = std::cout.rdbuf(bufferOut.rdbuf());
    std::streambuf* oldCerr = std::cerr.rdbuf(bufferErr.rdbuf());

    Logger::debug("TestCore", "This debug should be suppressed");
    Logger::info("TestCore", "This info should be suppressed");
    EXPECT_TRUE(bufferOut.str().empty());
    EXPECT_TRUE(bufferErr.str().empty());

    Logger::warn("TestCore", "A warning message");
    EXPECT_THAT(bufferOut.str(), HasSubstr("[WARN ]"));
    EXPECT_THAT(bufferOut.str(), HasSubstr("A warning message"));

    Logger::error("TestCore", "An error message");
    EXPECT_THAT(bufferErr.str(), HasSubstr("[ERROR]"));
    EXPECT_THAT(bufferErr.str(), HasSubstr("An error message"));

    // Reset streams and restore level
    std::cout.rdbuf(oldCout);
    std::cerr.rdbuf(oldCerr);
    Logger::setMinLevel(prev);
}

// Conventions §3: every feature flag defaults on and has a --no-<flag> switch.
TEST(CoreConfigTest, FeatureFlagsDefaultOnAndSwitchOff) {
    betacraft::core::FeatureFlags flags{};
    EXPECT_TRUE(flags.asyncWorldGen && flags.regionStorage && flags.mobPathfinding && flags.redstone && flags.weather);
    EXPECT_TRUE(betacraft::core::parseFeatureFlagArg("--no-region-storage", flags));
    EXPECT_FALSE(flags.regionStorage);
    EXPECT_TRUE(flags.asyncWorldGen);
    EXPECT_TRUE(betacraft::core::parseFeatureFlagArg("--no-async-gen", flags));
    EXPECT_FALSE(flags.asyncWorldGen);
    EXPECT_FALSE(betacraft::core::parseFeatureFlagArg("--seed", flags));
    EXPECT_TRUE(betacraft::core::EngineConfig{}.features.regionStorage);
}
