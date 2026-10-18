#include "ecs/SparseSet.hpp"

#include <algorithm>
#include <utility>

namespace betacraft::ecs {

size_t SparseSet::allocatedPages() const noexcept {
    return static_cast<size_t>(std::count_if(m_sparse.begin(), m_sparse.end(),
        [](const std::unique_ptr<Page>& p) { return static_cast<bool>(p); }));
}

uint32_t& SparseSet::sparseSlot(uint32_t idx) {
    const size_t page = idx / PAGE_SIZE;
    if (page >= m_sparse.size()) {
        m_sparse.resize(page + 1);
    }
    if (!m_sparse[page]) {
        m_sparse[page] = std::make_unique<Page>();
        m_sparse[page]->fill(NPOS);
    }
    return (*m_sparse[page])[idx % PAGE_SIZE];
}

bool SparseSet::insertSilently(EntityId e) {
    if (!canInsert(e)) {
        return false;
    }
    // Strong guarantee: both steps that may throw (page allocation, dense growth) run before
    // the sparse slot is written, so a bad_alloc never leaves a slot pointing past m_dense.
    // A page allocated for nothing is harmless (all NPOS).
    uint32_t& slot = sparseSlot(entityIndex(e));
    m_dense.push_back(e);
    slot = static_cast<uint32_t>(m_dense.size() - 1U);
    return true;
}

bool SparseSet::insert(EntityId e) {
    if (!acceptsBareInsert() || !insertSilently(e)) {
        return false;
    }
    notifyInserted(e);
    return true;
}

bool SparseSet::remove(EntityId e) {
    if (!contains(e)) {
        return false;
    }
    // The observer may reorder the dense array (owning groups shrink their prefix), so the
    // position is read only after notification.
    if (m_observer != nullptr) {
        m_observer->onRemoving(e);
    }
    const size_t pos = index(e);
    const size_t last = m_dense.size() - 1;
    swapPositions(pos, last);
    (*m_sparse[entityIndex(e) / PAGE_SIZE])[entityIndex(e) % PAGE_SIZE] = NPOS;
    m_dense.pop_back();
    popPayload();
    return true;
}

void SparseSet::swapPositions(size_t a, size_t b) noexcept {
    if (a == b) {
        return;
    }
    const EntityId ea = m_dense[a];
    const EntityId eb = m_dense[b];
    std::swap(m_dense[a], m_dense[b]);
    (*m_sparse[entityIndex(ea) / PAGE_SIZE])[entityIndex(ea) % PAGE_SIZE] = static_cast<uint32_t>(b);
    (*m_sparse[entityIndex(eb) / PAGE_SIZE])[entityIndex(eb) % PAGE_SIZE] = static_cast<uint32_t>(a);
    swapPayload(a, b);
}

void SparseSet::clear() noexcept {
    for (const EntityId e : m_dense) {
        (*m_sparse[entityIndex(e) / PAGE_SIZE])[entityIndex(e) % PAGE_SIZE] = NPOS;
    }
    m_dense.clear();
    clearPayload();
    if (m_observer != nullptr) {
        m_observer->onCleared();
    }
}

} // namespace betacraft::ecs
