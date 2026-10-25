// FluidState nibble encoding (bit 3 = falling, bits 0..2 = level, 7 = source).
#include "fluids/FluidState.hpp"

#include <gtest/gtest.h>

using betacraft::fluids::FluidState;
using betacraft::fluids::FluidType;

TEST(FluidState, DefaultIsDry) {
    constexpr FluidState s{};
    EXPECT_EQ(s.type, FluidType::None);
    EXPECT_TRUE(s.isEmpty());
    EXPECT_FALSE(s.isSource());
    EXPECT_FALSE(s.isFalling());
    EXPECT_EQ(s.toNibble(), 0U);
}

TEST(FluidState, SourceIsLevelSevenNotFalling) {
    constexpr FluidState s = FluidState::source(FluidType::Water);
    EXPECT_EQ(s.type, FluidType::Water);
    EXPECT_EQ(s.level, 7U);
    EXPECT_TRUE(s.isSource());
    EXPECT_FALSE(s.isFalling());
    EXPECT_FALSE(s.isEmpty());
    EXPECT_EQ(s.toNibble(), 0x7U);
}

TEST(FluidState, FallingFluidResetsLevelToFull) {
    constexpr FluidState f = FluidState::falling(FluidType::Lava);
    EXPECT_TRUE(f.isFalling());
    EXPECT_EQ(f.level, 7U);
    // A falling cell is never a source even though its level is full.
    EXPECT_FALSE(f.isSource());
    EXPECT_EQ(f.toNibble(), 0xFU);
}

TEST(FluidState, FlowingLevelsRoundTripThroughNibble) {
    for (uint8_t level = 0; level <= 6; ++level) {
        const FluidState s = FluidState::flowing(FluidType::Water, level);
        EXPECT_FALSE(s.isSource());
        EXPECT_FALSE(s.isFalling());
        EXPECT_EQ(s.toNibble(), level);
        EXPECT_EQ(FluidState::fromNibble(FluidType::Water, s.toNibble()), s);
    }
}

TEST(FluidState, AllNibblesRoundTrip) {
    for (uint8_t n = 0; n < 16; ++n) {
        const FluidState s = FluidState::fromNibble(FluidType::Lava, n);
        EXPECT_EQ(s.toNibble(), n) << "nibble " << int(n);
        EXPECT_EQ(s.isFalling(), (n & 0x8U) != 0U);
        EXPECT_EQ(s.level, n & 0x7U);
    }
}

TEST(FluidState, FromNibbleIgnoresUpperBits) {
    const FluidState s = FluidState::fromNibble(FluidType::Water, 0xFBU);
    EXPECT_EQ(s.toNibble(), 0xBU);
}

TEST(FluidState, FlowingClampsOutOfRangeLevel) {
    const FluidState s = FluidState::flowing(FluidType::Water, 42);
    EXPECT_EQ(s.level, 7U);
}

TEST(FluidState, NoneTypeIsEmptyRegardlessOfNibble) {
    const FluidState s = FluidState::fromNibble(FluidType::None, 0x7U);
    EXPECT_TRUE(s.isEmpty());
    EXPECT_FALSE(s.isSource());
}

TEST(FluidState, BetaDecayConversion) {
    // Beta stores "decay": 0 = source, 1..7 flowing, >= 8 falling, -1 = no fluid.
    EXPECT_EQ(FluidState{}.betaDecay(), -1);
    EXPECT_EQ(FluidState::source(FluidType::Water).betaDecay(), 0);
    EXPECT_EQ(FluidState::flowing(FluidType::Water, 6).betaDecay(), 1);
    EXPECT_EQ(FluidState::flowing(FluidType::Water, 0).betaDecay(), 7);
    EXPECT_EQ(FluidState::falling(FluidType::Water).betaDecay(), 8);
    for (int d = 0; d <= 8; ++d) {
        EXPECT_EQ(FluidState::fromBetaDecay(FluidType::Water, d).betaDecay(), d);
    }
    EXPECT_TRUE(FluidState::fromBetaDecay(FluidType::Water, -1).isEmpty());
    EXPECT_TRUE(FluidState::fromBetaDecay(FluidType::Water, 13).isFalling());
}

TEST(FluidState, EqualityComparesAllFields) {
    EXPECT_EQ(FluidState::source(FluidType::Water), FluidState::source(FluidType::Water));
    EXPECT_NE(FluidState::source(FluidType::Water), FluidState::source(FluidType::Lava));
    EXPECT_NE(FluidState::flowing(FluidType::Water, 3), FluidState::flowing(FluidType::Water, 4));
}
