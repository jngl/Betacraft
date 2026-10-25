// Persistence - a chunk's decoration record (pass-2 flag + feature plan) round-trips
// through bytes, so World can save it next to the chunk and restore it next session.
#include "worldgen/FeaturePlanCodec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace betacraft::worldgen;
using betacraft::core::BlockPos;

namespace {

FeaturePlan samplePlan() {
    return FeaturePlan{
        Feature{FeatureType::OakTree, BlockPos{-17, 70, 31}, 5, 0xDEADBEEFU, FeatureCondition::None},
        Feature{FeatureType::SugarCane, BlockPos{2147483000, 65, -2147483000}, 3, 7U, FeatureCondition::AdjacentWater},
        Feature{FeatureType::Rose, BlockPos{0, 127, 0}, 1, 0U, FeatureCondition::NoAdjacentWater},
    };
}

} // namespace

TEST(FeaturePlanCodec, RoundTripsFlagAndPlan) {
    for (const bool decorated : {false, true}) {
        const DecorationRecord in{decorated, samplePlan()};
        const std::vector<uint8_t> bytes = encodeDecorationRecord(in);
        const auto out = decodeDecorationRecord(bytes);
        ASSERT_TRUE(out.has_value());
        EXPECT_EQ(out->decorated, decorated);
        EXPECT_EQ(out->plan, in.plan);
    }
}

TEST(FeaturePlanCodec, EmptyPlanRoundTrips) {
    const auto out = decodeDecorationRecord(encodeDecorationRecord(DecorationRecord{true, {}}));
    ASSERT_TRUE(out.has_value());
    EXPECT_TRUE(out->decorated);
    EXPECT_TRUE(out->plan.empty());
}

TEST(FeaturePlanCodec, EncodingIsCompactAndStable) {
    const std::vector<uint8_t> bytes = encodeDecorationRecord(DecorationRecord{true, samplePlan()});
    EXPECT_EQ(bytes.size(), FEATURE_RECORD_HEADER_BYTES + 3 * FEATURE_RECORD_BYTES);
    // Little-endian, platform independent: the first feature's x = -17 follows its type byte.
    const size_t x0 = FEATURE_RECORD_HEADER_BYTES + 1;
    EXPECT_EQ(bytes[x0], 0xEF);
    EXPECT_EQ(bytes[x0 + 3], 0xFF);
}

TEST(FeaturePlanCodec, RejectsCorruptInput) {
    const std::vector<uint8_t> good = encodeDecorationRecord(DecorationRecord{false, samplePlan()});
    EXPECT_FALSE(decodeDecorationRecord({}).has_value());

    std::vector<uint8_t> bad = good;
    bad[0] ^= 0xFFU; // magic
    EXPECT_FALSE(decodeDecorationRecord(bad).has_value());

    bad = good;
    bad.pop_back(); // truncated
    EXPECT_FALSE(decodeDecorationRecord(bad).has_value());

    bad = good;
    bad.push_back(0); // trailing bytes
    EXPECT_FALSE(decodeDecorationRecord(bad).has_value());

    bad = good;
    bad[FEATURE_RECORD_HEADER_BYTES] = 0xEE; // unknown feature type
    EXPECT_FALSE(decodeDecorationRecord(bad).has_value());

    bad = good;
    bad[FEATURE_RECORD_HEADER_BYTES + FEATURE_RECORD_BYTES - 1] = 9; // unknown condition
    EXPECT_FALSE(decodeDecorationRecord(bad).has_value());

    bad = good;
    bad[FEATURE_RECORD_HEADER_BYTES - 5] = 0xFE; // unknown flag bits
    EXPECT_FALSE(decodeDecorationRecord(bad).has_value());
}
