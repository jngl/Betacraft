// ChunkBundle: BUNDLE_V1 then {tag, u32 len, bytes} records for blocks ('B'), the
// decoration record ('D') and the block entities ('E'); unknown tags are skipped.
#include "storage/ChunkBundleCodec.hpp"
#include "test_support/GLTestContext.hpp"
#include "voxel/BlockEntityStore.hpp"
#include "voxel/Chunk.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

using namespace betacraft;
using storage::ChunkBundle;
using voxel::BlockId;

namespace {

[[nodiscard]] worldgen::DecorationRecord sampleRecord(bool decorated) {
    return worldgen::DecorationRecord{
        decorated,
        {worldgen::Feature{worldgen::FeatureType::OakTree, core::BlockPos{3, 70, 5}, 5, 0xCAFEU,
                           worldgen::FeatureCondition::None},
         worldgen::Feature{worldgen::FeatureType::Rose, core::BlockPos{9, 66, 1}, 1, 0U,
                           worldgen::FeatureCondition::NoAdjacentWater}}};
}

[[nodiscard]] ChunkBundle sampleBundle() {
    voxel::BlockRegistry::initialize();
    voxel::Chunk chunk({2, -3});
    chunk.setBlock(1, 10, 1, BlockId::Obsidian);
    chunk.setBlock(4, 70, 4, BlockId::Chest);
    voxel::ChestData chest{};
    chest.slots[3] = {265, 9, 0};
    const std::vector<voxel::BlockEntityEntry> entities{{core::BlockPos{2 * 16 + 4, 70, -3 * 16 + 4}, chest}};
    return ChunkBundle{chunk.serializeRLE(), sampleRecord(false), voxel::encodeBlockEntities(entities)};
}

void appendRecord(std::vector<uint8_t>& out, uint8_t tag, const std::vector<uint8_t>& body) {
    out.push_back(tag);
    const auto len = static_cast<uint32_t>(body.size());
    for (int s = 0; s < 32; s += 8) out.push_back(static_cast<uint8_t>(len >> s));
    out.insert(out.end(), body.begin(), body.end());
}

} // namespace

TEST(ChunkBundleCodec, RoundtripAllTags) {
    const ChunkBundle in = sampleBundle();
    const std::vector<uint8_t> bytes = storage::encodeBundle(in);
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes[0], storage::BUNDLE_V1);

    const auto out = storage::decodeBundle(bytes);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->blocks, in.blocks);
    ASSERT_TRUE(out->decoration.has_value());
    EXPECT_EQ(out->decoration->decorated, false);
    EXPECT_EQ(out->decoration->plan, in.decoration->plan);
    EXPECT_EQ(out->blockEntities, in.blockEntities);

    // The parts are the very bytes their own codecs read.
    voxel::Chunk chunk({2, -3});
    ASSERT_TRUE(chunk.deserializeRLE(out->blocks));
    EXPECT_EQ(chunk.getBlock(1, 10, 1), BlockId::Obsidian);
    const auto entities = voxel::decodeBlockEntities({2, -3}, out->blockEntities);
    ASSERT_TRUE(entities.has_value());
    ASSERT_EQ(entities->size(), 1U);
    EXPECT_EQ(std::get<voxel::ChestData>(entities->front().second).slots[3], (voxel::StoredStack{265, 9, 0}));

    // Empty parts are simply left out.
    const auto empty = storage::decodeBundle(storage::encodeBundle(ChunkBundle{}));
    ASSERT_TRUE(empty.has_value());
    EXPECT_TRUE(empty->blocks.empty());
    EXPECT_FALSE(empty->decoration.has_value());
    EXPECT_TRUE(empty->blockEntities.empty());
    EXPECT_EQ(storage::encodeBundle(ChunkBundle{}).size(), 1U);
}

TEST(ChunkBundleCodec, UnknownTagIgnored) {
    const ChunkBundle in = sampleBundle();
    std::vector<uint8_t> bytes{storage::BUNDLE_V1};
    appendRecord(bytes, 'Z', {1, 2, 3, 4, 5, 6, 7}); // From a newer version
    appendRecord(bytes, storage::kBundleTagBlocks, in.blocks);
    appendRecord(bytes, 'x', {});
    appendRecord(bytes, storage::kBundleTagBlockEntities, in.blockEntities);

    const auto out = storage::decodeBundle(bytes);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->blocks, in.blocks);
    EXPECT_EQ(out->blockEntities, in.blockEntities);
    EXPECT_FALSE(out->decoration.has_value());
}

TEST(ChunkBundleCodec, MissingDecorationTagMeansLegacyDecorated) {
    ChunkBundle in = sampleBundle();
    in.decoration.reset();
    const auto out = storage::decodeBundle(storage::encodeBundle(in));
    ASSERT_TRUE(out.has_value());
    EXPECT_FALSE(out->decoration.has_value());

    // World reads such a chunk as a save predating two-pass decoration: already decorated,
    // its blocks untouched by neighbouring trees.
    test::TempDir dir("betacraft_bundle_");
    voxel::BlockRegistry::initialize();
    voxel::Chunk flat({0, 0});
    for (int32_t z = 0; z < voxel::Chunk::DEPTH; ++z)
        for (int32_t x = 0; x < voxel::Chunk::WIDTH; ++x)
            for (int32_t y = 0; y < 66; ++y) flat.setBlock(x, y, z, y == 65 ? BlockId::Grass : BlockId::Stone);
    {
        storage::WorldStorage storage(dir.path());
        ASSERT_TRUE(storage.saveChunkBundle({0, 0}, ChunkBundle{flat.serializeRLE(), std::nullopt, {}}));
        ASSERT_TRUE(storage.flush());
    }
    world::World w(173173173ULL, dir.path());
    for (int32_t z = -1; z <= 1; ++z)
        for (int32_t x = -1; x <= 1; ++x) (void)w.ensureChunkLoaded({x, z});
    EXPECT_TRUE(w.isChunkDecorated({0, 0}));
    for (int32_t z = 0; z < voxel::Chunk::DEPTH; ++z)
        for (int32_t x = 0; x < voxel::Chunk::WIDTH; ++x)
            for (int32_t y = 66; y < voxel::Chunk::HEIGHT; ++y)
                ASSERT_EQ(w.getChunk({0, 0})->getBlock(x, y, z), BlockId::Air) << x << " " << y << " " << z;
}

TEST(ChunkBundleCodec, RejectsWrongVersion) {
    std::vector<uint8_t> bytes = storage::encodeBundle(sampleBundle());
    bytes[0] = 0;
    EXPECT_FALSE(storage::decodeBundle(bytes).has_value());
    bytes[0] = storage::BUNDLE_V1 + 1;
    EXPECT_FALSE(storage::decodeBundle(bytes).has_value()) << "newer bundles are refused, never misread";
    EXPECT_FALSE(storage::decodeBundle({}).has_value());
}

TEST(ChunkBundleCodec, RejectsLengthOverflow) {
    const std::vector<uint8_t> good = storage::encodeBundle(sampleBundle());
    // Lengths claiming more than what follows, up to 0xFFFFFFFF.
    for (const uint32_t len : {static_cast<uint32_t>(good.size()), 0x7FFFFFFFU, 0xFFFFFFFFU}) {
        std::vector<uint8_t> bytes = good;
        for (int s = 0; s < 4; ++s) bytes[2 + static_cast<size_t>(s)] = static_cast<uint8_t>(len >> (8 * s));
        EXPECT_FALSE(storage::decodeBundle(bytes).has_value()) << len;
    }
    // A record header cut short.
    std::vector<uint8_t> cut{storage::BUNDLE_V1, storage::kBundleTagBlocks, 1, 0};
    EXPECT_FALSE(storage::decodeBundle(cut).has_value());
    // Every truncation of a valid bundle is either refused or a shorter valid prefix.
    for (size_t n = 1; n < good.size(); n += 97) {
        const auto out = storage::decodeBundle(std::span(good.data(), n));
        if (out) {
            EXPECT_LE(out->blocks.size() + out->blockEntities.size(), n);
        }
    }
}
