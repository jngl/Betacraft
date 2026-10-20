#include "redstone/RedstoneEngine.hpp"

#include "voxel/BlockShapes.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace betacraft::redstone {

namespace {

using world::BlockId;

constexpr std::array<glm::ivec3, 4> kHorizontal{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
constexpr std::array<glm::ivec3, 6> kSides{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};

// Neighbourhoods a change reaches: wires within 2 steps (a wire beside a block that lost or
// gained strong power, a wire whose step connection went through the cell), consumers within
// 3 (next to the block a neighbouring wire points into).
constexpr int32_t kWireSeedRadius = 2;
constexpr int32_t kConsumerRadius = 3;
// TorchBurnoutTracker entries are swept this often.
constexpr uint64_t kBurnoutSweepTicks = 200;

[[nodiscard]] core::BlockPos add(const core::BlockPos& p, const glm::ivec3& d) noexcept {
    return {p.x + d.x, p.y + d.y, p.z + d.z};
}

// Beta Block.canProvidePower, plus the wire itself (isPowerProviderOrWire).
[[nodiscard]] constexpr bool isPowerProviderOrWire(BlockId id) noexcept {
    switch (id) {
        case BlockId::RedstoneWire:
        case BlockId::Lever:
        case BlockId::StonePressurePlate:
        case BlockId::WoodPressurePlate:
        case BlockId::StoneButton:
        case BlockId::RedstoneTorchOff:
        case BlockId::RedstoneTorchOn:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr bool isTorch(BlockId id) noexcept {
    return id == BlockId::RedstoneTorchOff || id == BlockId::RedstoneTorchOn;
}

[[nodiscard]] constexpr bool isPlate(BlockId id) noexcept {
    return id == BlockId::StonePressurePlate || id == BlockId::WoodPressurePlate;
}

[[nodiscard]] constexpr bool isConsumer(BlockId id) noexcept {
    return isTorch(id) || world::isDoorBlock(id) || id == BlockId::Piston;
}

template <class Fn>
void forEachInDiamond(const core::BlockPos& c, int32_t radius, Fn&& fn) {
    for (int32_t dy = -radius; dy <= radius; ++dy) {
        const int32_t ry = radius - std::abs(dy);
        for (int32_t dz = -ry; dz <= ry; ++dz) {
            const int32_t rx = ry - std::abs(dz);
            for (int32_t dx = -rx; dx <= rx; ++dx) {
                fn(core::BlockPos{c.x + dx, c.y + dy, c.z + dz});
            }
        }
    }
}

} // namespace

RedstoneEngine::RedstoneEngine(IRedstoneVoxelAccess& access, world::ScheduledTickQueue* sharedQueue,
                               uint32_t maxEvaluationsPerTick)
    : m_access(access),
      m_queue(sharedQueue != nullptr ? sharedQueue : &m_ownQueue),
      m_sharedQueue(sharedQueue != nullptr),
      m_budget(std::max<uint32_t>(maxEvaluationsPerTick, 1)) {}

void RedstoneEngine::clear() noexcept {
    m_dirty.clear();
    m_dirtySet.clear();
    m_seeds.clear();
    m_removals.clear();
    m_relights.clear();
    m_overlay.clear();
    m_checks.clear();
    m_checkSet.clear();
    m_plateContacts.clear();
    m_poweredDoors.clear();
    m_burnout.clear();
    m_events.clear();
    if (!m_sharedQueue) {
        m_ownQueue.clear();
    }
}

void RedstoneEngine::onBlockChanged(core::BlockPos pos) {
    if (m_writing) {
        return;
    }
    if (m_dirtySet.insert(pos).second) {
        m_dirty.push_back(pos);
    }
}

void RedstoneEngine::onBlockLoaded(core::BlockPos pos) {
    const BlockId id = block(pos);
    if (isConsumer(id)) {
        enqueueCheck(pos);
    } else if (id == BlockId::StoneButton && (meta(pos) & world::kButtonPressedBit) != 0) {
        schedule(pos, kButtonTicks);
    } else if (isPlate(id) && (meta(pos) & world::kPlatePressedBit) != 0) {
        schedule(pos, kPlateTicks);
    }
}

void RedstoneEngine::schedule(core::BlockPos pos, uint32_t delayTicks) {
    if (!m_access.isLoaded(pos)) {
        return;
    }
    (void)m_queue->schedule(pos, block(pos), m_now + std::max<uint32_t>(delayTicks, 1));
}

size_t RedstoneEngine::pendingWork() const noexcept {
    return m_dirty.size() + m_seeds.size() + m_removals.size() + m_relights.size() + m_overlay.size() +
           m_checks.size();
}

std::vector<RedstoneEvent> RedstoneEngine::takeEvents() noexcept {
    std::vector<RedstoneEvent> out;
    out.swap(m_events);
    return out;
}

// --- Writes ----------------------------------------------------------------------------------

void RedstoneEngine::writeMetadata(const core::BlockPos& p, uint8_t value) {
    m_writing = true;
    m_access.setMetadata(p, value);
    m_writing = false;
}

void RedstoneEngine::writeBlock(const core::BlockPos& p, BlockId id, uint8_t value) {
    m_writing = true;
    m_access.setBlock(p, id, value);
    m_writing = false;
}

// --- Power rules ------------------------------------------------------------------------------

uint8_t RedstoneEngine::wirePower(const core::BlockPos& p) const {
    if (!isWire(p)) {
        return 0;
    }
    if (const auto it = m_overlay.find(p); it != m_overlay.end()) {
        return it->second;
    }
    return static_cast<uint8_t>(meta(p) & 0x0F);
}

void RedstoneEngine::setWirePower(const core::BlockPos& p, uint8_t power) {
    m_overlay[p] = power;
}

bool RedstoneEngine::connectsTowards(const core::BlockPos& p, const glm::ivec3& dir) const {
    const core::BlockPos n = add(p, dir);
    if (isPowerProviderOrWire(block(n))) {
        return true;
    }
    if (!normalCube(n)) {
        return isWire(n.offset(0, -1, 0));
    }
    return !normalCube(p.offset(0, 1, 0)) && isWire(n.offset(0, 1, 0));
}

size_t RedstoneEngine::connectedWires(const core::BlockPos& p, std::array<core::BlockPos, 4>& out) const {
    // Beta BlockRedstoneWire.calculateCurrentChanges: per side, the wire at the same level,
    // then one up over a normal cube (nothing solid above this wire) or one down past a
    // non-cube. A same-level wire is never a normal cube, so the two never both apply.
    size_t count = 0;
    const bool openAbove = !normalCube(p.offset(0, 1, 0));
    for (const glm::ivec3& d : kHorizontal) {
        const core::BlockPos n = add(p, d);
        if (isWire(n)) {
            out[count++] = n;
        } else if (normalCube(n)) {
            if (openAbove && isWire(n.offset(0, 1, 0))) out[count++] = n.offset(0, 1, 0);
        } else if (isWire(n.offset(0, -1, 0))) {
            out[count++] = n.offset(0, -1, 0);
        }
    }
    return count;
}

bool RedstoneEngine::wirePowersTowards(const core::BlockPos& wire, const core::BlockPos& target) const {
    const glm::ivec3 d{target.x - wire.x, target.y - wire.y, target.z - wire.z};
    if (d == glm::ivec3{0, -1, 0}) {
        return true; // The block the wire lies on
    }
    if (d.y != 0 || std::abs(d.x) + std::abs(d.z) != 1) {
        return false;
    }
    // Beta BlockRedstoneWire.isPoweringTo: a dot powers its four sides; a line powers the side
    // it runs into from behind, unless it also turns sideways.
    const glm::ivec3 back{-d.x, 0, -d.z};
    const glm::ivec3 left{d.z, 0, d.x};
    const glm::ivec3 right{-d.z, 0, -d.x};
    const bool b = connectsTowards(wire, back);
    const bool l = connectsTowards(wire, left);
    const bool r = connectsTowards(wire, right);
    if (!b && !l && !r && !connectsTowards(wire, d)) {
        return true;
    }
    return b && !l && !r;
}

uint8_t RedstoneEngine::weakPower(const core::BlockPos& src, const core::BlockPos& target, bool wires) const {
    const BlockId id = block(src);
    const uint8_t m = meta(src);
    switch (id) {
        case BlockId::Lever: return (m & world::kLeverOnBit) != 0 ? kMaxPower : 0;
        case BlockId::StoneButton: return (m & world::kButtonPressedBit) != 0 ? kMaxPower : 0;
        case BlockId::StonePressurePlate:
        case BlockId::WoodPressurePlate: return (m & world::kPlatePressedBit) != 0 ? kMaxPower : 0;
        case BlockId::RedstoneTorchOn:
            return add(src, world::attachmentOffset(m)) == target ? uint8_t{0} : kMaxPower;
        case BlockId::RedstoneWire:
            return wires && wirePowersTowards(src, target) ? wirePower(src) : uint8_t{0};
        default: return 0;
    }
}

uint8_t RedstoneEngine::strongPower(const core::BlockPos& src, const core::BlockPos& target, bool wires) const {
    const BlockId id = block(src);
    const uint8_t m = meta(src);
    switch (id) {
        case BlockId::Lever:
        case BlockId::StoneButton:
            // The on bit and the pressed bit are the same 0x8.
            return (m & world::kLeverOnBit) != 0 && add(src, world::attachmentOffset(m)) == target ? kMaxPower
                                                                                                    : uint8_t{0};
        case BlockId::StonePressurePlate:
        case BlockId::WoodPressurePlate:
            return (m & world::kPlatePressedBit) != 0 && src.offset(0, -1, 0) == target ? kMaxPower : uint8_t{0};
        case BlockId::RedstoneTorchOn: return src.offset(0, 1, 0) == target ? kMaxPower : uint8_t{0};
        case BlockId::RedstoneWire: return weakPower(src, target, wires);
        default: return 0;
    }
}

bool RedstoneEngine::isStronglyPowered(const core::BlockPos& cube, bool wires) const {
    for (const glm::ivec3& d : kSides) {
        if (strongPower(add(cube, d), cube, wires) > 0) {
            return true;
        }
    }
    return false;
}

uint8_t RedstoneEngine::directWirePower(const core::BlockPos& p) const {
    for (const glm::ivec3& d : kSides) {
        const core::BlockPos n = add(p, d);
        if (normalCube(n) ? isStronglyPowered(n, false) : weakPower(n, p, false) > 0) {
            return kMaxPower;
        }
    }
    return 0;
}

uint8_t RedstoneEngine::computeWirePower(const core::BlockPos& p) const {
    uint8_t best = directWirePower(p);
    if (best == kMaxPower) {
        return best;
    }
    std::array<core::BlockPos, 4> wires{};
    const size_t n = connectedWires(p, wires);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t w = wirePower(wires[i]);
        if (w > 0) {
            best = std::max<uint8_t>(best, static_cast<uint8_t>(w - 1));
        }
    }
    return best;
}

bool RedstoneEngine::isPowered(core::BlockPos pos) const {
    for (const glm::ivec3& d : kSides) {
        const core::BlockPos n = add(pos, d);
        if (normalCube(n) ? isStronglyPowered(n, true) : weakPower(n, pos, true) > 0) {
            return true;
        }
    }
    return false;
}

uint8_t RedstoneEngine::powerAt(core::BlockPos pos) const {
    if (isWire(pos)) {
        return wirePower(pos);
    }
    return isPowered(pos) ? kMaxPower : uint8_t{0};
}

bool RedstoneEngine::torchPowered(const core::BlockPos& torch) const {
    // Beta BlockRedstoneTorch.isIndirectlyPowered: the block it hangs on provides power to it.
    const core::BlockPos support = add(torch, world::attachmentOffset(meta(torch)));
    return normalCube(support) ? isStronglyPowered(support, true) : weakPower(support, torch, true) > 0;
}

// --- Sources ----------------------------------------------------------------------------------

bool RedstoneEngine::toggleLever(core::BlockPos pos) {
    if (block(pos) != BlockId::Lever) {
        return false;
    }
    writeMetadata(pos, static_cast<uint8_t>(meta(pos) ^ world::kLeverOnBit));
    onBlockChanged(pos);
    m_events.push_back({pos, RedstoneEventKind::Click});
    return true;
}

bool RedstoneEngine::pressButton(core::BlockPos pos) {
    if (block(pos) != BlockId::StoneButton || (meta(pos) & world::kButtonPressedBit) != 0) {
        return false;
    }
    writeMetadata(pos, static_cast<uint8_t>(meta(pos) | world::kButtonPressedBit));
    schedule(pos, kButtonTicks);
    onBlockChanged(pos);
    m_events.push_back({pos, RedstoneEventKind::Click});
    return true;
}

bool RedstoneEngine::reportPlateContact(core::BlockPos pos) {
    if (!isPlate(block(pos))) {
        return false;
    }
    m_plateContacts[pos] = m_now;
    if ((meta(pos) & world::kPlatePressedBit) != 0) {
        return false;
    }
    writeMetadata(pos, static_cast<uint8_t>(meta(pos) | world::kPlatePressedBit));
    schedule(pos, kPlateTicks);
    onBlockChanged(pos);
    m_events.push_back({pos, RedstoneEventKind::Click});
    return true;
}

// --- Scheduled updates ------------------------------------------------------------------------

void RedstoneEngine::runScheduledTick(core::BlockPos pos) {
    const BlockId id = block(pos);
    const uint8_t m = meta(pos);
    switch (id) {
        case BlockId::RedstoneTorchOn:
            if (torchPowered(pos)) {
                // Beta: a torch goes out even when that switch burns it out.
                const bool burnt = !m_burnout.tryToggle(pos, m_now);
                writeBlock(pos, BlockId::RedstoneTorchOff, m);
                ++m_stats.torchToggles;
                if (burnt) {
                    ++m_stats.burnouts;
                    m_events.push_back({pos, RedstoneEventKind::TorchBurnout});
                    schedule(pos, static_cast<uint32_t>(TorchBurnoutTracker::kBurnoutTicks));
                }
                onBlockChanged(pos);
            }
            break;
        case BlockId::RedstoneTorchOff:
            if (!torchPowered(pos)) {
                if (m_burnout.isBurntOut(pos, m_now)) {
                    schedule(pos, static_cast<uint32_t>(m_burnout.burntUntil(pos) - m_now));
                    break;
                }
                if (!m_burnout.tryToggle(pos, m_now)) {
                    ++m_stats.burnouts;
                    m_events.push_back({pos, RedstoneEventKind::TorchBurnout});
                    schedule(pos, static_cast<uint32_t>(TorchBurnoutTracker::kBurnoutTicks));
                    break;
                }
                writeBlock(pos, BlockId::RedstoneTorchOn, m);
                ++m_stats.torchToggles;
                onBlockChanged(pos);
            }
            break;
        case BlockId::StoneButton:
            if ((m & world::kButtonPressedBit) != 0) {
                writeMetadata(pos, static_cast<uint8_t>(m & ~world::kButtonPressedBit));
                onBlockChanged(pos);
                m_events.push_back({pos, RedstoneEventKind::Click});
            }
            break;
        case BlockId::StonePressurePlate:
        case BlockId::WoodPressurePlate: {
            if ((m & world::kPlatePressedBit) == 0) {
                break;
            }
            const auto it = m_plateContacts.find(pos);
            if (it != m_plateContacts.end() && m_now - it->second <= 1) {
                schedule(pos, kPlateTicks); // Still stood on (Beta: entities in the box)
                break;
            }
            if (it != m_plateContacts.end()) {
                m_plateContacts.erase(it);
            }
            writeMetadata(pos, static_cast<uint8_t>(m & ~world::kPlatePressedBit));
            onBlockChanged(pos);
            m_events.push_back({pos, RedstoneEventKind::Click});
            break;
        }
        default:
            break;
    }
}

// --- Work queues ------------------------------------------------------------------------------

void RedstoneEngine::enqueueCheck(const core::BlockPos& p) {
    if (m_checkSet.insert(p).second) {
        m_checks.push_back(p);
    }
}

void RedstoneEngine::seedAround(const core::BlockPos& p) {
    forEachInDiamond(p, kWireSeedRadius, [&](const core::BlockPos& c) {
        if (isWire(c)) m_seeds.push_back(c);
    });
    forEachInDiamond(p, kConsumerRadius, [&](const core::BlockPos& c) {
        if (isConsumer(block(c))) enqueueCheck(c);
    });
}

void RedstoneEngine::seedWire(const core::BlockPos& p) {
    if (!isWire(p)) {
        return;
    }
    const uint8_t current = wirePower(p);
    const uint8_t target = computeWirePower(p);
    if (target < current) {
        setWirePower(p, 0);
        m_removals.push_back({p, current});
    } else if (target > current) {
        setWirePower(p, target);
        m_relights.push_back(p);
    }
}

void RedstoneEngine::processRemoval(const Removal& r) {
    // Phase 1: wires fed by the one switched off (weaker than it was) go dark too; stronger
    // ones are fed from elsewhere and relight the darkened part in phase 2.
    std::array<core::BlockPos, 4> wires{};
    const size_t n = connectedWires(r.pos, wires);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t level = wirePower(wires[i]);
        if (level == 0) {
            continue;
        }
        if (level < r.oldPower) {
            setWirePower(wires[i], 0);
            m_removals.push_back({wires[i], level});
        } else {
            m_relights.push_back(wires[i]);
        }
    }
    if (isWire(r.pos) && directWirePower(r.pos) > 0) {
        setWirePower(r.pos, kMaxPower);
        m_relights.push_back(r.pos);
    }
}

void RedstoneEngine::processRelight(const core::BlockPos& p) {
    // Phase 2: spreads the level outwards, one less per wire.
    if (!isWire(p)) {
        return;
    }
    uint8_t level = wirePower(p);
    const uint8_t direct = directWirePower(p);
    if (direct > level) {
        level = direct;
        setWirePower(p, level);
    }
    if (level <= 1) {
        return;
    }
    std::array<core::BlockPos, 4> wires{};
    const size_t n = connectedWires(p, wires);
    for (size_t i = 0; i < n; ++i) {
        if (wirePower(wires[i]) < level - 1) {
            setWirePower(wires[i], static_cast<uint8_t>(level - 1));
            m_relights.push_back(wires[i]);
        }
    }
}

void RedstoneEngine::flushOverlay() {
    // The network settled: write the new powers, and re-check the consumers around every wire
    // that switched on or off.
    std::unordered_map<core::BlockPos, uint8_t> overlay;
    overlay.swap(m_overlay);
    for (const auto& [pos, power] : overlay) {
        if (!isWire(pos)) {
            continue;
        }
        const auto old = static_cast<uint8_t>(meta(pos) & 0x0F);
        if (old == power) {
            continue;
        }
        writeMetadata(pos, power);
        if ((old > 0) != (power > 0)) {
            forEachInDiamond(pos, kWireSeedRadius, [&](const core::BlockPos& c) {
                if (isConsumer(block(c))) enqueueCheck(c);
            });
        }
    }
}

void RedstoneEngine::evaluateConsumer(const core::BlockPos& p) {
    const BlockId id = block(p);
    if (isTorch(id)) {
        evaluateTorch(p);
    } else if (world::isDoorBlock(id)) {
        evaluateDoor(p);
    } else if (id == BlockId::Piston) {
        evaluatePiston(p);
    }
}

void RedstoneEngine::evaluateTorch(const core::BlockPos& p) {
    const bool lit = block(p) == BlockId::RedstoneTorchOn;
    if (lit == torchPowered(p)) {
        schedule(p, kTorchDelayTicks); // Beta: the switch happens 2 ticks later
    }
}

void RedstoneEngine::evaluateDoor(const core::BlockPos& p) {
    const BlockId id = block(p);
    const core::BlockPos lower = (meta(p) & world::kDoorUpperBit) != 0 ? p.offset(0, -1, 0) : p;
    if (block(lower) != id) {
        return;
    }
    const core::BlockPos upper = lower.offset(0, 1, 0);
    const bool powered = isPowered(lower) || (block(upper) == id && isPowered(upper));
    const uint8_t m = meta(lower);
    const bool open = (m & world::kDoorOpenBit) != 0;
    bool wantOpen = open;
    if (powered && m_poweredDoors.insert(lower).second) {
        wantOpen = true;
    } else if (!powered && m_poweredDoors.erase(lower) > 0) {
        wantOpen = false;
    }
    if (wantOpen == open) {
        return;
    }
    const auto lowerMeta = static_cast<uint8_t>((m ^ world::kDoorOpenBit) & ~world::kDoorUpperBit);
    writeMetadata(lower, lowerMeta);
    if (block(upper) == id) {
        writeMetadata(upper, static_cast<uint8_t>(lowerMeta | world::kDoorUpperBit));
    }
    m_events.push_back({lower, wantOpen ? RedstoneEventKind::DoorOpen : RedstoneEventKind::DoorClose});
}

void RedstoneEngine::evaluatePiston(const core::BlockPos& p) {
    const uint8_t m = meta(p);
    const bool powered = isPowered(p);
    const bool wasPowered = (m & world::kPistonPoweredBit) != 0;
    if (powered == wasPowered) {
        return;
    }
    writeMetadata(p, static_cast<uint8_t>(m ^ world::kPistonPoweredBit));
    if (powered && m_access.pushPiston(p, world::pistonFacing(m))) {
        m_events.push_back({p, RedstoneEventKind::PistonPush});
    }
}

// --- Tick -------------------------------------------------------------------------------------

RedstoneTickStats RedstoneEngine::tick(uint64_t worldTick) {
    m_now = worldTick;
    if (!m_sharedQueue) {
        m_due.clear();
        (void)m_ownQueue.drainDue(m_now, m_due);
        for (const world::ScheduledTickQueue::Entry& entry : m_due) {
            if (block(entry.pos) == entry.block) {
                runScheduledTick(entry.pos);
            }
        }
    }

    uint32_t evaluations = 0;
    while (evaluations < m_budget) {
        if (!m_dirty.empty()) {
            const core::BlockPos p = m_dirty.front();
            m_dirty.pop_front();
            m_dirtySet.erase(p);
            seedAround(p);
        } else if (!m_seeds.empty()) {
            const core::BlockPos p = m_seeds.front();
            m_seeds.pop_front();
            seedWire(p);
        } else if (!m_removals.empty()) {
            const Removal r = m_removals.front();
            m_removals.pop_front();
            processRemoval(r);
        } else if (!m_relights.empty()) {
            const core::BlockPos p = m_relights.front();
            m_relights.pop_front();
            processRelight(p);
        } else if (!m_overlay.empty()) {
            flushOverlay();
            continue; // Writing the settled powers is not an evaluation
        } else if (!m_checks.empty()) {
            const core::BlockPos p = m_checks.front();
            m_checks.pop_front();
            m_checkSet.erase(p);
            evaluateConsumer(p);
        } else {
            break;
        }
        ++evaluations;
    }

    if (m_now % kBurnoutSweepTicks == 0) {
        m_burnout.collect(m_now);
    }
    // Switches made by scheduled updates the World ran before this call count for this tick.
    RedstoneTickStats stats = m_stats;
    m_stats = RedstoneTickStats{};
    stats.evaluations = evaluations;
    stats.pending = pendingWork();
    stats.budgetExhausted = evaluations >= m_budget && stats.pending > 0;
    return stats;
}

} // namespace betacraft::redstone
