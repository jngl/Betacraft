#include "game/ItemUse.hpp"

#include "game/ItemBridge.hpp"
#include "gameplay/Crops.hpp"
#include "gameplay/ItemIds.hpp"
#include "math/AABB.hpp"
#include "world/BlockShapes.hpp"
#include "world/World.hpp"

#include <cmath>

namespace betacraft::game {

namespace {

using world::BlockId;

[[nodiscard]] bool isAir(const world::World& world, const core::BlockPos& p) {
    return world.getBlock(p) == BlockId::Air;
}

[[nodiscard]] bool isReplaceable(const world::World& world, const core::BlockPos& p) {
    return world::BlockRegistry::get(world.getBlock(p)).replaceable;
}

[[nodiscard]] bool isNormalCubeAt(const world::World& world, int32_t x, int32_t y, int32_t z) {
    return world::isNormalCube(world.getBlock(x, y, z));
}

// The player must not end up inside the new block (Beta only checks this for ItemBlock; kept
// here so a door or a bed never traps the player).
[[nodiscard]] bool overlapsPlayer(const physics::Player& player, const core::BlockPos& p, BlockId id, uint8_t meta) {
    return player.getAABB().intersects(math::AABB::fromBlock(p.x, p.y, p.z, id, meta));
}

// Beta ItemDoor.onItemUse (`door`: WoodDoor or IronDoor).
std::optional<ItemUseResult> placeDoor(world::World& world, physics::Player& player, const physics::RaycastHit& target,
                                       BlockId door) {
    if (target.face != core::FaceDir::PosY) {
        return std::nullopt;
    }
    const core::BlockPos lower = target.adjacentPos;
    const core::BlockPos upper = lower.offset(0, 1, 0);
    if (lower.y < 1 || upper.y >= world::Chunk::HEIGHT || !isNormalCubeAt(world, lower.x, lower.y - 1, lower.z) ||
        !isReplaceable(world, lower) || !isReplaceable(world, upper)) {
        return std::nullopt;
    }
    const glm::vec3 look = player.getLookDirection();
    uint8_t facing = doorFacingForLook(look.x, look.z);
    // The cells on the door's left (-) and right (+) side, along the door plane.
    int32_t sx = 0;
    int32_t sz = 0;
    switch (facing) {
        case 0: sz = 1; break;
        case 1: sx = -1; break;
        case 2: sz = -1; break;
        default: sx = 1; break;
    }
    const auto solidCount = [&](int32_t dx, int32_t dz) {
        return (isNormalCubeAt(world, lower.x + dx, lower.y, lower.z + dz) ? 1 : 0) +
               (isNormalCubeAt(world, lower.x + dx, lower.y + 1, lower.z + dz) ? 1 : 0);
    };
    const auto doorAt = [&](int32_t dx, int32_t dz) {
        return world::isDoorBlock(world.getBlock(lower.x + dx, lower.y, lower.z + dz)) ||
               world::isDoorBlock(world.getBlock(lower.x + dx, lower.y + 1, lower.z + dz));
    };
    const int32_t leftSolid = solidCount(-sx, -sz);
    const int32_t rightSolid = solidCount(sx, sz);
    const bool leftDoor = doorAt(-sx, -sz);
    const bool rightDoor = doorAt(sx, sz);
    // Beta: the hinge goes to the other side next to a door (double doors) or when the
    // right side has more wall; it is stored as the turned orientation plus the open bit.
    if ((leftDoor && !rightDoor) || rightSolid > leftSolid) {
        facing = static_cast<uint8_t>(((facing + 3) & world::kDoorOrientationMask) | world::kDoorOpenBit);
    }
    if (overlapsPlayer(player, lower, door, facing) ||
        overlapsPlayer(player, upper, door, static_cast<uint8_t>(facing | world::kDoorUpperBit))) {
        return std::nullopt;
    }
    if (!world.setBlockWithMetadata(lower.x, lower.y, lower.z, door, facing)) {
        return std::nullopt;
    }
    (void)world.setBlockWithMetadata(upper.x, upper.y, upper.z, door, static_cast<uint8_t>(facing | world::kDoorUpperBit));
    (void)player.consumeHeldItem(1);
    return ItemUseResult{ItemUseKind::DoorPlaced, lower, door};
}

// Beta ItemRedstone.onItemUse.
std::optional<ItemUseResult> placeWire(world::World& world, physics::Player& player, const physics::RaycastHit& target) {
    const core::BlockPos cell = target.adjacentPos;
    if (cell.y < 1 || cell.y >= world::Chunk::HEIGHT || !isReplaceable(world, cell) ||
        world::BlockRegistry::isLiquid(world.getBlock(cell)) || !isNormalCubeAt(world, cell.x, cell.y - 1, cell.z)) {
        return std::nullopt;
    }
    if (!world.setBlockWithMetadata(cell.x, cell.y, cell.z, BlockId::RedstoneWire, 0)) {
        return std::nullopt;
    }
    (void)player.consumeHeldItem(1);
    return ItemUseResult{ItemUseKind::WirePlaced, cell, BlockId::RedstoneWire};
}

// Beta ItemBed.onItemUse.
std::optional<ItemUseResult> placeBed(world::World& world, physics::Player& player, const physics::RaycastHit& target) {
    if (target.face != core::FaceDir::PosY) {
        return std::nullopt;
    }
    const glm::vec3 look = player.getLookDirection();
    const uint8_t facing = bedFacingForLook(look.x, look.z);
    const glm::ivec3 d = world::bedHeadDirection(facing);
    const core::BlockPos foot = target.adjacentPos;
    const core::BlockPos head = foot.offset(d.x, d.y, d.z);
    if (foot.y < 1 || foot.y >= world::Chunk::HEIGHT || !isAir(world, foot) || !isAir(world, head) ||
        !isNormalCubeAt(world, foot.x, foot.y - 1, foot.z) || !isNormalCubeAt(world, head.x, head.y - 1, head.z)) {
        return std::nullopt;
    }
    const auto headMeta = static_cast<uint8_t>(facing | world::kBedHeadBit);
    if (overlapsPlayer(player, foot, BlockId::Bed, facing) || overlapsPlayer(player, head, BlockId::Bed, headMeta)) {
        return std::nullopt;
    }
    if (!world.setBlockWithMetadata(foot.x, foot.y, foot.z, BlockId::Bed, facing)) {
        return std::nullopt;
    }
    (void)world.setBlockWithMetadata(head.x, head.y, head.z, BlockId::Bed, headMeta);
    (void)player.consumeHeldItem(1);
    return ItemUseResult{ItemUseKind::BedPlaced, foot, BlockId::Bed};
}

// Beta ItemHoe.onItemUse.
std::optional<ItemUseResult> tillSoil(world::World& world, physics::Player& player, const physics::RaycastHit& target) {
    const core::BlockPos pos = target.blockPos;
    const BlockId block = world.getBlock(pos);
    if (target.face == core::FaceDir::NegY || (block != BlockId::Grass && block != BlockId::Dirt) ||
        !isAir(world, pos.offset(0, 1, 0))) {
        return std::nullopt;
    }
    if (!world.setBlockWithMetadata(pos.x, pos.y, pos.z, BlockId::Farmland, 0)) {
        return std::nullopt;
    }
    gameplay::ItemStack held = toItemStack(player.getHeldItem());
    const gameplay::WearResult wear = gameplay::onSoilTilled(held);
    player.setHeldItem(toInventorySlot(held));
    return ItemUseResult{ItemUseKind::SoilTilled, pos, BlockId::Farmland, wear};
}

// Beta ItemSeeds.onItemUse.
std::optional<ItemUseResult> plantSeeds(world::World& world, physics::Player& player, const physics::RaycastHit& target) {
    const core::BlockPos soil = target.blockPos;
    const core::BlockPos crop = soil.offset(0, 1, 0);
    if (target.face != core::FaceDir::PosY || world.getBlock(soil) != BlockId::Farmland || !isAir(world, crop)) {
        return std::nullopt;
    }
    if (!world.setBlockWithMetadata(crop.x, crop.y, crop.z, BlockId::Crops, 0)) {
        return std::nullopt;
    }
    (void)player.consumeHeldItem(1);
    return ItemUseResult{ItemUseKind::SeedsPlanted, crop, BlockId::Crops};
}

// The held bucket becomes `bucket` (a stack of one; any extra empty buckets go to the bag).
void swapHeldBucket(physics::Player& player, gameplay::ItemId bucket) {
    const physics::InventorySlot held = player.getHeldItem();
    if (held.count <= 1) {
        player.setHeldItem(physics::InventorySlot{bucket, 1});
        return;
    }
    (void)player.consumeHeldItem(1);
    (void)player.insertStack({bucket, 1});
}

} // namespace

std::optional<ItemUseResult> useHeldBucket(world::World& world, physics::Player& player) {
    const physics::InventorySlot held = player.getHeldItem();
    if (held.isEmpty() || player.isDead()) {
        return std::nullopt;
    }
    const gameplay::ItemId id = gameplay::canonicalize(held.id);
    if (id != gameplay::items::Bucket && id != gameplay::items::WaterBucket && id != gameplay::items::LavaBucket) {
        return std::nullopt;
    }
    const bool empty = id == gameplay::items::Bucket;
    const physics::RaycastHit hit =
        physics::Raycaster::cast(world, player.getEyePosition(), player.getLookDirection(), kBucketReach,
                                 empty ? physics::RaycastMode::LiquidSources : physics::RaycastMode::Solid);
    if (!hit.hit) {
        return std::nullopt;
    }
    if (empty) {
        const BlockId liquid = world.getBlock(hit.blockPos);
        if (!world::BlockRegistry::isLiquid(liquid) || world.getMetadata(hit.blockPos) != 0) {
            return std::nullopt;
        }
        if (!world.setBlockWithMetadata(hit.blockPos.x, hit.blockPos.y, hit.blockPos.z, BlockId::Air, 0)) {
            return std::nullopt;
        }
        const bool water = world::isWaterBlock(liquid);
        swapHeldBucket(player, water ? gameplay::items::WaterBucket : gameplay::items::LavaBucket);
        return ItemUseResult{ItemUseKind::BucketFilled, hit.blockPos, water ? BlockId::Water : BlockId::Lava};
    }
    const core::BlockPos cell = hit.adjacentPos;
    if (cell.y < 0 || cell.y >= world::Chunk::HEIGHT || !isReplaceable(world, cell)) {
        return std::nullopt;
    }
    const BlockId liquid = id == gameplay::items::WaterBucket ? BlockId::Water : BlockId::Lava;
    // Flowing liquid of the same id becomes a source through the metadata; an existing
    // source of it changes nothing and keeps the bucket full.
    if (!world.setBlockWithMetadata(cell.x, cell.y, cell.z, liquid, 0)) {
        return std::nullopt;
    }
    swapHeldBucket(player, gameplay::items::Bucket);
    return ItemUseResult{ItemUseKind::BucketEmptied, cell, liquid};
}

std::optional<ItemUseResult> useHeldItemOnBlock(world::World& world, physics::Player& player,
                                                const physics::RaycastHit& target) {
    const physics::InventorySlot held = player.getHeldItem();
    if (!target.hit || held.isEmpty() || player.isDead()) {
        return std::nullopt;
    }
    const gameplay::ItemId id = gameplay::canonicalize(held.id);
    if (id == gameplay::items::Bucket || id == gameplay::items::WaterBucket || id == gameplay::items::LavaBucket) {
        return useHeldBucket(world, player);
    }
    if (id == gameplay::items::WoodenDoor) {
        return placeDoor(world, player, target, BlockId::WoodDoor);
    }
    if (id == gameplay::items::IronDoor) {
        return placeDoor(world, player, target, BlockId::IronDoor);
    }
    if (id == gameplay::items::Redstone) {
        return placeWire(world, player, target);
    }
    if (id == gameplay::items::Bed) {
        return placeBed(world, player, target);
    }
    if (id == gameplay::items::Seeds) {
        return plantSeeds(world, player, target);
    }
    if (const auto tool = gameplay::toolInfo(id); tool && tool->kind == gameplay::ToolKind::Hoe) {
        return tillSoil(world, player, target);
    }
    return std::nullopt;
}

std::optional<bool> toggleDoor(world::World& world, const core::BlockPos& pos) {
    if (world.getBlock(pos) != BlockId::WoodDoor) {
        return std::nullopt;
    }
    // Beta: the upper half forwards the click to the lower one, which holds the state.
    core::BlockPos lower = pos;
    if ((world.getMetadata(pos) & world::kDoorUpperBit) != 0) {
        lower = pos.offset(0, -1, 0);
        if (world.getBlock(lower) != BlockId::WoodDoor) {
            return std::nullopt;
        }
    }
    const auto meta = static_cast<uint8_t>((world.getMetadata(lower) ^ world::kDoorOpenBit) & ~world::kDoorUpperBit);
    (void)world.setMetadata(lower.x, lower.y, lower.z, meta);
    const core::BlockPos upper = lower.offset(0, 1, 0);
    if (world.getBlock(upper) == BlockId::WoodDoor) {
        (void)world.setMetadata(upper.x, upper.y, upper.z, static_cast<uint8_t>(meta | world::kDoorUpperBit));
    }
    return (meta & world::kDoorOpenBit) != 0;
}

uint8_t doorFacingForLook(float lookX, float lookZ) noexcept {
    if (std::abs(lookX) >= std::abs(lookZ)) {
        return lookX >= 0.0f ? uint8_t{0} : uint8_t{2};
    }
    return lookZ >= 0.0f ? uint8_t{1} : uint8_t{3};
}

uint8_t bedFacingForLook(float lookX, float lookZ) noexcept {
    if (std::abs(lookX) >= std::abs(lookZ)) {
        return lookX >= 0.0f ? uint8_t{3} : uint8_t{1};
    }
    return lookZ >= 0.0f ? uint8_t{0} : uint8_t{2};
}

bool trampleFarmland(world::World& world, const physics::StepEvent& step, gameplay::SplitMix64& rng) {
    if (step.landedFallDistance <= 0.0f || world.getBlock(step.landedOn) != BlockId::Farmland) {
        return false;
    }
    if (rng.nextFloat() >= step.landedFallDistance - gameplay::kFarmlandTrampleOffset) {
        return false;
    }
    const core::BlockPos p = step.landedOn;
    return world.setBlockWithMetadata(p.x, p.y, p.z, BlockId::Dirt, 0);
}

} // namespace betacraft::game
