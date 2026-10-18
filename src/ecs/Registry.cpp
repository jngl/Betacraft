#include "ecs/Registry.hpp"

#include <algorithm>
#include <atomic>
#include <utility>

namespace betacraft::ecs {

namespace detail {

uint32_t nextComponentTypeId() noexcept {
    static std::atomic<uint32_t> counter{0};
    return counter.fetch_add(1U, std::memory_order_relaxed);
}

} // namespace detail

Registry::Registry(uint32_t maxEntities, uint32_t recycleThreshold) noexcept
    : m_capacity(std::min(maxEntities, MAX_ENTITIES)), m_recycleThreshold(recycleThreshold) {}

Registry::~Registry() = default;
Registry::Registry(Registry&& other) noexcept
    : m_slots(std::move(other.m_slots)),
      m_alive(std::move(other.m_alive)),
      m_freeIds(std::move(other.m_freeIds)),
      m_pools(std::move(other.m_pools)),
      m_groups(std::move(other.m_groups)),
      m_aliveCount(std::exchange(other.m_aliveCount, 0U)),
      m_capacity(other.m_capacity),
      m_recycleThreshold(other.m_recycleThreshold) {}

Registry& Registry::operator=(Registry&& other) noexcept {
    if (this != &other) {
        // Groups observe pools: tear them down before the pools they point into.
        m_groups.clear();
        m_pools.clear();
        m_slots = std::move(other.m_slots);
        m_alive = std::move(other.m_alive);
        m_freeIds = std::move(other.m_freeIds);
        m_pools = std::move(other.m_pools);
        m_groups = std::move(other.m_groups);
        m_aliveCount = std::exchange(other.m_aliveCount, 0U);
        m_capacity = other.m_capacity;
        m_recycleThreshold = other.m_recycleThreshold;
    }
    return *this;
}

EntityId Registry::create() {
    if (m_slots.empty()) {
        // Reserved index 0 (never alive). Done lazily so a moved-from registry stays correct.
        m_slots.push_back(makeEntity(0U, 0U));
        m_alive.push_back(0U);
    }
    const bool full = slotCount() >= m_capacity;
    if (!m_freeIds.empty() && (full || m_freeIds.size() > m_recycleThreshold)) {
        const uint32_t idx = m_freeIds.front();
        m_freeIds.pop_front();
        m_alive[idx] = 1U;
        ++m_aliveCount;
        return m_slots[idx];
    }
    if (full) {
        return NULL_ENTITY;
    }
    const auto idx = static_cast<uint32_t>(m_slots.size());
    const EntityId e = makeEntity(idx, 0U);
    m_slots.push_back(e);
    m_alive.push_back(1U);
    ++m_aliveCount;
    return e;
}

bool Registry::destroy(EntityId e) {
    if (!valid(e)) {
        return false;
    }
    for (const auto& p : m_pools) {
        if (p) {
            (void)p->remove(e);
        }
    }
    const uint32_t idx = entityIndex(e);
    m_alive[idx] = 0U;
    --m_aliveCount;
    // Wrap-around (255 -> 0) rather than retirement; see the constructor comment.
    m_slots[idx] = makeEntity(idx, (entityGeneration(e) + 1U) & ENTITY_GENERATION_MASK);
    m_freeIds.push_back(idx);
    return true;
}

void Registry::clear() {
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_alive[i] != 0U) {
            (void)destroy(m_slots[i]);
        }
    }
}

} // namespace betacraft::ecs
