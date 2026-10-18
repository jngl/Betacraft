#include "tests/test_helpers.hpp"

#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string>

using namespace testing;

#ifndef BETACRAFT_BIN_PATH
#define BETACRAFT_BIN_PATH "./betacraft"
#endif

TEST(CliE2ETest, HelpArgument) {
    const std::string cmd = std::string(BETACRAFT_BIN_PATH) + " --help > /dev/null 2>&1";
    const int status = std::system(cmd.c_str());
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(CliE2ETest, RunHeadlessFrames) {
    if (!betacraft::test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "Skipping CLI execution test: OpenGL 3.3 Core display/context not available.";
    }

    betacraft::test::TempDir tempDir;
    const std::string cmd = std::string(BETACRAFT_BIN_PATH) +
                            " --frames 3 --seed 54321 --render-distance 2 --save-dir \"" +
                            tempDir.path().string() + "\" > /dev/null 2>&1";

    const int status = std::system(cmd.c_str());
    EXPECT_EQ(WEXITSTATUS(status), 0);

    const auto levelDat = tempDir.path() / "level.dat";
    EXPECT_TRUE(std::filesystem::exists(levelDat));
}

// S7.4: launching on a pre-Sprint 7 save (frozen copy of tests/data/saves/v1) migrates its
// chunks/ files to region/ and keeps them as chunks.migrated/.
TEST(CliE2ETest, LegacySaveMigratedToRegions) {
    if (!betacraft::test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "Skipping CLI execution test: OpenGL 3.3 Core display/context not available.";
    }
    betacraft::test::TempDir tempDir;
    const auto save = tempDir.path() / "world";
    std::filesystem::copy(std::filesystem::path(BETACRAFT_TEST_DATA_DIR) / "saves" / "v1", save,
                          std::filesystem::copy_options::recursive);
    const std::string cmd = std::string(BETACRAFT_BIN_PATH) + " --frames 3 --render-distance 2 --save-dir \"" +
                            save.string() + "\" > /dev/null 2>&1";
    const int status = std::system(cmd.c_str());
    EXPECT_EQ(WEXITSTATUS(status), 0);

    EXPECT_TRUE(std::filesystem::exists(save / "region" / "r.0.0.bcr"));
    EXPECT_TRUE(std::filesystem::exists(save / "region" / "r.-1.-1.bcr"));
    EXPECT_TRUE(std::filesystem::exists(save / "chunks.migrated" / "c_0_0.chk"));
    EXPECT_TRUE(std::filesystem::exists(save / "chunks.migrated" / "c_0_0.be"));
    EXPECT_FALSE(std::filesystem::exists(save / "chunks"));
    std::ifstream level(save / "level.dat");
    const std::string text((std::istreambuf_iterator<char>(level)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("storage_format 2"), std::string::npos);
    EXPECT_NE(text.find("inv 0 274 1 17"), std::string::npos) << "the inventory is intact";
}
