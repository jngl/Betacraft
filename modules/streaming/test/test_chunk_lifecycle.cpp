#include "streaming/ChunkLifecycle.hpp"

#include <gtest/gtest.h>

#include <array>

using betacraft::core::ChunkCoord;
using betacraft::streaming::ChunkLifecycleTracker;
using betacraft::streaming::ChunkStage;
using betacraft::streaming::canTransition;
using betacraft::streaming::stageName;

namespace {
constexpr std::array kAll{ChunkStage::Unloaded, ChunkStage::TerrainGen, ChunkStage::Lit,
                          ChunkStage::Decorated, ChunkStage::Meshed, ChunkStage::Uploaded};
}

TEST(ChunkLifecycle, ForwardPathIsOneStepAtATime) {
    EXPECT_TRUE(canTransition(ChunkStage::Unloaded, ChunkStage::TerrainGen));
    EXPECT_TRUE(canTransition(ChunkStage::TerrainGen, ChunkStage::Lit));
    EXPECT_TRUE(canTransition(ChunkStage::Lit, ChunkStage::Decorated));
    EXPECT_TRUE(canTransition(ChunkStage::Decorated, ChunkStage::Meshed));
    EXPECT_TRUE(canTransition(ChunkStage::Meshed, ChunkStage::Uploaded));
}

TEST(ChunkLifecycle, SkippingStagesIsRejected) {
    EXPECT_FALSE(canTransition(ChunkStage::Unloaded, ChunkStage::Lit));
    EXPECT_FALSE(canTransition(ChunkStage::TerrainGen, ChunkStage::Meshed));
    EXPECT_FALSE(canTransition(ChunkStage::Decorated, ChunkStage::Uploaded));
}

TEST(ChunkLifecycle, AnyLoadedStageCanUnload) {
    for (ChunkStage s : kAll) {
        EXPECT_EQ(canTransition(s, ChunkStage::Unloaded), s != ChunkStage::Unloaded) << stageName(s);
    }
}

TEST(ChunkLifecycle, EditInvalidatesMeshBackToDecorated) {
    EXPECT_TRUE(canTransition(ChunkStage::Meshed, ChunkStage::Decorated));
    EXPECT_TRUE(canTransition(ChunkStage::Uploaded, ChunkStage::Decorated));
    EXPECT_FALSE(canTransition(ChunkStage::Lit, ChunkStage::TerrainGen));
    EXPECT_FALSE(canTransition(ChunkStage::Uploaded, ChunkStage::Lit));
}

TEST(ChunkLifecycle, SelfTransitionsAreNotTransitions) {
    for (ChunkStage s : kAll) EXPECT_FALSE(canTransition(s, s)) << stageName(s);
}

TEST(ChunkLifecycle, ExhaustiveTransitionTableCount) {
    int legal = 0;
    for (ChunkStage a : kAll)
        for (ChunkStage b : kAll) legal += canTransition(a, b) ? 1 : 0;
    // 5 forward + 5 unload + 2 invalidations
    EXPECT_EQ(legal, 12);
}

TEST(ChunkLifecycle, StageNamesAreDistinct) {
    for (ChunkStage a : kAll) {
        for (ChunkStage b : kAll) {
            if (a != b) {
                EXPECT_NE(stageName(a), stageName(b));
            }
        }
    }
}

TEST(ChunkLifecycleTracker, UnknownChunksAreUnloaded) {
    ChunkLifecycleTracker t;
    EXPECT_EQ(t.stage({3, -4}), ChunkStage::Unloaded);
    EXPECT_EQ(t.trackedCount(), 0u);
}

TEST(ChunkLifecycleTracker, AdvanceAppliesOnlyLegalTransitions) {
    ChunkLifecycleTracker t;
    const ChunkCoord c{1, 2};
    EXPECT_FALSE(t.advance(c, ChunkStage::Meshed));
    EXPECT_EQ(t.stage(c), ChunkStage::Unloaded);
    EXPECT_TRUE(t.advance(c, ChunkStage::TerrainGen));
    EXPECT_TRUE(t.advance(c, ChunkStage::Lit));
    EXPECT_TRUE(t.advance(c, ChunkStage::Decorated));
    EXPECT_TRUE(t.advance(c, ChunkStage::Meshed));
    EXPECT_TRUE(t.advance(c, ChunkStage::Uploaded));
    EXPECT_EQ(t.stage(c), ChunkStage::Uploaded);
    EXPECT_EQ(t.countIn(ChunkStage::Uploaded), 1u);
    EXPECT_EQ(t.rejectedTransitions(), 1u);
}

TEST(ChunkLifecycleTracker, UnloadForgetsTheChunk) {
    ChunkLifecycleTracker t;
    const ChunkCoord c{0, 0};
    ASSERT_TRUE(t.advance(c, ChunkStage::TerrainGen));
    EXPECT_EQ(t.trackedCount(), 1u);
    EXPECT_TRUE(t.advance(c, ChunkStage::Unloaded));
    EXPECT_EQ(t.trackedCount(), 0u);
    EXPECT_EQ(t.stage(c), ChunkStage::Unloaded);
}
