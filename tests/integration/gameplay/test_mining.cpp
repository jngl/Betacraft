// Phase 2 integration, P2.5 tools in the game loop: game::EntitySimulation mines with Beta's
// hold-to-break rules (gameplay::MiningProgress + computeDig on the 20 TPS tick), drops only
// what the held tool may harvest as ECS item entities, wears the held tool and breaks it
// after its durability, and exposes the crack stage the renderer draws.
#include "tests/integration/support/GameplaySupport.hpp"

#include "gameplay/Harvesting.hpp"
#include "gameplay/ItemIds.hpp"

#include <gtest/gtest.h>

using namespace betacraft;
using namespace betacraft::test::p2gameplay;
using gameplay::WearResult;
using physics::InventorySlot;
namespace items = gameplay::items;

namespace {

const core::BlockPos kRock{5, kY, 2};

class GameplayMining : public GameplayWorld {};

} // namespace

TEST_F(GameplayMining, StoneByHandIsSlowAndDropsNothing) {
    ASSERT_TRUE(m_world->setBlock(kRock, BlockId::Stone));
    const MineResult r = mine(kRock);

    ASSERT_TRUE(r.broken.has_value());
    EXPECT_EQ(r.ticks, 150); // Beta: hardness 1.5 x 100 ticks without the right tool
    EXPECT_EQ(r.ticks, gameplay::computeDig(BlockId::Stone, {}).ticks);
    EXPECT_FALSE(r.broken->dropped);
    EXPECT_EQ(r.broken->block, BlockId::Stone);
    EXPECT_EQ(m_world->getBlock(kRock), BlockId::Air);
    EXPECT_EQ(itemEntityCount(), 0U);
    EXPECT_TRUE(m_player.getHeldItem().isEmpty());
}

TEST_F(GameplayMining, StoneWithAWoodenPickaxeIsFasterDropsCobbleAndWearsTheTool) {
    ASSERT_TRUE(m_world->setBlock(kRock, BlockId::Stone));
    hold({items::WoodPickaxe, 1});
    const MineResult r = mine(kRock);

    ASSERT_TRUE(r.broken.has_value());
    EXPECT_EQ(r.ticks, 23); // ceil(1.5 x 30 / 2): harvestable, wood efficiency 2
    EXPECT_TRUE(r.broken->dropped);
    EXPECT_EQ(r.wear, WearResult::Worn);
    EXPECT_EQ(m_player.getHeldItem(), InventorySlot(items::WoodPickaxe, 1, 1));

    ASSERT_EQ(itemEntityCount(), 1U);
    const auto& drop = m_session.entities().get<ecs::ItemDropComp>(r.broken->drop);
    EXPECT_EQ(drop.id, BlockId::Cobblestone);
    EXPECT_EQ(drop.count, 1);
    const auto& t = m_session.entities().get<ecs::TransformComp>(r.broken->drop);
    EXPECT_NEAR(t.pos.x, 5.5f, 0.5f);
    EXPECT_NEAR(t.pos.z, 2.5f, 0.5f);
}

TEST_F(GameplayMining, OreDropsOnlyWithASufficientHarvestLevel) {
    ASSERT_TRUE(m_world->setBlock(kRock, BlockId::IronOre));
    hold({items::WoodPickaxe, 1});
    const MineResult wood = mine(kRock);
    ASSERT_TRUE(wood.broken.has_value());
    EXPECT_FALSE(wood.broken->dropped); // Iron needs a stone pickaxe (level 1)
    EXPECT_EQ(itemEntityCount(), 0U);
    EXPECT_EQ(m_player.getHeldItem().damage, 1); // Beta still wears the tool

    ASSERT_TRUE(m_world->setBlock(kRock, BlockId::IronOre));
    hold({items::StonePickaxe, 1});
    const MineResult stone = mine(kRock);
    ASSERT_TRUE(stone.broken.has_value());
    EXPECT_TRUE(stone.broken->dropped);
    ASSERT_EQ(itemEntityCount(), 1U);
    EXPECT_EQ(m_session.entities().get<ecs::ItemDropComp>(stone.broken->drop).id, BlockId::IronOre);
    EXPECT_LT(stone.ticks, wood.ticks);
}

TEST_F(GameplayMining, AWoodenPickaxeBreaksAfterItsDurability) {
    hold({items::WoodPickaxe, 1});
    const int32_t uses = gameplay::materialStats(gameplay::ToolMaterial::Wood).maxUses + 1; // Beta: 60 blocks
    for (int32_t i = 1; i <= uses; ++i) {
        ASSERT_TRUE(m_world->setBlock(kRock, BlockId::Stone));
        const MineResult r = mine(kRock);
        ASSERT_TRUE(r.broken.has_value()) << "block " << i;
        ASSERT_TRUE(r.broken->dropped) << "block " << i; // The breaking use still harvests
        if (i < uses) {
            ASSERT_EQ(r.wear, WearResult::Worn) << "block " << i;
            ASSERT_EQ(m_player.getHeldItem(), InventorySlot(items::WoodPickaxe, 1, i)) << "block " << i;
        } else {
            EXPECT_EQ(r.wear, WearResult::Broken);
            EXPECT_TRUE(m_player.getHeldItem().isEmpty());
        }
    }
    // The next stone is mined by hand again: slow, and nothing drops.
    ASSERT_TRUE(m_world->setBlock(kRock, BlockId::Stone));
    const MineResult bare = mine(kRock);
    EXPECT_EQ(bare.ticks, 150);
    EXPECT_FALSE(bare.broken->dropped);
}

TEST_F(GameplayMining, CrackStageFollowsTheDigAndClearsOnRelease) {
    ASSERT_TRUE(m_world->setBlock(kRock, BlockId::Stone));
    EXPECT_EQ(m_session.crackStage(), -1);
    const physics::RaycastHit target = targetAt(kRock, BlockId::Stone);
    int32_t previous = 0;
    for (int32_t t = 0; t < 75; ++t) {
        (void)m_session.tick(*m_world, m_player, {.attackPressed = t == 0, .attackHeld = true, .target = target});
        const int32_t stage = m_session.crackStage();
        // Stage = floor(10 x progress); the progress sum may sit an ulp below a stage edge.
        ASSERT_NEAR(stage, (t + 1) * 10.0 / 150.0, 1.0) << "tick " << t;
        ASSERT_GE(stage, previous) << "tick " << t;
        previous = stage;
    }
    EXPECT_GE(m_session.crackStage(), 4);
    EXPECT_LE(m_session.crackStage(), 5);
    ASSERT_TRUE(m_session.miningTarget().has_value());
    EXPECT_EQ(*m_session.miningTarget(), kRock);

    (void)tickIdle();
    EXPECT_EQ(m_session.crackStage(), -1);
    EXPECT_FALSE(m_session.miningTarget().has_value());
    EXPECT_EQ(m_world->getBlock(kRock), BlockId::Stone);
}

TEST_F(GameplayMining, InstantBlocksBreakOnTheClickTick) {
    // Beta flowers, torches, saplings and reeds have hardness 0: broken by the click itself.
    ASSERT_TRUE(m_world->setBlock(kRock, BlockId::Dandelion));
    ASSERT_TRUE(gameplay::computeDig(BlockId::Dandelion, {}).instant);
    const MineResult r = mine(kRock);
    ASSERT_TRUE(r.broken.has_value());
    EXPECT_EQ(r.ticks, 1);
    EXPECT_TRUE(r.broken->dropped);
    EXPECT_EQ(m_session.entities().get<ecs::ItemDropComp>(r.broken->drop).id, BlockId::Dandelion);
}

TEST_F(GameplayMining, BedrockNeverBreaks) {
    ASSERT_TRUE(m_world->setBlock(kRock, BlockId::Bedrock));
    hold({items::DiamondPickaxe, 1});
    const MineResult r = mine(kRock, 400);
    EXPECT_FALSE(r.broken.has_value());
    EXPECT_EQ(m_world->getBlock(kRock), BlockId::Bedrock);
    EXPECT_EQ(m_player.getHeldItem().damage, 0);
}
