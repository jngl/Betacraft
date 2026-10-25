#pragma once

#include "core/Types.hpp"
#include "voxel/Block.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

namespace betacraft::voxel {

// Block updates scheduled for a later game tick (Beta World.scheduleBlockUpdate /
// TickNextTick): falling sand now, redstone later. A min-heap on the due tick, at most
// one pending entry per position (Beta's NextTickListEntry set), first scheduled first served
// among entries due on the same tick. Not persisted: an update pending when the world is
// saved is lost.
class ScheduledTickQueue {
public:
    struct Entry {
        core::BlockPos pos{};
        BlockId block{BlockId::Air}; // Block the update was scheduled for; it does nothing if replaced
        uint64_t dueTick{0};
        uint64_t sequence{0};        // Scheduling order, breaks ties between equal due ticks
    };

    // Queues an update of `block` at `pos` for `dueTick`. Returns false, leaving the queue
    // unchanged, when `pos` already has a pending update.
    bool schedule(const core::BlockPos& pos, BlockId block, uint64_t dueTick);

    // Moves the entries due at or before `now` into `out` (appended, earliest first), at most
    // `maxEntries`; the rest stay queued for the next call. Returns how many were moved.
    size_t drainDue(uint64_t now, std::vector<Entry>& out,
                    size_t maxEntries = std::numeric_limits<size_t>::max());

    [[nodiscard]] bool isScheduled(const core::BlockPos& pos) const noexcept { return m_pending.contains(pos); }
    [[nodiscard]] size_t size() const noexcept { return m_heap.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_heap.empty(); }
    void clear() noexcept;

private:
    std::vector<Entry> m_heap; // std::push_heap / pop_heap with a "later first" comparator
    std::unordered_set<core::BlockPos> m_pending;
    uint64_t m_nextSequence{0};
};

} // namespace betacraft::voxel
