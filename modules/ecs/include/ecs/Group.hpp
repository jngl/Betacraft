#pragma once

#include "ecs/ComponentPool.hpp"
#include "ecs/SparseSet.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>

namespace betacraft::ecs {

// Bookkeeping for one owning group ("owning pools", EnTT::group-like).
//
// Invariant: the first size() dense slots of every owned pool hold exactly the entities that
// own all the group's components, in the same order. Hence dense_A[i] and dense_B[i] belong
// to the same entity and systems iterate them stride-1 without any sparse lookup.
//
// The handler observes each owned pool. When an entity completes the set it is swapped to
// position size() in every owned pool and the prefix grows; just before an entity loses an
// owned component it is swapped to the prefix's last slot in every pool and the prefix
// shrinks, so the pool's own swap-and-pop never touches the packed region.
//
// It is type-erased (SparseSet::swapPositions moves the payload) so it lives in a .cpp.
class GroupHandler final : public SparseSet::Observer {
public:
    GroupHandler(std::vector<SparseSet*> pools, std::vector<uint32_t> typeIds);
    ~GroupHandler() override;

    GroupHandler(const GroupHandler&) = delete;
    GroupHandler& operator=(const GroupHandler&) = delete;
    GroupHandler(GroupHandler&&) = delete;
    GroupHandler& operator=(GroupHandler&&) = delete;

    [[nodiscard]] size_t size() const noexcept { return m_size; }
    [[nodiscard]] bool contains(EntityId e) const noexcept {
        const uint32_t pos = m_pools.front()->index(e);
        return pos != SparseSet::NPOS && pos < m_size;
    }

    // True when the group owns exactly this set of component types (order-insensitive).
    [[nodiscard]] bool ownsExactly(std::span<const uint32_t> typeIds) const noexcept;

    void onInserted(EntityId e) override;
    void onRemoving(EntityId e) override;
    void onCleared() noexcept override { m_size = 0; }

private:
    [[nodiscard]] bool ownsAll(EntityId e) const noexcept;
    void pack(EntityId e) noexcept;

    std::vector<SparseSet*> m_pools;
    std::vector<uint32_t> m_typeIds; // sorted
    size_t m_size{0};
};

// Lightweight, copyable handle onto an owning group. Valid while its Registry is alive.
template<ComponentType... Ts>
class Group {
    static_assert(sizeof...(Ts) > 0, "a group needs at least one component type");

public:
    Group(const GroupHandler& handler, ComponentPool<Ts>&... pools) noexcept
        : m_handler(&handler), m_pools(&pools...) {}

    [[nodiscard]] size_t size() const noexcept { return m_handler->size(); }
    [[nodiscard]] bool empty() const noexcept { return m_handler->size() == 0; }
    [[nodiscard]] bool contains(EntityId e) const noexcept { return m_handler->contains(e); }

    // Group members in packed order.
    [[nodiscard]] std::span<const EntityId> entities() const noexcept {
        return std::get<0>(m_pools)->entities().first(size());
    }

    // Packed, index-aligned component range: get<A>()[i] and get<B>()[i] share an owner.
    template<ComponentType T>
    [[nodiscard]] std::span<T> get() const noexcept {
        return std::get<ComponentPool<T>*>(m_pools)->components().first(size());
    }

    // func(EntityId, Ts&...) or func(Ts&...). Runs back-to-front over the packed prefix with no
    // copy (this is the hot path). Visiting stays exact if the callback changes or destroys the
    // visited entity (the prefix shrinks over already-visited slots) or adds components to any
    // entity (joiners are packed past the visited range and are not visited). Removing an owned
    // component from, or destroying, ANOTHER member is unsupported. In every case the Ts&
    // received are invalid once the callback has made a structural change to their pools
    // (reallocation, group swaps, swap-and-pop): re-fetch them, or better, record changes in a
    // CommandBuffer and flush it after the loop (as HealthSystem::update's death list does).
    template<typename Func>
    void each(Func&& func) const {
        for (size_t i = size(); i-- > 0;) {
            if (i >= size()) {
                continue;
            }
            if constexpr (std::is_invocable_v<Func&, EntityId, Ts&...>) {
                const EntityId e = std::get<0>(m_pools)->entities()[i];
                func(e, std::get<ComponentPool<Ts>*>(m_pools)->components()[i]...);
            } else {
                func(std::get<ComponentPool<Ts>*>(m_pools)->components()[i]...);
            }
        }
    }

private:
    const GroupHandler* m_handler;
    std::tuple<ComponentPool<Ts>*...> m_pools;
};

} // namespace betacraft::ecs
