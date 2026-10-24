// GL-free WorldSession crafting (spec #9, ticket #14): the inventory screen's grid, cursor and
// opened container belong to the world session, not to the entity simulation.

#include "tests/integration/support/WorldSessionSupport.hpp"

#include "crafting/CraftingGrid.hpp"
#include "gameplay/ItemIds.hpp"

#include <gtest/gtest.h>

namespace betacraft::test::worldsession {
namespace {

using physics::InventorySlot;

class WorldSessionCrafting : public WorldSessionSupport {};

TEST_F(WorldSessionCrafting, TheWorldSessionOwnsTheCraftingScreen) {
    ASSERT_TRUE(player().addItem(BlockId::OakLog, 1));
    (void)m_session->crafting().open(crafting::GridSize::Inventory2x2, player());
    ASSERT_TRUE(m_session->crafting().clickInventorySlot(player(), 0));
    ASSERT_TRUE(m_session->crafting().clickGridSlot(1));
    EXPECT_EQ(m_session->crafting().resultPreview(), InventorySlot(BlockId::OakPlanks, 4));
}

} // namespace
} // namespace betacraft::test::worldsession
