#pragma once

#include "fluids/ActiveFluidQueue.hpp"
#include "fluids/FluidVoxelAccess.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace betacraft::fluids {

struct FluidConfig {
    uint32_t waterTickRate{5};      // Beta BlockFluid.tickRate(): water 5 game ticks
    uint32_t lavaTickRate{30};      // lava 30 game ticks (overworld)
    int waterDecayStep{1};          // decay added per horizontal block -> 7 flowing cells
    int lavaDecayStep{2};           // overworld lava -> 3 flowing cells (use 1 for the Nether)
    int maxFlowSearchDepth{4};      // Beta calculateFlowCost() recursion limit, clamped to
                                    // [0, FluidSimulator::kMaxFlowSearchDepth]
    uint32_t maxUpdatesPerTick{1024}; // hard cap on cell updates per tick (frametime budget)
    // Work cap: no new update starts once this many flow-search nodes ran this tick (>= 1;
    // the first update always runs). One open-floor update costs up to 4*(1+3+9+27) = 160
    // nodes of ~20 voxel reads each, so the update count alone does not bound the cost;
    // this keeps a tick to roughly 330k reads plus one update's overshoot.
    uint32_t maxSearchNodesPerTick{16384};
    size_t maxBufferedEvents{4096};   // events beyond this are dropped if nobody drains them
    // Beta updateTick slows lava thinning down: a lava cell that should get thinner only
    // does so when random.nextInt(4) == 0, so lava retracts ~4x slower than its tick rate.
    // Reproduced with a seeded hash of (seed, position, tick) instead of a shared RNG so
    // the result does not depend on update order; 1 disables the slowdown.
    uint32_t lavaThinningOdds{4};
    uint64_t randomSeed{0};           // seed of the deterministic stand-in for Beta's Random
};

// Side effects the game layer reacts to (item drops, fizz sound + smoke particles).
enum class FluidEventKind : uint8_t {
    BlockWashedAway, // `block` was destroyed by flowing *water* (spawn its drop)
    Fizz             // lava mix effect at `pos` (sound + smoke): `block` is what it produced
                     // (obsidian/cobble/stone) or Air when lava boiled water or burnt a
                     // plant/torch, which never drops an item
};

struct FluidEvent {
    FluidEventKind kind{FluidEventKind::Fizz};
    core::BlockPos pos{};
    world::BlockId block{world::BlockId::Air};

    bool operator==(const FluidEvent& other) const noexcept = default;
};

struct FluidTickStats {
    uint32_t processed{0};       // cell updates executed this tick
    bool budgetExhausted{false}; // due work was deferred to a later tick by the budget
    size_t pending{0};           // queued updates after this tick (due now or later)
    uint32_t searchNodes{0};     // flow-search (calculateFlowCost) nodes visited this tick
};

// Beta 1.7.3 BlockFlowing/BlockStationary cellular automaton driven by a scheduled-tick
// queue. Only cells in the active frontier are visited; a cell whose update changes
// nothing simply drops out of the queue (Beta's "stationary" fluid) and is woken again
// by neighbour changes.
class FluidSimulator {
public:
    // Search cost is ~4 * 3^depth nodes per update: 8 already means ~26k nodes, so deeper
    // configurations are clamped to keep one update's cost bounded.
    static constexpr int kMaxFlowSearchDepth = 8;

    explicit FluidSimulator(IFluidVoxelAccess& access, FluidConfig config = {}) noexcept;

    FluidSimulator(const FluidSimulator&) = delete;
    FluidSimulator& operator=(const FluidSimulator&) = delete;

    // Places a fluid source (bucket, world generation) and wakes it and its neighbours.
    void placeSource(const core::BlockPos& pos, FluidType type);

    // Advances one game tick and runs due cell updates, until maxUpdatesPerTick updates or
    // maxSearchNodesPerTick search nodes are used; the rest stays queued, in order.
    FluidTickStats tick();

    // Ticks until no work is pending or maxTicks elapsed; returns the ticks run.
    uint64_t runUntilIdle(uint64_t maxTicks);

    // Removes the fluid at `pos` (bucket pickup, sponge) and wakes the neighbours so the
    // flow it was feeding retracts. No-op on dry, solid or unloaded cells.
    void removeFluid(const core::BlockPos& pos);

    // Must be called by the game whenever a cell changes outside the simulator (block
    // placed/broken, bucket emptied/filled): wakes the cell and its six neighbours.
    void notifyBlockChanged(const core::BlockPos& pos);

    // Must be called when a region (typically a chunk column) becomes loaded. Unloaded
    // cells act as walls, so fluid that stopped at the region's border would otherwise
    // stay frozen: this queues every fluid cell on the region's boundary and in the ring
    // just outside it (O(surface)). Updates queued for cells of the region while it was
    // unloaded were dropped; pass wakeInterior to re-queue all of its fluid cells too
    // (O(volume), e.g. when the chunk was unloaded mid-flow). Inclusive box.
    void notifyRegionLoaded(const core::BlockPos& min, const core::BlockPos& max, bool wakeInterior = false);

    // Queues a fluid cell for its next scheduled update (used when fluid is loaded or
    // generated without going through placeSource). No-op on dry cells.
    void scheduleUpdate(const core::BlockPos& pos);

    // True when updating `pos` now would change nothing: dry cells, and sources with no
    // displaceable cell below or beside them (lava also not touching water). Flowing and
    // falling cells are never settled. Lets a loader queue only the fluid that can move
    // instead of every cell of an ocean.
    [[nodiscard]] bool isSettled(const core::BlockPos& pos) const noexcept;

    // Returns and clears the buffered side-effect events, in emission order.
    [[nodiscard]] std::vector<FluidEvent> takeEvents() noexcept;
    [[nodiscard]] const std::vector<FluidEvent>& events() const noexcept { return m_events; }

    [[nodiscard]] uint64_t currentTick() const noexcept { return m_tick; }
    [[nodiscard]] size_t pendingUpdates() const noexcept { return m_queue.size(); }
    [[nodiscard]] uint32_t tickRateFor(FluidType type) const noexcept;
    [[nodiscard]] const FluidConfig& config() const noexcept { return m_config; }

private:
    // Horizontal order matches Beta (x-1, x+1, z-1, z+1) so that i ^ 1 is the opposite
    // direction and tie-breaking is identical.
    static constexpr std::array<core::BlockPos, 4> kHorizontal{{{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}}};
    static constexpr std::array<core::BlockPos, 6> kNeighbours{
        {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}}};
    static constexpr int kNoDrop = 1000; // Beta's "no hole found" flow cost

    [[nodiscard]] int decayStepFor(FluidType type) const noexcept;
    [[nodiscard]] int decayAt(const core::BlockPos& pos, FluidType type) const noexcept;
    [[nodiscard]] bool cellBlocksFlow(const core::BlockPos& pos) const noexcept;
    [[nodiscard]] bool canDisplace(const core::BlockPos& pos, FluidType type) const noexcept;
    [[nodiscard]] bool touchesWater(const core::BlockPos& pos) const noexcept;
    [[nodiscard]] int recomputeDecay(const core::BlockPos& pos, FluidType type) const noexcept;
    [[nodiscard]] bool lavaThinsNow(const core::BlockPos& pos) const noexcept;

    [[nodiscard]] bool isPathBlocked(const core::BlockPos& pos, FluidType type) const noexcept;
    [[nodiscard]] int flowCost(const core::BlockPos& pos, FluidType type, int accumulated, size_t fromDir);
    [[nodiscard]] std::array<bool, 4> optimalFlowDirections(const core::BlockPos& pos, FluidType type);

    void scheduleFluid(const core::BlockPos& pos);
    void wakeNeighbours(const core::BlockPos& pos, bool hardenLava);
    void writeFluid(const core::BlockPos& pos, FluidState state);
    void writeBlock(const core::BlockPos& pos, world::BlockId id);
    bool tryHarden(const core::BlockPos& pos);
    void emit(FluidEventKind kind, const core::BlockPos& pos, world::BlockId block);

    void updateCell(const core::BlockPos& pos);
    void flowInto(const core::BlockPos& pos, FluidType type, int decay);

    IFluidVoxelAccess& m_access;
    FluidConfig m_config;
    ActiveFluidQueue m_queue;
    std::vector<FluidEvent> m_events;
    uint64_t m_tick{0};
    uint32_t m_searchNodes{0}; // flowCost() calls in the current tick (work budget)
};

} // namespace betacraft::fluids
