#include "voxel/ScheduledTicks.hpp"

#include <algorithm>

namespace betacraft::world {

namespace {

// std heap algorithms keep the "largest" element on top; ordering by "later" puts the
// earliest due entry there.
[[nodiscard]] bool later(const ScheduledTickQueue::Entry& a, const ScheduledTickQueue::Entry& b) noexcept {
    if (a.dueTick != b.dueTick) return a.dueTick > b.dueTick;
    return a.sequence > b.sequence;
}

} // namespace

bool ScheduledTickQueue::schedule(const core::BlockPos& pos, BlockId block, uint64_t dueTick) {
    if (!m_pending.insert(pos).second) {
        return false;
    }
    m_heap.push_back({pos, block, dueTick, m_nextSequence++});
    std::push_heap(m_heap.begin(), m_heap.end(), later);
    return true;
}

size_t ScheduledTickQueue::drainDue(uint64_t now, std::vector<Entry>& out, size_t maxEntries) {
    size_t moved = 0;
    while (moved < maxEntries && !m_heap.empty() && m_heap.front().dueTick <= now) {
        std::pop_heap(m_heap.begin(), m_heap.end(), later);
        out.push_back(m_heap.back());
        m_heap.pop_back();
        m_pending.erase(out.back().pos);
        ++moved;
    }
    return moved;
}

void ScheduledTickQueue::clear() noexcept {
    m_heap.clear();
    m_pending.clear();
}

} // namespace betacraft::world
