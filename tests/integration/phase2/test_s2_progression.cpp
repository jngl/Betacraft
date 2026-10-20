// Sprint 2 integration: a scripted Survival run of the material progression, through the
// game's own rules only (WorldSession ticks: mining and pickup, the InventoryCrafting screen with
// the vanilla book, the furnace screen and its block entity): log -> planks -> workbench ->
// wooden pickaxe -> cobblestone -> stone pickaxe -> furnace -> iron ingots -> iron pickaxe
// -> a diamond. Raw ore never makes a tool.
#include "WorldSessionSupport.hpp"

#include "crafting/ItemIds.hpp"
#include "crafting/SmeltingRecipes.hpp"
#include "voxel/BlockEntityStore.hpp"

#include <variant>

using namespace betacraft;
using namespace betacraft::test::worldsession;
using crafting::GridSize;
using game::SlotButton;
using physics::Player;

namespace {

namespace items = gameplay::items;
using gameplay::toItemId;

constexpr core::BlockPos kMineCell{4, kY, 2};
constexpr glm::vec3 kHome{2.5f, static_cast<float>(kY), 2.5f};

class S2Progression : public WorldSessionSupport {
protected:
    void SetUp() override {
        WorldSessionSupport::SetUp();
        player().setGameMode(core::GameMode::Survival);
    }

    game::InventoryCrafting& ui() { return m_session->crafting(); }

    [[nodiscard]] size_t findSlot(gameplay::ItemId id) const {
        for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) {
            const physics::InventorySlot s = player().getInventorySlot(i);
            if (!s.isEmpty() && s.id == id) return i;
        }
        return Player::TOTAL_INVENTORY_SIZE;
    }

    [[nodiscard]] size_t firstEmpty() const {
        for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) {
            if (player().getInventorySlot(i).isEmpty()) return i;
        }
        return Player::TOTAL_INVENTORY_SIZE;
    }

    // Mines `block` placed at kMineCell with whatever is held, then walks over the drops.
    void mineAndCollect(BlockId block) {
        ASSERT_TRUE(world().setBlock(kMineCell, block));
        const MineResult r = mine(kMineCell);
        ASSERT_TRUE(r.broken.has_value()) << static_cast<int>(block);
        placePlayer({kMineCell.x + 0.5f, static_cast<float>(kY), kMineCell.z + 0.5f});
        tickIdle(40);
        placePlayer(kHome);
    }

    // Moves the inventory stack `id` into the selected hotbar slot 0 with screen clicks.
    void equip(gameplay::ItemId id) {
        const size_t from = findSlot(id);
        ASSERT_LT(from, Player::TOTAL_INVENTORY_SIZE) << id;
        if (from == 0) return;
        ASSERT_TRUE(ui().clickInventorySlot(player(), from));
        ASSERT_TRUE(ui().clickInventorySlot(player(), 0));
        if (!ui().carried().isEmpty()) {
            ASSERT_TRUE(ui().clickInventorySlot(player(), from));
        }
        ASSERT_TRUE(ui().carried().isEmpty());
        player().setSelectedSlot(0);
    }

    void placeOne(gameplay::ItemId id, size_t cell) {
        const size_t from = findSlot(id);
        ASSERT_LT(from, Player::TOTAL_INVENTORY_SIZE) << "no " << id << " left";
        ASSERT_TRUE(ui().clickInventorySlot(player(), from));
        ASSERT_TRUE(ui().clickGridSlot(cell, SlotButton::Right));
        if (!ui().carried().isEmpty()) {
            ASSERT_TRUE(ui().clickInventorySlot(player(), from));
        }
    }

    // Puts one item per listed grid cell, crafts, stashes the result; `times` times over.
    void craft(std::initializer_list<std::pair<size_t, gameplay::ItemId>> cells, gameplay::ItemId expected,
               int times = 1) {
        for (int t = 0; t < times; ++t) {
            for (const auto& [cell, id] : cells) placeOne(id, cell);
            ASSERT_EQ(ui().resultPreview().id, expected);
            ASSERT_TRUE(ui().clickResult(player()));
            const size_t to = findSlot(expected) < Player::TOTAL_INVENTORY_SIZE &&
                                      gameplay::maxStackSize(expected) > 1
                                  ? findSlot(expected)
                                  : firstEmpty();
            ASSERT_TRUE(ui().clickInventorySlot(player(), to));
            ASSERT_TRUE(ui().carried().isEmpty());
            ASSERT_TRUE(ui().grid().isEmpty());
        }
    }

    // Pickaxe layout in a 3x3 grid: X X X / . # . / . # .
    void craftPickaxe(gameplay::ItemId material, gameplay::ItemId expected) {
        craft({{0, material}, {1, material}, {2, material}, {4, items::Stick}, {7, items::Stick}}, expected);
    }
};

} // namespace

TEST_F(S2Progression, FromALogToADiamond) {
    // Wood by hand, then planks and sticks in the 2x2 inventory grid.
    for (int i = 0; i < 3; ++i) mineAndCollect(BlockId::OakLog);
    ASSERT_EQ(player().countItem(BlockId::OakLog), 3);
    (void)ui().open(GridSize::Inventory2x2, player());
    craft({{0, toItemId(BlockId::OakLog)}}, toItemId(BlockId::OakPlanks), 3);
    ASSERT_EQ(player().countItem(BlockId::OakPlanks), 12);
    craft({{0, toItemId(BlockId::OakPlanks)}, {1, toItemId(BlockId::OakPlanks)}, {2, toItemId(BlockId::OakPlanks)},
           {3, toItemId(BlockId::OakPlanks)}},
          toItemId(BlockId::CraftingTable));
    craft({{0, toItemId(BlockId::OakPlanks)}, {2, toItemId(BlockId::OakPlanks)}}, items::Stick, 2);
    ASSERT_EQ(player().countItem(items::Stick), 8);
    EXPECT_EQ(findSlot(gameplay::items::legacy::Stick), Player::TOTAL_INVENTORY_SIZE) << "Beta stick id only";

    // The workbench: a wooden pickaxe.
    (void)ui().open(GridSize::Workbench3x3, player());
    craftPickaxe(toItemId(BlockId::OakPlanks), items::WoodPickaxe);
    EXPECT_TRUE(ui().close(player()).empty());

    // Stone and coal with the wooden pickaxe.
    equip(items::WoodPickaxe);
    for (int i = 0; i < 11; ++i) mineAndCollect(BlockId::Stone);
    mineAndCollect(BlockId::CoalOre);
    ASSERT_EQ(player().countItem(BlockId::Cobblestone), 11);
    ASSERT_EQ(player().countItem(items::Coal), 1);

    (void)ui().open(GridSize::Workbench3x3, player());
    craftPickaxe(toItemId(BlockId::Cobblestone), items::StonePickaxe);
    const gameplay::ItemId cobble = toItemId(BlockId::Cobblestone);
    craft({{0, cobble}, {1, cobble}, {2, cobble}, {3, cobble}, {5, cobble}, {6, cobble}, {7, cobble}, {8, cobble}},
          toItemId(BlockId::Furnace));
    EXPECT_TRUE(ui().close(player()).empty());

    // Iron ore with the stone pickaxe. A pickaxe of raw ore is refused by the game book.
    equip(items::StonePickaxe);
    for (int i = 0; i < 3; ++i) mineAndCollect(BlockId::IronOre);
    ASSERT_EQ(player().countItem(BlockId::IronOre), 3);
    (void)ui().open(GridSize::Workbench3x3, player());
    for (const size_t cell : {0, 1, 2}) placeOne(toItemId(BlockId::IronOre), cell);
    placeOne(items::Stick, 4);
    placeOne(items::Stick, 7);
    EXPECT_TRUE(ui().resultPreview().isEmpty()) << "raw ore makes no tool (S2.8)";
    EXPECT_TRUE(ui().close(player()).empty());
    ASSERT_EQ(player().countItem(BlockId::IronOre), 3);

    // Place the furnace, load it through its screen, let it burn.
    equip(toItemId(BlockId::Furnace));
    const core::BlockPos furnacePos = kMineCell;
    physics::RaycastHit floor = targetAt({furnacePos.x, kFloorY, furnacePos.z}, BlockId::Glass);
    floor.adjacentPos = furnacePos;
    floor.face = core::FaceDir::PosY;
    ASSERT_EQ(game::placeHeldBlock(world(), player(), floor), std::optional<BlockId>{BlockId::Furnace});
    (void)world().blockEntities().getOrCreate(furnacePos, BlockId::Furnace);
    EXPECT_TRUE(ui().openContainer(ui::ScreenKind::Furnace, furnacePos, player()).empty());
    ASSERT_TRUE(ui().clickInventorySlot(player(), findSlot(toItemId(BlockId::IronOre))));
    ASSERT_TRUE(ui().clickContainerSlot(world(), ui::kFurnaceInputSlot));
    ASSERT_TRUE(ui().clickInventorySlot(player(), findSlot(items::Coal)));
    ASSERT_TRUE(ui().clickContainerSlot(world(), ui::kFurnaceFuelSlot));
    EXPECT_TRUE(ui().close(player()).empty());

    tickIdle(3 * crafting::kSmeltTicks + 2);
    EXPECT_EQ(world().getBlock(furnacePos), BlockId::LitFurnace) << "coal burns 1600 ticks";
    EXPECT_TRUE(ui().openContainer(ui::ScreenKind::Furnace, furnacePos, player()).empty());
    EXPECT_EQ(ui().containerSlot(world(), ui::kFurnaceOutputSlot), physics::InventorySlot(items::IronIngot, 3));
    EXPECT_TRUE(ui().containerSlot(world(), ui::kFurnaceInputSlot).isEmpty());
    ASSERT_TRUE(ui().clickContainerSlot(world(), ui::kFurnaceOutputSlot));
    ASSERT_TRUE(ui().clickInventorySlot(player(), firstEmpty()));
    EXPECT_TRUE(ui().close(player()).empty());
    ASSERT_EQ(player().countItem(items::IronIngot), 3);

    // The iron pickaxe, then a diamond (a stone pickaxe would yield nothing).
    (void)ui().open(GridSize::Workbench3x3, player());
    craftPickaxe(items::IronIngot, items::IronPickaxe);
    EXPECT_TRUE(ui().close(player()).empty());
    equip(items::IronPickaxe);
    const core::BlockPos oreCell{kMineCell.x, kY, kMineCell.z + 2};
    ASSERT_TRUE(world().setBlock(oreCell, BlockId::DiamondOre));
    ASSERT_TRUE(mine(oreCell).broken.has_value());
    placePlayer({oreCell.x + 0.5f, static_cast<float>(kY), oreCell.z + 0.5f});
    tickIdle(40);
    EXPECT_EQ(player().countItem(items::Diamond), 1);
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) {
        EXPECT_FALSE(gameplay::isLegacyItemId(player().getInventorySlot(i).id)) << i;
    }
}
