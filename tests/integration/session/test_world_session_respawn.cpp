// GL-free WorldSession::respawn (spec #9, ticket #13): the death screen's Respawn, next to
// the bed slept in or back to the world spawn. Equivalent of the Application e2e test
// DeathScreenThenRespawnAtSpawn, plus the bed, the missing bed and a world saved while dead.

#include "tests/integration/support/WorldSessionSupport.hpp"

#include "game/EntitySimulation.hpp"
#include "game/ItemUse.hpp"
#include "gameplay/Health.hpp"
#include "gameplay/ItemIds.hpp"
#include "gameplay/Survival.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>

namespace betacraft::test::worldsession {
namespace {

using game::WorldSession;

class WorldSessionRespawn : public WorldSessionSupport {
protected:
    // Killed by the void, inventory scattered: what the Application does on playerDied.
    void die() {
        game::GameTickEvents events{};
        (void)m_session->simulation().damagePlayer(
            player(), {.amount = 1000, .cause = game::DamageCause::Void, .bypassArmor = true}, events);
        ASSERT_TRUE(player().isDead());
        (void)m_session->simulation().dropInventoryOnDeath(player());
    }

    // S5.8 a bed on a stone floor (beds and respawn spots need normal cubes under them), foot
    // at (5, kY, 2) and head at (6, kY, 2), slept in through the night: the bed spawn.
    void sleepInABed() {
        for (int32_t z = kBedZ - 3; z <= kBedZ + 3; ++z) {
            for (int32_t x = kBedX - 2; x <= kBedX + 4; ++x) {
                ASSERT_TRUE(world().setBlock(x, kFloorY, z, BlockId::Stone));
            }
        }
        player().setInventorySlot(player().getSelectedSlot(), {gameplay::items::Bed, 1});
        physics::RaycastHit floor{};
        floor.hit = true;
        floor.blockPos = {kBedX, kFloorY, kBedZ};
        floor.adjacentPos = {kBedX, kY, kBedZ};
        floor.face = core::FaceDir::PosY;
        floor.blockId = BlockId::Stone;
        floor.distance = 2.5f;
        const std::optional<game::ItemUseResult> used = game::useHeldItemOnBlock(world(), player(), floor);
        ASSERT_TRUE(used.has_value());
        ASSERT_EQ(used->kind, game::ItemUseKind::BedPlaced);
        ASSERT_EQ(game::bedHeadAt(world(), m_bedHead), std::optional<core::BlockPos>{m_bedHead});
        m_session->wakeInBed(m_bedHead);
        ASSERT_EQ(m_session->bedSpawn(), std::optional<core::BlockPos>{m_bedHead});
    }

    // Next to the bed: on the stone floor, out of the bed's cells.
    void expectNextToTheBed(const glm::vec3& feet) {
        EXPECT_NEAR(feet.y, static_cast<float>(kY), 0.02f);
        EXPECT_LE(std::abs(feet.x - (m_bedHead.x + 0.5f)), 2.0f);
        EXPECT_LE(std::abs(feet.z - (m_bedHead.z + 0.5f)), 1.0f);
        const core::BlockPos cell{static_cast<int32_t>(std::floor(feet.x)), kY,
                                  static_cast<int32_t>(std::floor(feet.z))};
        EXPECT_EQ(world().getBlock(cell), BlockId::Air);
    }

    static constexpr int32_t kBedX = 5;
    static constexpr int32_t kBedZ = 2;
    const core::BlockPos m_bedFoot{kBedX, kY, kBedZ};
    const core::BlockPos m_bedHead{kBedX + 1, kY, kBedZ};
};

// S1.6: dying scatters the inventory; Respawn brings the player back to the spawn point with
// full health, air and no fire, standing on the first free cell, the inventory still empty and
// the mode and selected slot untouched.
TEST_F(WorldSessionRespawn, DeathThenRespawnAtSpawn) {
    player().setInventorySlot(0, {BlockId::Torch, 10});
    player().setSelectedSlot(4);
    player().breath().air = 37;
    player().burning().fireTicks = 120;
    const size_t itemsBefore = itemEntityCount();
    die();
    EXPECT_EQ(player().countItem(BlockId::Torch), 0);
    EXPECT_EQ(itemEntityCount(), itemsBefore + 1);

    // The spawn point sits inside the glass floor: the player stands on top of it.
    const glm::vec3 spawn{10.5f, static_cast<float>(kFloorY) + 0.5f, 10.5f};
    m_session->setSpawnPoint(spawn);
    const game::RespawnResult result = m_session->respawn();
    EXPECT_FALSE(result.bedMissing);
    EXPECT_FALSE(player().isDead());
    EXPECT_EQ(player().getHealth(), 20);
    EXPECT_EQ(player().health().invulnerableTicks, 0);
    EXPECT_EQ(player().breath().air, gameplay::kMaxAirTicks);
    EXPECT_EQ(player().burning().fireTicks, 0);
    EXPECT_EQ(m_session->spawnPoint(), spawn) << "the world spawn is kept";
    const glm::vec3 feet = player().getPosition();
    EXPECT_FLOAT_EQ(feet.x, 10.5f);
    EXPECT_FLOAT_EQ(feet.z, 10.5f);
    EXPECT_NEAR(feet.y, static_cast<float>(kY), 0.02f);
    EXPECT_EQ(player().getInterpolatedPosition(0.0f), feet) << "no interpolation from the death place";
    EXPECT_EQ(player().countItem(BlockId::Torch), 0) << "the inventory stays empty";
    EXPECT_FALSE(player().isCreative());
    EXPECT_EQ(player().getSelectedSlot(), 4);

    // Alive again: the survival rules apply and the player stands on the floor.
    for (int i = 0; i < 30; ++i) {
        (void)player().fixedUpdate(1.0f / 60.0f, {}, world());
    }
    EXPECT_TRUE(player().isOnGround());
    game::GameTickEvents events{};
    EXPECT_EQ(m_session->simulation().damagePlayer(player(), {.amount = 3, .cause = game::DamageCause::Fall}, events).applied,
              3);
}

// S5.8: the player comes back next to the bed slept in, which stays the spawn.
TEST_F(WorldSessionRespawn, RespawnNextToTheBed) {
    sleepInABed();
    placePlayer({12.5f, static_cast<float>(kY), 12.5f});
    die();

    const game::RespawnResult result = m_session->respawn();
    EXPECT_FALSE(result.bedMissing);
    EXPECT_EQ(m_session->bedSpawn(), std::optional<core::BlockPos>{m_bedHead}) << "the bed stays the spawn";
    EXPECT_FALSE(player().isDead());
    EXPECT_EQ(player().getHealth(), 20);
    expectNextToTheBed(player().getPosition());
}

// S5.8: a bed broken since the night is forgotten; the player is back at the world spawn
// (World::findSafeSpawnPosition), which becomes the spawn point, and Application is told.
TEST_F(WorldSessionRespawn, AMissingBedFallsBackToTheWorldSpawn) {
    sleepInABed();
    ASSERT_TRUE(world().setBlock(m_bedHead, BlockId::Air));
    ASSERT_EQ(world().getBlock(m_bedFoot), BlockId::Air) << "the foot went with the head";
    die();

    const game::RespawnResult result = m_session->respawn();
    EXPECT_TRUE(result.bedMissing);
    EXPECT_FALSE(m_session->bedSpawn().has_value()) << "the bed spawn is forgotten";
    const glm::vec3 worldSpawn = world().findSafeSpawnPosition();
    EXPECT_EQ(m_session->spawnPoint(), worldSpawn);
    EXPECT_FALSE(player().isDead());
    EXPECT_FLOAT_EQ(player().getPosition().x, worldSpawn.x);
    EXPECT_FLOAT_EQ(player().getPosition().z, worldSpawn.z);
    EXPECT_GE(player().getPosition().y, worldSpawn.y - 0.01f);

    // The next death goes to the world spawn without a word about the bed.
    die();
    EXPECT_FALSE(m_session->respawn().bedMissing);
    EXPECT_EQ(m_session->spawnPoint(), worldSpawn);
}

// S1.6: a world saved while dead (death screen, then "Title menu") reopens with the player
// dead, so the Application shows the death screen; Respawn then uses the saved bed.
TEST_F(WorldSessionRespawn, AWorldSavedWhileDeadReopensDeadThenRespawns) {
    sleepInABed();
    placePlayer({12.5f, static_cast<float>(kY), 12.5f});
    player().setSelectedSlot(3);
    die();
    ASSERT_TRUE(m_session->close());
    m_session.reset();

    m_session = WorldSession::open(openOptions(m_dir.path()));
    ASSERT_NE(m_session, nullptr);
    EXPECT_EQ(m_session->levelStatus(), storage::LevelLoadStatus::Ok);
    EXPECT_TRUE(player().isDead());
    EXPECT_EQ(m_session->bedSpawn(), std::optional<core::BlockPos>{m_bedHead});

    EXPECT_FALSE(m_session->respawn().bedMissing);
    EXPECT_FALSE(player().isDead());
    EXPECT_EQ(player().getHealth(), 20);
    expectNextToTheBed(player().getPosition());
    EXPECT_EQ(player().getSelectedSlot(), 3);
    EXPECT_FALSE(player().isCreative());
    for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
        EXPECT_TRUE(player().getInventorySlot(i).isEmpty()) << i;
    }
}

} // namespace
} // namespace betacraft::test::worldsession
