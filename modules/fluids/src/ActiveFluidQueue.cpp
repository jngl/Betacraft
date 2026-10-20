#include "fluids/ActiveFluidQueue.hpp"

namespace betacraft::fluids {

bool ActiveFluidQueue::schedule(const core::BlockPos& pos, uint64_t dueTick, FluidType type) {
    auto it = m_pending.find(pos);
    if (it != m_pending.end() && it->second.type == type && it->second.due <= dueTick) {
        return false;
    }
    const uint64_t seq = m_nextSeq++;
    if (it != m_pending.end()) {
        it->second = {dueTick, seq, type};
    } else {
        m_pending.emplace(pos, Pending{dueTick, seq, type});
    }
    m_heap.push({dueTick, seq, pos, type});
    return true;
}

void ActiveFluidQueue::dropStaleTop() noexcept {
    while (!m_heap.empty()) {
        const Entry& top = m_heap.top();
        const auto it = m_pending.find(top.pos);
        if (it != m_pending.end() && it->second.seq == top.seq) {
            return;
        }
        m_heap.pop();
    }
}

std::optional<uint64_t> ActiveFluidQueue::nextDueTick() noexcept {
    dropStaleTop();
    if (m_heap.empty()) {
        return std::nullopt;
    }
    return m_heap.top().due;
}

bool ActiveFluidQueue::popDue(uint64_t now, core::BlockPos& out, FluidType& type) {
    dropStaleTop();
    if (m_heap.empty() || m_heap.top().due > now) {
        return false;
    }
    out = m_heap.top().pos;
    type = m_heap.top().type;
    m_heap.pop();
    m_pending.erase(out);
    return true;
}

void ActiveFluidQueue::clear() noexcept {
    m_heap = {};
    m_pending.clear();
}

} // namespace betacraft::fluids
