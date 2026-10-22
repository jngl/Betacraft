// P2.1 — EntityId encoding (24-bit index | 8-bit generation).
#include "ecs/Entity.hpp"

#include <gtest/gtest.h>

#include <cstdint>

using namespace betacraft::ecs;

TEST(EcsEntity, NullEntityIsAllOnes) {
    EXPECT_EQ(NULL_ENTITY, 0xFFFFFFFFU);
    EXPECT_EQ(entityIndex(NULL_ENTITY), ENTITY_INDEX_MASK);
    EXPECT_EQ(entityGeneration(NULL_ENTITY), ENTITY_GENERATION_MASK);
}

TEST(EcsEntity, BitLayoutIs24IndexBits8GenerationBits) {
    EXPECT_EQ(ENTITY_INDEX_BITS, 24U);
    EXPECT_EQ(ENTITY_GENERATION_BITS, 8U);
    EXPECT_EQ(ENTITY_INDEX_MASK, 0x00FFFFFFU);
    EXPECT_EQ(ENTITY_GENERATION_MASK, 0xFFU);
    // The all-ones index is reserved for NULL_ENTITY, so usable indices stop one below it.
    EXPECT_EQ(MAX_ENTITY_INDEX, 0x00FFFFFEU);
}

TEST(EcsEntity, MakeEntityRoundTripsIndexAndGeneration) {
    const EntityId e = makeEntity(1234U, 7U);
    EXPECT_EQ(entityIndex(e), 1234U);
    EXPECT_EQ(entityGeneration(e), 7U);
}

TEST(EcsEntity, MakeEntityBoundaryValues) {
    const EntityId lo = makeEntity(0U, 0U);
    EXPECT_EQ(lo, 0U);
    EXPECT_EQ(entityIndex(lo), 0U);
    EXPECT_EQ(entityGeneration(lo), 0U);

    const EntityId hi = makeEntity(MAX_ENTITY_INDEX, 0xFFU);
    EXPECT_EQ(entityIndex(hi), MAX_ENTITY_INDEX);
    EXPECT_EQ(entityGeneration(hi), 0xFFU);
    EXPECT_NE(hi, NULL_ENTITY);
}

TEST(EcsEntity, MakeEntityMasksOverflowingInputs) {
    // Out-of-range bits must never leak into the neighbouring field.
    const EntityId e = makeEntity(0x01000005U, 0x103U);
    EXPECT_EQ(entityIndex(e), 5U);
    EXPECT_EQ(entityGeneration(e), 3U);
}

TEST(EcsEntity, GenerationOccupiesHighByte) {
    EXPECT_EQ(makeEntity(0U, 1U), 0x01000000U);
    EXPECT_EQ(makeEntity(1U, 0U), 0x00000001U);
}

TEST(EcsEntity, HelpersAreConstexpr) {
    static_assert(entityIndex(makeEntity(42U, 3U)) == 42U);
    static_assert(entityGeneration(makeEntity(42U, 3U)) == 3U);
    static_assert(NULL_ENTITY == 0xFFFFFFFFU);
    SUCCEED();
}

TEST(EcsEntity, ExhaustiveRoundTripOnSampledIndices) {
    for (uint32_t gen = 0; gen <= 0xFFU; ++gen) {
        for (uint32_t idx : {0U, 1U, 4095U, 4096U, 65535U, 65536U, 0x7FFFFFU, MAX_ENTITY_INDEX}) {
            const EntityId e = makeEntity(idx, gen);
            ASSERT_EQ(entityIndex(e), idx);
            ASSERT_EQ(entityGeneration(e), gen);
        }
    }
}
