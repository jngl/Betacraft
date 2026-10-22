#include "fluids/FluidSimulator.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace betacraft::fluids {

namespace {

// Beta checkForHarden(): flowing lava hardens to cobblestone only while still thick
// (decay <= 4); thinner overworld lava (decay 6) and falling lava are left alone.
constexpr int kMaxHardeningDecay = 4;

// splitmix64 finaliser: cheap, platform-independent, good avalanche for hashing coords.
constexpr uint64_t mix64(uint64_t z) noexcept {
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

} // namespace

FluidSimulator::FluidSimulator(IFluidVoxelAccess& access, FluidConfig config) noexcept
    : m_access(access), m_config(config) {
    // A zero rate would let a cell re-schedule itself into the tick being processed.
    m_config.waterTickRate = std::max<uint32_t>(m_config.waterTickRate, 1U);
    m_config.lavaTickRate = std::max<uint32_t>(m_config.lavaTickRate, 1U);
    m_config.maxUpdatesPerTick = std::max<uint32_t>(m_config.maxUpdatesPerTick, 1U);
    m_config.maxSearchNodesPerTick = std::max<uint32_t>(m_config.maxSearchNodesPerTick, 1U);
    m_config.maxFlowSearchDepth = std::clamp(m_config.maxFlowSearchDepth, 0, kMaxFlowSearchDepth);
}

uint32_t FluidSimulator::tickRateFor(FluidType type) const noexcept {
    return type == FluidType::Lava ? m_config.lavaTickRate : m_config.waterTickRate;
}

int FluidSimulator::decayStepFor(FluidType type) const noexcept {
    return std::max(type == FluidType::Lava ? m_config.lavaDecayStep : m_config.waterDecayStep, 1);
}

int FluidSimulator::decayAt(const core::BlockPos& pos, FluidType type) const noexcept {
    const FluidState f = m_access.getFluid(pos);
    return f.type == type ? f.betaDecay() : -1;
}

// ---------------------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------------------

void FluidSimulator::placeSource(const core::BlockPos& pos, FluidType type) {
    if (type == FluidType::None || !m_access.isLoaded(pos)) {
        return;
    }
    writeFluid(pos, FluidState::source(type));
}

void FluidSimulator::removeFluid(const core::BlockPos& pos) {
    if (!m_access.isLoaded(pos) || m_access.getFluid(pos).isEmpty()) {
        return;
    }
    writeFluid(pos, FluidState{});
}

void FluidSimulator::notifyBlockChanged(const core::BlockPos& pos) {
    if (!m_access.isLoaded(pos)) {
        return;
    }
    if (tryHarden(pos)) {
        return;
    }
    scheduleFluid(pos);
    wakeNeighbours(pos, true);
}

void FluidSimulator::notifyRegionLoaded(const core::BlockPos& min, const core::BlockPos& max, bool wakeInterior) {
    // 64-bit loop bounds: growing the box by one must not overflow at extreme coordinates.
    const int64_t x0 = int64_t{std::min(min.x, max.x)} - 1;
    const int64_t y0 = int64_t{std::min(min.y, max.y)} - 1;
    const int64_t z0 = int64_t{std::min(min.z, max.z)} - 1;
    const int64_t x1 = int64_t{std::max(min.x, max.x)} + 1;
    const int64_t y1 = int64_t{std::max(min.y, max.y)} + 1;
    const int64_t z1 = int64_t{std::max(min.z, max.z)} + 1;
    const auto visit = [this](int64_t x, int64_t y, int64_t z) {
        const auto in32 = [](int64_t v) noexcept { return v >= INT32_MIN && v <= INT32_MAX; };
        if (in32(x) && in32(y) && in32(z)) {
            scheduleFluid({static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z)});
        }
    };
    for (int64_t y = y0; y <= y1; ++y) {
        for (int64_t z = z0; z <= z1; ++z) {
            // A row strictly inside the box in y and z only touches the outside world at
            // its two x ends; visiting just those keeps a border wake O(surface).
            const bool innerRow = !wakeInterior && y > y0 + 1 && y < y1 - 1 && z > z0 + 1 && z < z1 - 1;
            if (innerRow) {
                for (const int64_t x : {x0, x0 + 1, x1 - 1, x1}) {
                    visit(x, y, z); // duplicates (1-wide box) are absorbed by the queue
                }
                continue;
            }
            for (int64_t x = x0; x <= x1; ++x) {
                visit(x, y, z);
            }
        }
    }
}

void FluidSimulator::scheduleUpdate(const core::BlockPos& pos) {
    scheduleFluid(pos);
}

bool FluidSimulator::isSettled(const core::BlockPos& pos) const noexcept {
    const FluidState f = m_access.getFluid(pos);
    if (f.isEmpty()) {
        return true;
    }
    // Mirrors updateCell(): a source skips the decay recompute, so it can only harden,
    // fall or spread; anything thinner may need to retract.
    if (!f.isSource()) {
        return false;
    }
    if (f.type == FluidType::Lava && touchesWater(pos)) {
        return false;
    }
    if (canDisplace(pos.offset(0, -1, 0), f.type)) {
        return false;
    }
    return std::none_of(kHorizontal.begin(), kHorizontal.end(), [&](const core::BlockPos& d) {
        return canDisplace(pos.offset(d.x, d.y, d.z), f.type);
    });
}

std::vector<FluidEvent> FluidSimulator::takeEvents() noexcept {
    std::vector<FluidEvent> out;
    out.swap(m_events);
    return out;
}

FluidTickStats FluidSimulator::tick() {
    ++m_tick;
    FluidTickStats stats{};
    m_searchNodes = 0;
    core::BlockPos pos{};
    FluidType type = FluidType::None;
    // Both caps are checked before an update starts, so the first update always runs
    // (guaranteed progress) and the overshoot is at most one update's search.
    while (stats.processed < m_config.maxUpdatesPerTick && m_searchNodes < m_config.maxSearchNodesPerTick &&
           m_queue.popDue(m_tick, pos, type)) {
        // Beta TickNextTick skips entries whose block id no longer matches the world.
        if (m_access.getFluid(pos).type != type) {
            continue;
        }
        updateCell(pos);
        ++stats.processed;
    }
    // Exhausted only when due work was actually left behind (it runs first next tick,
    // since the queue orders by due tick and overdue entries sort ahead of new ones).
    const std::optional<uint64_t> next = m_queue.nextDueTick();
    stats.budgetExhausted = next.has_value() && *next <= m_tick;
    stats.pending = m_queue.size();
    stats.searchNodes = m_searchNodes;
    return stats;
}

uint64_t FluidSimulator::runUntilIdle(uint64_t maxTicks) {
    uint64_t ran = 0;
    while (ran < maxTicks && !m_queue.empty()) {
        tick();
        ++ran;
    }
    return ran;
}

// ---------------------------------------------------------------------------------------
// Writes, wake-ups and side effects
// ---------------------------------------------------------------------------------------

void FluidSimulator::emit(FluidEventKind kind, const core::BlockPos& pos, voxel::BlockId block) {
    if (m_events.size() < m_config.maxBufferedEvents) {
        m_events.push_back({kind, pos, block});
    }
}

void FluidSimulator::scheduleFluid(const core::BlockPos& pos) {
    const FluidState f = m_access.getFluid(pos);
    if (!f.isEmpty()) {
        m_queue.schedule(pos, m_tick + tickRateFor(f.type), f.type);
    }
}

void FluidSimulator::wakeNeighbours(const core::BlockPos& pos, bool hardenLava) {
    for (const core::BlockPos& d : kNeighbours) {
        const core::BlockPos n = pos.offset(d.x, d.y, d.z);
        // Beta onNeighborBlockChange: lava re-checks for adjacent water immediately.
        if (hardenLava && tryHarden(n)) {
            continue;
        }
        scheduleFluid(n);
    }
}

void FluidSimulator::writeFluid(const core::BlockPos& pos, FluidState state) {
    m_access.setFluid(pos, state);
    // Beta onBlockAdded: freshly placed lava hardens at once if it touches water.
    if (tryHarden(pos)) {
        return;
    }
    scheduleFluid(pos);
    wakeNeighbours(pos, true);
}

void FluidSimulator::writeBlock(const core::BlockPos& pos, voxel::BlockId id) {
    m_access.setBlock(pos, id);
    // No hardening cascade from here: a new solid block never adds water, and lava that
    // was already touching water is resolved on its own scheduled update instead. This
    // keeps the work per write O(1) even next to large pre-generated lava/water contacts.
    wakeNeighbours(pos, false);
}

bool FluidSimulator::touchesWater(const core::BlockPos& pos) const noexcept {
    // Above and the four horizontal neighbours; water *below* lava is handled by the
    // falling-lava-into-water rule instead.
    const core::BlockPos above = pos.offset(0, 1, 0);
    if (m_access.getFluid(above).type == FluidType::Water) {
        return true;
    }
    return std::any_of(kHorizontal.begin(), kHorizontal.end(), [&](const core::BlockPos& d) {
        return m_access.getFluid(pos.offset(d.x, d.y, d.z)).type == FluidType::Water;
    });
}

bool FluidSimulator::tryHarden(const core::BlockPos& pos) {
    const FluidState f = m_access.getFluid(pos);
    if (f.type != FluidType::Lava || !touchesWater(pos)) {
        return false;
    }
    const int decay = f.betaDecay();
    voxel::BlockId result = voxel::BlockId::Air;
    if (decay == 0) {
        result = voxel::BlockId::Obsidian;
    } else if (decay <= kMaxHardeningDecay) {
        result = voxel::BlockId::Cobblestone;
    } else {
        return false;
    }
    writeBlock(pos, result);
    emit(FluidEventKind::Fizz, pos, result);
    return true;
}

// ---------------------------------------------------------------------------------------
// Cell update (Beta BlockFlowing.updateTick)
// ---------------------------------------------------------------------------------------

bool FluidSimulator::cellBlocksFlow(const core::BlockPos& pos) const noexcept {
    // Fluid cells never block (their getBlock() is unspecified by the access contract).
    return m_access.getFluid(pos).isEmpty() && m_access.blocksFlow(pos);
}

bool FluidSimulator::canDisplace(const core::BlockPos& pos, FluidType type) const noexcept {
    if (!m_access.isLoaded(pos)) {
        return false;
    }
    const FluidState there = m_access.getFluid(pos);
    if (!there.isEmpty()) {
        // Beta liquidCanDisplaceBlock: never the same fluid, never lava; lava does
        // displace water.
        return type == FluidType::Lava && there.type == FluidType::Water;
    }
    return !m_access.blocksFlow(pos);
}

int FluidSimulator::recomputeDecay(const core::BlockPos& pos, FluidType type) const noexcept {
    const int step = decayStepFor(type);
    // Strongest horizontal neighbour (Beta getSmallestFlowDecay); falling neighbours feed
    // like sources but do not count towards the infinite-source rule.
    int smallest = -1;
    int adjacentSources = 0;
    for (const core::BlockPos& d : kHorizontal) {
        int n = decayAt(pos.offset(d.x, d.y, d.z), type);
        if (n < 0) {
            continue;
        }
        if (n == 0) {
            ++adjacentSources;
        }
        if (n >= FluidState::kFallingDecay) {
            n = 0;
        }
        if (smallest < 0 || n < smallest) {
            smallest = n;
        }
    }
    int next = smallest < 0 ? -1 : smallest + step;
    if (next >= FluidState::kFallingDecay) {
        next = -1;
    }
    // Fluid directly above feeds this cell as a falling column.
    if (decayAt(pos.offset(0, 1, 0), type) >= 0) {
        next = FluidState::kFallingDecay;
    }
    // Infinite water: two horizontal sources plus a solid floor or a water source
    // underneath turn this cell into a source (lava never regenerates).
    if (type == FluidType::Water && adjacentSources >= 2) {
        const core::BlockPos below = pos.offset(0, -1, 0);
        const FluidState under = m_access.getFluid(below);
        // An unloaded floor is unknown, not solid (Beta reads unloaded chunks as air).
        const bool solidFloor = under.isEmpty() && m_access.isLoaded(below) && cellBlocksFlow(below);
        if (solidFloor || (under.type == type && under.isSource())) {
            next = 0;
        }
    }
    return next;
}

bool FluidSimulator::lavaThinsNow(const core::BlockPos& pos) const noexcept {
    if (m_config.lavaThinningOdds <= 1U) {
        return true;
    }
    uint64_t h = mix64(m_config.randomSeed);
    h = mix64(h ^ static_cast<uint32_t>(pos.x));
    h = mix64(h ^ static_cast<uint32_t>(pos.y));
    h = mix64(h ^ static_cast<uint32_t>(pos.z));
    h = mix64(h ^ m_tick);
    return h % m_config.lavaThinningOdds == 0U;
}

void FluidSimulator::updateCell(const core::BlockPos& pos) {
    const FluidState self = m_access.getFluid(pos);
    if (self.isEmpty()) {
        return;
    }
    // Lava left touching water without a notification (world generation, chunk load).
    if (tryHarden(pos)) {
        return;
    }
    const FluidType type = self.type;
    int decay = self.betaDecay();

    if (decay > 0) {
        int next = recomputeDecay(pos, type);
        // Beta: lava that should thin (not dry out, not start falling) usually keeps its
        // level this time. Beta retries via later ticks; here the cell is simply re-queued.
        if (type == FluidType::Lava && decay < FluidState::kFallingDecay && next < FluidState::kFallingDecay &&
            next > decay && !lavaThinsNow(pos)) {
            next = decay;
            m_queue.schedule(pos, m_tick + tickRateFor(type), type);
        }
        if (next != decay) {
            decay = next;
            writeFluid(pos, FluidState::fromBetaDecay(type, decay));
            if (m_access.getFluid(pos).type != type) {
                return; // dried up or hardened
            }
        }
    }
    if (decay < 0) {
        return;
    }

    // Gravity has priority over spreading.
    const core::BlockPos below = pos.offset(0, -1, 0);
    if (canDisplace(below, type)) {
        if (type == FluidType::Lava && m_access.getFluid(below).type == FluidType::Water) {
            writeBlock(below, voxel::BlockId::Stone);
            emit(FluidEventKind::Fizz, below, voxel::BlockId::Stone);
            return;
        }
        flowInto(below, type, FluidState::kFallingDecay);
        return;
    }
    // Only sources, or cells resting on something solid, spread sideways: flowing water
    // lying on more water stays put.
    if (decay != 0 && !cellBlocksFlow(below)) {
        return;
    }
    const int spread = decay >= FluidState::kFallingDecay ? 1 : decay + decayStepFor(type);
    if (spread >= FluidState::kFallingDecay) {
        return;
    }
    const std::array<bool, 4> dirs = optimalFlowDirections(pos, type);
    for (size_t i = 0; i < kHorizontal.size(); ++i) {
        if (dirs[i]) {
            const core::BlockPos& d = kHorizontal[i];
            flowInto(pos.offset(d.x, d.y, d.z), type, spread);
        }
    }
}

void FluidSimulator::flowInto(const core::BlockPos& pos, FluidType type, int decay) {
    if (!canDisplace(pos, type)) {
        return;
    }
    // Beta flowIntoBlock(): lava destroys what it displaces with the mix effects (fizz,
    // no drop) whether that is water or a plant/torch; only water drops the block.
    const FluidState there = m_access.getFluid(pos);
    const voxel::BlockId displaced = there.isEmpty() ? m_access.getBlock(pos) : voxel::BlockId::Air;
    if (!there.isEmpty() || displaced != voxel::BlockId::Air) {
        if (type == FluidType::Lava) {
            emit(FluidEventKind::Fizz, pos, voxel::BlockId::Air);
        } else {
            emit(FluidEventKind::BlockWashedAway, pos, displaced);
        }
    }
    writeFluid(pos, FluidState::fromBetaDecay(type, decay));
}

// ---------------------------------------------------------------------------------------
// Flow direction search (Beta getOptimalFlowDirections / calculateFlowCost)
// ---------------------------------------------------------------------------------------

bool FluidSimulator::isPathBlocked(const core::BlockPos& pos, FluidType type) const noexcept {
    // Cells that are walls or already sources of the same fluid are not flow targets.
    if (cellBlocksFlow(pos)) {
        return true;
    }
    const FluidState f = m_access.getFluid(pos);
    return f.type == type && f.isSource();
}

int FluidSimulator::flowCost(const core::BlockPos& pos, FluidType type, int accumulated, size_t fromDir) {
    ++m_searchNodes;
    // Depth-limited DFS: distance to the closest cell with a drop below it, never
    // stepping straight back where we came from. Worst case 4 * 3^depth probes.
    int best = kNoDrop;
    for (size_t i = 0; i < kHorizontal.size(); ++i) {
        if (i == (fromDir ^ 1U)) {
            continue;
        }
        const core::BlockPos& d = kHorizontal[i];
        const core::BlockPos next = pos.offset(d.x, d.y, d.z);
        if (isPathBlocked(next, type)) {
            continue;
        }
        if (!cellBlocksFlow(next.offset(0, -1, 0))) {
            return accumulated;
        }
        if (accumulated >= m_config.maxFlowSearchDepth) {
            continue;
        }
        best = std::min(best, flowCost(next, type, accumulated + 1, i));
    }
    return best;
}

std::array<bool, 4> FluidSimulator::optimalFlowDirections(const core::BlockPos& pos, FluidType type) {
    std::array<int, 4> cost{kNoDrop, kNoDrop, kNoDrop, kNoDrop};
    for (size_t i = 0; i < kHorizontal.size(); ++i) {
        const core::BlockPos& d = kHorizontal[i];
        const core::BlockPos next = pos.offset(d.x, d.y, d.z);
        if (isPathBlocked(next, type)) {
            continue;
        }
        cost[i] = cellBlocksFlow(next.offset(0, -1, 0)) ? flowCost(next, type, 1, i) : 0;
    }
    const int best = *std::min_element(cost.begin(), cost.end());
    // With no reachable drop every direction ties at kNoDrop: spread everywhere.
    return {cost[0] == best, cost[1] == best, cost[2] == best, cost[3] == best};
}

} // namespace betacraft::fluids
