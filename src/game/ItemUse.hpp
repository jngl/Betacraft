#pragma once

#include "core/Types.hpp"
#include "gameplay/ItemStack.hpp"
#include "gameplay/SplitMix64.hpp"
#include "physics/Player.hpp"
#include "physics/Raycaster.hpp"
#include "world/Block.hpp"

#include <cstdint>
#include <optional>

namespace betacraft::world { class World; }

namespace betacraft::game {

// S5.6 step 3 of the right-click order (conventions §2): the held item used on the block
// under the crosshair, before the held block is placed (placeHeldBlock).
enum class ItemUseKind : uint8_t {
    DoorPlaced,   // Beta ItemDoor
    BedPlaced,    // Beta ItemBed
    SoilTilled,   // Beta ItemHoe
    SeedsPlanted, // Beta ItemSeeds
    BucketFilled, // S6.4 Beta ItemBucket: an empty bucket took a liquid source (block = the liquid)
    BucketEmptied, // S6.4 a full bucket poured its liquid (block = the liquid placed)
    WirePlaced     // S8.5 Beta ItemRedstone: redstone dust laid as wire
};

// S6.4 Beta ItemBucket.onItemRightClick reach (blocks from the eyes).
inline constexpr float kBucketReach = 5.0f;

struct ItemUseResult {
    ItemUseKind kind{ItemUseKind::DoorPlaced};
    core::BlockPos pos{};                        // The changed cell (lower door half, bed foot, farmland, crop)
    world::BlockId block{world::BlockId::Air};   // What it became
    gameplay::WearResult wear{gameplay::WearResult::Unchanged}; // Hoe wear
};

// Uses the held item on `target` (Beta Item.onItemUse):
//  * wooden door (324) or iron door (330, S8.5) on a top face: two replaceable cells over a
//    normal cube, orientation
//    from the look, the hinge mirrored towards a neighbouring door or the wall side;
//  * bed (355) on a top face: foot there, head one cell along the look, both over normal
//    cubes and both cells air;
//  * hoe on grass or dirt with air above (not from below): farmland, 1 point of wear;
//  * seeds (295) on the top of farmland with air above: crops at stage 0;
//  * redstone (331, S8.5) on any face: wire in the replaceable cell in front of it, over a
//    normal cube (Beta ItemRedstone / BlockRedstoneWire.canPlaceBlockAt).
// Consumes the item (the hoe wears instead) and returns what happened; nullopt when the held
// item has no use there, so the caller goes on with placeHeldBlock.
//  * buckets: useHeldBucket (their own ray, `target` unused).
std::optional<ItemUseResult> useHeldItemOnBlock(world::World& world, physics::Player& player,
                                                const physics::RaycastHit& target);

// S6.4 buckets (325, 326, 327), Beta ItemBucket.onItemRightClick, from step 3 (through
// useHeldItemOnBlock) or step 4 (nothing under the crosshair) of the right-click order. The
// bucket casts its own kBucketReach ray from the eyes:
//  * empty: RaycastMode::LiquidSources; a water or lava source there turns to air and the
//    bucket into a water or lava bucket;
//  * water or lava bucket: RaycastMode::Solid; the cell in front of the hit face, when
//    replaceable, gets a source (setBlockWithMetadata(..., 0), which wakes the fluid
//    simulation) and the bucket is empty again.
// nullopt when the held item is no bucket or nothing changed.
std::optional<ItemUseResult> useHeldBucket(world::World& world, physics::Player& player);

// Beta BlockDoor.blockActivated (step 2): flips the open bit of both halves of the wooden
// door at `pos` (either half). Returns the new open state, nullopt when `pos` holds no wooden
// door (an iron door only answers to redstone, S8.7).
std::optional<bool> toggleDoor(world::World& world, const core::BlockPos& pos);

// Beta door orientation (metadata bits 0-1) for a player looking along (lookX, lookZ):
// 0 = +X, 1 = +Z, 2 = -X, 3 = -Z (ItemDoor: floor((yaw + 180) * 4 / 360 - 0.5) & 3).
[[nodiscard]] uint8_t doorFacingForLook(float lookX, float lookZ) noexcept;
// Beta bed orientation for a look direction (ItemBed: floor(yaw * 4 / 360 + 0.5) & 3), so
// that the head lies ahead of the foot: 0 = +Z, 1 = -X, 2 = -Z, 3 = +X.
[[nodiscard]] uint8_t bedFacingForLook(float lookX, float lookZ) noexcept;

// S5.7 Beta BlockFarmland.onFallenUpon: a landing (StepEvent::landedFallDistance) on
// farmland turns it back to dirt when rand < fall - 0.5. Returns whether it did.
bool trampleFarmland(world::World& world, const physics::StepEvent& step, gameplay::SplitMix64& rng);

} // namespace betacraft::game
