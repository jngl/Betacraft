#include "ecs/Group.hpp"

#include <algorithm>
#include <utility>

namespace betacraft::ecs {

GroupHandler::GroupHandler(std::vector<SparseSet*> pools, std::vector<uint32_t> typeIds)
    : m_pools(std::move(pools)), m_typeIds(std::move(typeIds)) {
    std::sort(m_typeIds.begin(), m_typeIds.end());

    // Adopt the existing population: walk the smallest pool and pack every full match.
    // Positions [m_size, i) were already rejected, so swapping i with m_size is safe.
    const SparseSet* lead = *std::min_element(m_pools.begin(), m_pools.end(),
        [](const SparseSet* a, const SparseSet* b) { return a->size() < b->size(); });
    for (size_t i = 0; i < lead->size(); ++i) {
        const EntityId e = lead->entities()[i];
        if (ownsAll(e)) {
            pack(e);
        }
    }
    for (SparseSet* p : m_pools) {
        p->setObserver(this);
    }
}

GroupHandler::~GroupHandler() {
    for (SparseSet* p : m_pools) {
        if (p->observer() == this) {
            p->setObserver(nullptr);
        }
    }
}

bool GroupHandler::ownsExactly(std::span<const uint32_t> typeIds) const noexcept {
    if (typeIds.size() != m_typeIds.size()) {
        return false;
    }
    return std::all_of(typeIds.begin(), typeIds.end(), [this](uint32_t id) {
        return std::binary_search(m_typeIds.begin(), m_typeIds.end(), id);
    });
}

bool GroupHandler::ownsAll(EntityId e) const noexcept {
    return std::all_of(m_pools.begin(), m_pools.end(),
        [e](const SparseSet* p) { return p->contains(e); });
}

void GroupHandler::pack(EntityId e) noexcept {
    for (SparseSet* p : m_pools) {
        p->swapPositions(p->index(e), m_size);
    }
    ++m_size;
}

void GroupHandler::onInserted(EntityId e) {
    if (!contains(e) && ownsAll(e)) {
        pack(e);
    }
}

void GroupHandler::onRemoving(EntityId e) {
    if (!contains(e)) {
        return;
    }
    --m_size;
    for (SparseSet* p : m_pools) {
        p->swapPositions(p->index(e), m_size);
    }
}

} // namespace betacraft::ecs
