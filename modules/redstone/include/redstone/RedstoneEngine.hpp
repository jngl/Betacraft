#pragma once

#include "core/Types.hpp"
#include "redstone/RedstoneVoxelAccess.hpp"
#include "redstone/TorchBurnoutTracker.hpp"
#include "voxel/Block.hpp"
#include "voxel/ScheduledTicks.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace betacraft::redstone {

// What one tick of the engine did.
struct RedstoneTickStats {
    uint32_t evaluations{0};   // Wire recomputations and consumer checks (<= the budget)
    size_t pending{0};         // Work carried over to the next tick
    uint32_t torchToggles{0};
    uint32_t burnouts{0};
    bool budgetExhausted{false};
};

// Side effects for whoever runs the game tick (sounds, smoke).
enum class RedstoneEventKind : uint8_t {
    Click,        // A lever switched, a button or a plate went down or up (Beta random.click)
    DoorOpen,     // A door opened or closed by power
    DoorClose,
    PistonPush,   // A powered piston pushed
    TorchBurnout  // A torch burnt out (Beta random.fizz + smoke)
};

struct RedstoneEvent {
    core::BlockPos pos{};
    RedstoneEventKind kind{RedstoneEventKind::Click};
};

// Blocks the engine evaluates or schedules (sources, wire, torches and the consumers).
[[nodiscard]] constexpr bool isRedstoneComponent(voxel::BlockId id) noexcept {
    using voxel::BlockId;
    switch (id) {
        case BlockId::RedstoneWire:
        case BlockId::Lever:
        case BlockId::StonePressurePlate:
        case BlockId::WoodPressurePlate:
        case BlockId::StoneButton:
        case BlockId::RedstoneTorchOff:
        case BlockId::RedstoneTorchOn:
        case BlockId::WoodDoor:
        case BlockId::IronDoor:
        case BlockId::Piston:
            return true;
        default:
            return false;
    }
}

// Blocks whose scheduled updates belong to the engine.
[[nodiscard]] constexpr bool hasRedstoneScheduledTick(voxel::BlockId id) noexcept {
    using voxel::BlockId;
    return id == BlockId::RedstoneTorchOff || id == BlockId::RedstoneTorchOn || id == BlockId::StoneButton ||
           id == BlockId::StonePressurePlate || id == BlockId::WoodPressurePlate;
}

// Redstone over an IRedstoneVoxelAccess, Beta 1.7.3 rules:
//  * sources: lever (on), button (pressed), pressure plates (pressed) and lit torches. They
//    weakly power every neighbour (a torch not the block it hangs on) and strongly power one
//    block: the lever's and button's support, the block under a plate, the block over a torch;
//  * a normal cube strongly powered (by a source, or by wire pointing into it or lying on it)
//    powers its neighbours;
//  * wire takes 15 next to a source or a strongly powered block (wire alone does not count),
//    otherwise the strongest connected wire minus one (same level, one up, one down), and
//    powers the block under it and, as a line, the block it points to (a lone dot: all four);
//  * torches switch 2 ticks after their support's power changes, burning out after more than
//    8 switches in 60 ticks (TorchBurnoutTracker);
//  * a button is released and an unloaded plate goes up after 20 ticks;
//  * consumers: a door opens while either half is powered (only redstone moves an iron door;
//    a wooden door opened by hand keeps its state until its power changes), a piston pushes
//    on the rising edge of its power (kPistonPoweredBit keeps the edge across saves).
//
// Wire networks update breadth-first in two phases without recursion: the wires a change
// darkened are switched off, then the network is relit from what still feeds it. The new
// powers are kept aside and written once a network has settled. tick() spends at most
// maxEvaluationsPerTick evaluations and carries the rest over.
//
// Delayed updates (torches, buttons, plates) go to a voxel::ScheduledTickQueue: the World's
// own queue when one is shared (its owner then calls runScheduledTick for the due entries of
// redstone blocks), otherwise one the engine drains itself in tick().
class RedstoneEngine {
public:
    static constexpr uint32_t kMaxEvaluationsPerTick = 1024;
    static constexpr uint32_t kTorchDelayTicks = 2;
    static constexpr uint32_t kButtonTicks = 20;
    static constexpr uint32_t kPlateTicks = 20;
    static constexpr uint8_t kMaxPower = 15;

    explicit RedstoneEngine(IRedstoneVoxelAccess& access, voxel::ScheduledTickQueue* sharedQueue = nullptr,
                            uint32_t maxEvaluationsPerTick = kMaxEvaluationsPerTick);

    RedstoneEngine(const RedstoneEngine&) = delete;
    RedstoneEngine& operator=(const RedstoneEngine&) = delete;

    // Something changed at `pos` (block, metadata or a neighbour's shape): the wires and
    // consumers around it are re-evaluated on the next tick(). Ignored while the engine
    // writes its own results.
    void onBlockChanged(core::BlockPos pos);
    // The redstone block at `pos` comes back from disk with no pending update: torches and
    // consumers are checked, a pressed button or plate is scheduled to come up again.
    void onBlockLoaded(core::BlockPos pos);
    // Runs the update of `pos` after `delayTicks` ticks (ScheduledTickQueue, one per cell).
    void schedule(core::BlockPos pos, uint32_t delayTicks);
    // The scheduled update of `pos` is due (shared-queue mode; tick() calls it otherwise).
    void runScheduledTick(core::BlockPos pos);
    // The current game tick, for updates scheduled before the next tick() (shared queue).
    void setTime(uint64_t worldTick) noexcept { m_now = worldTick; }
    [[nodiscard]] uint64_t time() const noexcept { return m_now; }

    // One game tick: due scheduled updates (own queue), then at most the budget of
    // evaluations.
    RedstoneTickStats tick(uint64_t worldTick);

    // Power at `pos`: a wire's level, otherwise 15 when the cell is powered (directly by a
    // neighbouring source or through a strongly powered block), 0 when it is not.
    [[nodiscard]] uint8_t powerAt(core::BlockPos pos) const;
    // Beta World.isBlockIndirectlyGettingPowered.
    [[nodiscard]] bool isPowered(core::BlockPos pos) const;

    // Player interactions (right click, contact). Each returns whether anything changed.
    bool toggleLever(core::BlockPos pos);
    bool pressButton(core::BlockPos pos);
    // Someone stands on the plate at `pos` this tick (a stone plate only for living things:
    // the caller filters). It goes down and stays down while contacts keep coming.
    bool reportPlateContact(core::BlockPos pos);

    [[nodiscard]] bool isBurntOut(core::BlockPos pos) const { return m_burnout.isBurntOut(pos, m_now); }
    [[nodiscard]] const TorchBurnoutTracker& burnout() const noexcept { return m_burnout; }
    [[nodiscard]] std::vector<RedstoneEvent> takeEvents() noexcept;
    // Work still queued (nothing pending means the circuits are settled until the next
    // change or scheduled update).
    [[nodiscard]] size_t pendingWork() const noexcept;
    [[nodiscard]] bool idle() const noexcept { return pendingWork() == 0; }
    [[nodiscard]] const voxel::ScheduledTickQueue& scheduledTicks() const noexcept { return *m_queue; }
    // Forgets queued work, overlay and burnout history (the World's seed or save changed).
    void clear() noexcept;

private:
    struct Removal {
        core::BlockPos pos{};
        uint8_t oldPower{0};
    };

    [[nodiscard]] voxel::BlockId block(const core::BlockPos& p) const noexcept { return m_access.getBlock(p); }
    [[nodiscard]] uint8_t meta(const core::BlockPos& p) const noexcept { return m_access.getMetadata(p); }
    [[nodiscard]] bool normalCube(const core::BlockPos& p) const noexcept { return m_access.isNormalCube(p); }

    // Wire.
    [[nodiscard]] uint8_t wirePower(const core::BlockPos& p) const;
    void setWirePower(const core::BlockPos& p, uint8_t power);
    [[nodiscard]] bool isWire(const core::BlockPos& p) const noexcept {
        return block(p) == voxel::BlockId::RedstoneWire;
    }
    // The wire cells `p` exchanges power with (4 sides, one up, one down); returns the count.
    size_t connectedWires(const core::BlockPos& p, std::array<core::BlockPos, 4>& out) const;
    // Beta isPowerProviderOrWire towards horizontal direction `dir` of the wire at `p`.
    [[nodiscard]] bool connectsTowards(const core::BlockPos& p, const glm::ivec3& dir) const;
    [[nodiscard]] bool wirePowersTowards(const core::BlockPos& wire, const core::BlockPos& target) const;
    // 15 when a non-wire source or a block strongly powered by one feeds the wire at `p`.
    [[nodiscard]] uint8_t directWirePower(const core::BlockPos& p) const;
    [[nodiscard]] uint8_t computeWirePower(const core::BlockPos& p) const;

    // Power rules.
    [[nodiscard]] uint8_t weakPower(const core::BlockPos& src, const core::BlockPos& target, bool wires) const;
    [[nodiscard]] uint8_t strongPower(const core::BlockPos& src, const core::BlockPos& target, bool wires) const;
    [[nodiscard]] bool isStronglyPowered(const core::BlockPos& cube, bool wires) const;
    [[nodiscard]] bool torchPowered(const core::BlockPos& torch) const;

    // Work queues.
    void enqueueCheck(const core::BlockPos& p);
    void seedAround(const core::BlockPos& p);
    void seedWire(const core::BlockPos& p);
    void processRemoval(const Removal& r);
    void processRelight(const core::BlockPos& p);
    void flushOverlay();
    void evaluateConsumer(const core::BlockPos& p);
    void evaluateTorch(const core::BlockPos& p);
    void evaluateDoor(const core::BlockPos& p);
    void evaluatePiston(const core::BlockPos& p);
    void writeMetadata(const core::BlockPos& p, uint8_t value);
    void writeBlock(const core::BlockPos& p, voxel::BlockId id, uint8_t value);

    IRedstoneVoxelAccess& m_access;
    voxel::ScheduledTickQueue m_ownQueue;
    voxel::ScheduledTickQueue* m_queue;
    bool m_sharedQueue;
    uint32_t m_budget;
    uint64_t m_now{0};
    bool m_writing{false};

    std::deque<core::BlockPos> m_dirty;
    std::unordered_set<core::BlockPos> m_dirtySet;
    std::deque<core::BlockPos> m_seeds;
    std::deque<Removal> m_removals;
    std::deque<core::BlockPos> m_relights;
    std::unordered_map<core::BlockPos, uint8_t> m_overlay; // New wire powers, written once settled
    std::deque<core::BlockPos> m_checks;
    std::unordered_set<core::BlockPos> m_checkSet;

    std::unordered_map<core::BlockPos, uint64_t> m_plateContacts; // Last contact tick per plate
    std::unordered_set<core::BlockPos> m_poweredDoors;            // Lower halves held open by power
    TorchBurnoutTracker m_burnout;
    std::vector<RedstoneEvent> m_events;
    std::vector<voxel::ScheduledTickQueue::Entry> m_due; // Reused by tick()
    RedstoneTickStats m_stats{}; // Switches since the last tick() returned
};

} // namespace betacraft::redstone
