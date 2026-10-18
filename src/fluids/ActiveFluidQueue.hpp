#pragma once

#include "core/Types.hpp"
#include "fluids/FluidState.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace betacraft::fluids {

// Scheduled-tick queue holding only the fluid cells that are actively propagating (the
// "dirty frontier"), so idle oceans and lakes cost nothing per tick.
//
// Ordering is a strict (dueTick, insertionSequence) min-heap: two runs fed the same
// schedule calls pop in exactly the same order, independently of hash-table layout,
// which keeps the whole simulation deterministic.
//
// Each position is pending at most once per fluid type (Beta's scheduledTickSet keys
// NextTickListEntry by position *and* block id). Re-scheduling a pending cell earlier for
// the same type moves it forward and later requests are ignored; a request for a
// different type replaces the entry outright, so lava displacing a pending water cell
// waits its own 30-tick rate instead of inheriting the water's earlier tick. The popped
// type lets the caller skip an entry whose cell changed fluid without re-scheduling.
// Superseded heap entries are skipped lazily on pop instead of being searched for.
class ActiveFluidQueue {
public:
    // Returns true if the position was newly queued, moved to an earlier tick, or
    // re-keyed to another fluid type.
    bool schedule(const core::BlockPos& pos, uint64_t dueTick, FluidType type = FluidType::None);

    [[nodiscard]] bool isPending(const core::BlockPos& pos) const noexcept {
        return m_pending.find(pos) != m_pending.end();
    }
    [[nodiscard]] size_t size() const noexcept { return m_pending.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_pending.empty(); }

    // Earliest due tick among live entries (purges stale heap tops, hence non-const-free).
    [[nodiscard]] std::optional<uint64_t> nextDueTick() noexcept;

    // Pops the next live entry whose due tick is <= now. Returns false when none is due.
    bool popDue(uint64_t now, core::BlockPos& out, FluidType& type);
    bool popDue(uint64_t now, core::BlockPos& out) {
        FluidType ignored = FluidType::None;
        return popDue(now, out, ignored);
    }

    void clear() noexcept;

private:
    struct Entry {
        uint64_t due;
        uint64_t seq;
        core::BlockPos pos;
        FluidType type;
    };
    struct Later {
        bool operator()(const Entry& a, const Entry& b) const noexcept {
            return a.due != b.due ? a.due > b.due : a.seq > b.seq;
        }
    };
    struct Pending {
        uint64_t due;
        uint64_t seq;
        FluidType type;
    };

    void dropStaleTop() noexcept;

    std::priority_queue<Entry, std::vector<Entry>, Later> m_heap;
    std::unordered_map<core::BlockPos, Pending> m_pending;
    uint64_t m_nextSeq{0};
};

} // namespace betacraft::fluids
