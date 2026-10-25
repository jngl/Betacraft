// X renderer/HUD: every item of the shared id scheme has a display
// (render::ItemAppearance): blocks keep their BlockDef look, tools at 256 and above get
// procedural icon tiles, and the shipped atlas PNG (exported before those icons existed)
// receives them through TextureAtlas::fillPlaceholderTiles.
#include "render/ItemAppearance.hpp"
#include "render/TextureAtlas.hpp"

#include "gameplay/ItemIds.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>

#include <set>

using namespace betacraft;
using gameplay::ToolKind;
using gameplay::ToolMaterial;
using render::TextureAtlas;
using voxel::BlockId;
namespace items = gameplay::items;

namespace {

constexpr ToolKind kKinds[] = {ToolKind::Pickaxe, ToolKind::Axe, ToolKind::Shovel, ToolKind::Sword, ToolKind::Hoe};
constexpr ToolMaterial kMaterials[] = {ToolMaterial::Wood, ToolMaterial::Stone, ToolMaterial::Iron, ToolMaterial::Gold,
                                       ToolMaterial::Diamond};

} // namespace

TEST(ItemAppearance, BlocksKeepTheirBlockDefLook) {
    voxel::BlockRegistry::initialize();
    const render::ItemAppearance stone = render::itemAppearance(gameplay::toItemId(BlockId::Stone));
    EXPECT_EQ(stone.name, "Stone");
    EXPECT_FALSE(stone.flat);
    EXPECT_EQ(stone.block, BlockId::Stone);
    EXPECT_EQ(stone.faceTiles, voxel::BlockRegistry::get(BlockId::Stone).faceTiles);
    EXPECT_TRUE(render::itemAppearance(gameplay::toItemId(BlockId::Torch)).flat);
    const render::ItemAppearance pick = render::itemAppearance(items::DiamondPickaxe);
    EXPECT_EQ(pick.name, "Diamond Pickaxe");
    EXPECT_TRUE(pick.flat);
    EXPECT_FALSE(pick.block.has_value());
}

TEST(ItemAppearance, EveryToolHasItsOwnNamedIcon) {
    voxel::BlockRegistry::initialize();
    std::set<uint8_t> tiles;
    for (const ToolKind k : kKinds) {
        for (const ToolMaterial m : kMaterials) {
            const gameplay::ItemId id = gameplay::toolItemId(k, m);
            const render::ItemAppearance a = render::itemAppearance(id);
            EXPECT_TRUE(a.flat) << id;
            EXPECT_FALSE(a.name.empty()) << id;
            EXPECT_EQ(a.name.find("Item #"), std::string::npos) << id;
            EXPECT_NE(a.iconTile(), render::kGenericItemTile) << id;
            tiles.insert(a.iconTile());
        }
    }
    EXPECT_EQ(tiles.size(), 25U);
    EXPECT_EQ(render::itemAppearance(items::beta::WoodPickaxe).name, "Wooden Pickaxe"); // vanilla alias
}

TEST(ItemAppearance, OtherItemsGetNamesAndTheGenericSprite) {
    EXPECT_EQ(render::itemAppearance(items::GlowstoneDust).name, "Glowstone Dust");
    EXPECT_EQ(render::itemAppearance(items::GlowstoneDust).iconTile(), render::kGlowstoneDustTile);
    EXPECT_EQ(render::itemAppearance(items::Bucket).name, "Bucket");
    EXPECT_EQ(render::itemAppearance(items::Bucket).iconTile(), render::kGenericItemTile);
    EXPECT_EQ(render::itemAppearance(items::Chest).name, "Chest");
    EXPECT_EQ(render::itemAppearance(999).name, "Item #999");
}

TEST(ItemAppearance, ProceduralIconTilesAreDrawn) {
    const std::vector<uint8_t> atlas = TextureAtlas::generateProceduralAtlasRGBA();
    for (const ToolKind k : kKinds) {
        for (const ToolMaterial m : kMaterials) {
            const auto tile = render::toolIconTile(k, m);
            if (tile) {
                EXPECT_FALSE(TextureAtlas::isPlaceholderTile(atlas, *tile)) << int(*tile);
            }
        }
    }
    EXPECT_FALSE(TextureAtlas::isPlaceholderTile(atlas, render::kGlowstoneDustTile));
    EXPECT_FALSE(TextureAtlas::isPlaceholderTile(atlas, render::kGenericItemTile));
    EXPECT_FALSE(TextureAtlas::isPlaceholderTile(atlas, 237)); // lava
    EXPECT_TRUE(TextureAtlas::isPlaceholderTile(atlas, 255));  // never assigned
}

TEST(ItemAppearance, PlaceholderTilesOfALoadedAtlasAreFilledFromTheProceduralOne) {
    const std::vector<uint8_t> procedural = TextureAtlas::generateProceduralAtlasRGBA();
    // An "old export": the same atlas with the item icons and lava still grey.
    std::vector<uint8_t> loaded = procedural;
    const uint8_t stale[] = {render::kGoldPickaxeTile, render::kGlowstoneDustTile, 237};
    for (const uint8_t tile : stale) {
        const int tx = (tile % TextureAtlas::TILES_PER_ROW) * TextureAtlas::TILE_SIZE;
        const int ty = (tile / TextureAtlas::TILES_PER_ROW) * TextureAtlas::TILE_SIZE;
        for (int y = 0; y < TextureAtlas::TILE_SIZE; ++y) {
            for (int x = 0; x < TextureAtlas::TILE_SIZE; ++x) {
                const size_t i = (static_cast<size_t>(ty + y) * TextureAtlas::ATLAS_SIZE + static_cast<size_t>(tx + x)) * 4U;
                loaded[i] = loaded[i + 1] = loaded[i + 2] = 128;
                loaded[i + 3] = 255;
            }
        }
        ASSERT_TRUE(TextureAtlas::isPlaceholderTile(loaded, tile));
    }
    // A customised tile (texture pack) is kept even where the procedural one differs.
    loaded[0] = 1;

    const size_t filled = TextureAtlas::fillPlaceholderTiles(loaded, procedural);
    EXPECT_EQ(filled, 3U);
    EXPECT_EQ(loaded[0], 1);
    loaded[0] = procedural[0];
    EXPECT_EQ(loaded, procedural); // grey tiles that are grey procedurally too stay grey
    std::vector<uint8_t> wrongSize(16, 0);
    EXPECT_EQ(TextureAtlas::fillPlaceholderTiles(wrongSize, procedural), 0U);
}
