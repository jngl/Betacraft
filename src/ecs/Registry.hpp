#pragma once

#include "ecs/ComponentPool.hpp"
#include "ecs/Entity.hpp"
#include "ecs/Group.hpp"
#include "ecs/SparseSet.hpp"
#include "ecs/View.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace betacraft::ecs {

namespace detail {

// Dense, process-wide component type ids. The counter lives in Registry.cpp so every
// translation unit (and every Phase 2 static library) agrees on the same numbering.
[[nodiscard]] uint32_t nextComponentTypeId() noexcept;

template<typename T>
[[nodiscard]] uint32_t componentTypeId() noexcept {
    static const uint32_t id = nextComponentTypeId();
    return id;
}

template<typename T, typename... Rest>
[[nodiscard]] consteval bool allDistinct() noexcept {
    if constexpr (sizeof...(Rest) == 0) {
        return true;
    } else {
        return (!std::is_same_v<T, Rest> && ...) && allDistinct<Rest...>();
    }
}

} // namespace detail

// ECS registry (Fiche 3 §2.2): owns entity slots and one sparse-set pool per component type.
//
// Error policy: lookups (tryGet/has/remove/destroy) are total and simply fail on invalid or
// stale handles; mutations that cannot be honoured (emplace on a dead entity, duplicate
// emplace, get of a missing component) throw, because they reveal a gameplay logic bug.
class Registry {
public:
    // Index 0 is reserved (the spec's m_nextId{1}): the all-zero handle is never issued, so a
    // zero-initialised EntityId field (serialisation, integration structs) never aliases a
    // live entity. Usable indices are 1..MAX_ENTITY_INDEX.
    static constexpr uint32_t MAX_ENTITIES = MAX_ENTITY_INDEX;
    static constexpr uint32_t DEFAULT_RECYCLE_THRESHOLD = 1024U;

    // maxEntities caps the number of simultaneously alive entities (clamped to MAX_ENTITIES),
    // e.g. a hard budget on transient entities (dropped items). Freed slots are recycled FIFO,
    // and only once more than recycleThreshold of them are waiting (or the cap is reached):
    // the 8-bit generation wraps around, so a stale handle can only alias after its slot has
    // been recycled 256 times, i.e. after >= 256 * recycleThreshold creations. Retiring
    // exhausted slots instead would leak index space (and sparse pages) forever.
    explicit Registry(uint32_t maxEntities = MAX_ENTITIES,
                      uint32_t recycleThreshold = DEFAULT_RECYCLE_THRESHOLD) noexcept;
    ~Registry();

    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) noexcept;
    Registry& operator=(Registry&&) noexcept;

    // ---- Entities ---------------------------------------------------------------------

    // Returns NULL_ENTITY only when capacity() entities are alive.
    [[nodiscard]] EntityId create();
    // Removes every component of e, then frees its slot with a bumped generation.
    bool destroy(EntityId e);

    [[nodiscard]] bool valid(EntityId e) const noexcept {
        const uint32_t idx = entityIndex(e);
        return e != NULL_ENTITY && idx < m_slots.size() && m_alive[idx] != 0U && m_slots[idx] == e;
    }

    [[nodiscard]] size_t alive() const noexcept { return m_aliveCount; }
    [[nodiscard]] uint32_t capacity() const noexcept { return m_capacity; }
    // Indices handed out so far (alive or free), i.e. the extent of the index space in use.
    [[nodiscard]] size_t slotCount() const noexcept { return m_slots.empty() ? 0U : m_slots.size() - 1U; }

    void clear();

    template<typename Func>
    void forEachEntity(Func&& func) const {
        for (size_t i = 0; i < m_slots.size(); ++i) {
            if (m_alive[i] != 0U) {
                func(m_slots[i]);
            }
        }
    }

    // ---- Components -------------------------------------------------------------------

    template<ComponentType T, typename... Args>
    T& emplace(EntityId e, Args&&... args) {
        requireValid(e);
        ComponentPool<T>& p = pool<T>();
        if (p.contains(e)) {
            throw std::logic_error("ecs::Registry::emplace: component already present");
        }
        T* c = p.emplace(e, std::forward<Args>(args)...);
        return *c;
    }

    template<ComponentType T, typename... Args>
    T& emplaceOrReplace(EntityId e, Args&&... args) {
        requireValid(e);
        ComponentPool<T>& p = pool<T>();
        if (T* existing = p.tryGet(e)) {
            *existing = makeComponent<T>(std::forward<Args>(args)...);
            return *existing;
        }
        return *p.emplace(e, std::forward<Args>(args)...);
    }

    template<ComponentType T>
    [[nodiscard]] T& get(EntityId e) {
        T* c = tryGet<T>(e);
        if (c == nullptr) {
            throw std::out_of_range("ecs::Registry::get: component missing");
        }
        return *c;
    }

    template<ComponentType T>
    [[nodiscard]] const T& get(EntityId e) const {
        const T* c = tryGet<T>(e);
        if (c == nullptr) {
            throw std::out_of_range("ecs::Registry::get: component missing");
        }
        return *c;
    }

    template<ComponentType T>
    [[nodiscard]] T* tryGet(EntityId e) noexcept {
        ComponentPool<T>* p = findPool<T>();
        return p == nullptr ? nullptr : p->tryGet(e);
    }

    template<ComponentType T>
    [[nodiscard]] const T* tryGet(EntityId e) const noexcept {
        const ComponentPool<T>* p = findPool<T>();
        return p == nullptr ? nullptr : p->tryGet(e);
    }

    template<ComponentType T>
    bool remove(EntityId e) {
        ComponentPool<T>* p = findPool<T>();
        return p != nullptr && p->remove(e);
    }

    template<ComponentType... Ts>
    [[nodiscard]] bool has(EntityId e) const noexcept {
        return (hasOne<Ts>(e) && ...);
    }

    template<ComponentType... Ts>
    [[nodiscard]] bool hasAny(EntityId e) const noexcept {
        return (hasOne<Ts>(e) || ...);
    }

    template<ComponentType T>
    [[nodiscard]] size_t count() const noexcept {
        const ComponentPool<T>* p = findPool<T>();
        return p == nullptr ? 0U : p->size();
    }

    // Pool access (created on demand). Exposed for views, groups and vectorised systems.
    template<ComponentType T>
    [[nodiscard]] ComponentPool<T>& pool() {
        const uint32_t id = detail::componentTypeId<T>();
        if (id >= m_pools.size()) {
            m_pools.resize(id + 1U);
        }
        if (!m_pools[id]) {
            m_pools[id] = std::make_unique<ComponentPool<T>>();
        }
        return static_cast<ComponentPool<T>&>(*m_pools[id]);
    }

    template<ComponentType T>
    [[nodiscard]] ComponentPool<T>* findPool() noexcept {
        const uint32_t id = detail::componentTypeId<T>();
        return id < m_pools.size() ? static_cast<ComponentPool<T>*>(m_pools[id].get()) : nullptr;
    }

    template<ComponentType T>
    [[nodiscard]] const ComponentPool<T>* findPool() const noexcept {
        const uint32_t id = detail::componentTypeId<T>();
        return id < m_pools.size() ? static_cast<const ComponentPool<T>*>(m_pools[id].get()) : nullptr;
    }

    // ---- Views ------------------------------------------------------------------------

    // Non-owning view over every entity that has all of Ts. Creates missing pools.
    template<ComponentType... Ts>
    [[nodiscard]] View<Ts...> view() {
        static_assert(sizeof...(Ts) > 0, "a view needs at least one component type");
        return View<Ts...>(pool<Ts>()...);
    }

    // ---- Owning groups ----------------------------------------------------------------

    // Returns the owning group over Ts, creating it (and packing the existing population) on
    // first use. The same type set in any order yields the same group. A component type can be
    // owned by a single group: requesting an overlapping group throws std::logic_error.
    template<ComponentType... Ts>
    [[nodiscard]] Group<Ts...> group() {
        static_assert(sizeof...(Ts) > 0, "a group needs at least one component type");
        static_assert(detail::allDistinct<Ts...>(), "a group cannot own a type twice");
        const std::vector<uint32_t> ids{detail::componentTypeId<Ts>()...};
        for (const auto& g : m_groups) {
            if (g->ownsExactly(ids)) {
                return Group<Ts...>(*g, pool<Ts>()...);
            }
        }
        std::vector<SparseSet*> owned{static_cast<SparseSet*>(&pool<Ts>())...};
        for (const SparseSet* p : owned) {
            if (p->observer() != nullptr) {
                throw std::logic_error("ecs::Registry::group: component already owned by another group");
            }
        }
        m_groups.push_back(std::make_unique<GroupHandler>(std::move(owned), ids));
        return Group<Ts...>(*m_groups.back(), pool<Ts>()...);
    }

    [[nodiscard]] size_t groupCount() const noexcept { return m_groups.size(); }

    // Roadmap name (Fiche 3 §2.2): synchronised, indirection-free iteration of a pair.
    template<ComponentType T1, ComponentType T2, typename Func>
    void forEachOwnedGroup(Func&& func) {
        group<T1, T2>().each(std::forward<Func>(func));
    }

    // ---- Roadmap API names (Fiche 3 §2.2) -------------------------------------------------

    [[nodiscard]] EntityId createEntity() { return create(); }
    void destroyEntity(EntityId e) { (void)destroy(e); }

    template<ComponentType T>
    void addComponent(EntityId e, T component) {
        (void)emplaceOrReplace<T>(e, std::move(component));
    }

    template<ComponentType T>
    [[nodiscard]] T* getComponent(EntityId e) noexcept { return tryGet<T>(e); }

    // Contiguous dense range of the whole pool for stride-1 iteration (pool's dense order).
    // Two such spans are index-aligned (dense_A[i] and dense_B[i] share an owner) only over the
    // first group<A, B>().size() entries, the owning group's packed prefix; prefer
    // Group::get<T>(), which returns exactly that aligned range.
    template<ComponentType T>
    [[nodiscard]] std::span<T> getDenseComponents() noexcept {
        ComponentPool<T>* p = findPool<T>();
        return p == nullptr ? std::span<T>{} : p->components();
    }

private:
    template<ComponentType T>
    [[nodiscard]] bool hasOne(EntityId e) const noexcept {
        const ComponentPool<T>* p = findPool<T>();
        return p != nullptr && p->contains(e);
    }

    template<typename T, typename... Args>
    [[nodiscard]] static T makeComponent(Args&&... args) {
        if constexpr (sizeof...(Args) == 0) {
            return T{};
        } else if constexpr (std::is_aggregate_v<T>) {
            return T{std::forward<Args>(args)...};
        } else {
            return T(std::forward<Args>(args)...);
        }
    }

    void requireValid(EntityId e) const {
        if (!valid(e)) {
            throw std::invalid_argument("ecs::Registry: invalid or stale entity");
        }
    }

    std::vector<EntityId> m_slots;      // current handle (index + generation) per slot
    std::vector<uint8_t> m_alive;       // 1 while the slot's handle is issued
    std::deque<uint32_t> m_freeIds;     // FIFO queue of recyclable slot indices
    std::vector<std::unique_ptr<SparseSet>> m_pools; // indexed by component type id
    // Declared after m_pools so groups are destroyed first (they detach from their pools).
    std::vector<std::unique_ptr<GroupHandler>> m_groups;
    size_t m_aliveCount{0};
    uint32_t m_capacity{0};
    uint32_t m_recycleThreshold{0};
};

} // namespace betacraft::ecs
