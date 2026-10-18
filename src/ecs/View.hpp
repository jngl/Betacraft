#pragma once

#include "ecs/ComponentPool.hpp"

#include <cstddef>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>

namespace betacraft::ecs {

// Non-owning view over the entities that have every component in Ts.
// Iteration walks the smallest pool (fewest candidates) and probes the others in O(1).
//
// each() iterates a snapshot of the leading pool's handles and re-checks membership when an
// entity is reached. In-place iteration is not enough: the callback may emplace a component
// that makes the entity join an owning group, and the group then swaps an unvisited entity
// into the visited slot (skipped) — likewise destroying another entity swap-and-pops an
// already-visited one back into the unvisited range (visited twice). With the snapshot, the
// SET of visited entities stays well-defined under any structural change: every entity
// matching at the start and still matching when reached is visited exactly once; entities
// that start matching mid-iteration are not visited. The copy is 4 bytes per candidate,
// negligible next to the per-entity probes; hot loops that must stay copy-free use a Group.
//
// The component REFERENCES passed to the callback are another matter: they point into the
// pools' vectors and are invalidated by any structural change to a pool they live in, made
// during the callback (an emplace may reallocate, a group join/leave swaps payloads, a destroy
// or remove swap-and-pops). After such a change, either stop using them and re-fetch with
// Registry::get/tryGet, or record the change in a CommandBuffer and flush it after the loop.
template<ComponentType... Ts>
class View {
    static_assert(sizeof...(Ts) > 0, "a view needs at least one component type");

public:
    explicit View(ComponentPool<Ts>&... pools) noexcept : m_pools(&pools...) {}

    // func(EntityId, Ts&...) or func(Ts&...).
    template<typename Func>
    void each(Func&& func) {
        const std::span<const EntityId> live = leadingPool()->entities();
        // reserve + insert instead of the iterator-pair constructor: same result, but avoids a
        // GCC 15 -Wfree-nonheap-object false positive on the span-iterator overload at -O2.
        std::vector<EntityId> snapshot;
        snapshot.reserve(live.size());
        snapshot.insert(snapshot.end(), live.data(), live.data() + live.size());
        for (const EntityId e : snapshot) {
            // Exact-handle check: also rejects a slot destroyed and recycled meanwhile.
            if (!contains(e)) {
                continue;
            }
            if constexpr (std::is_invocable_v<Func&, EntityId, Ts&...>) {
                func(e, std::get<ComponentPool<Ts>*>(m_pools)->get(e)...);
            } else {
                func(std::get<ComponentPool<Ts>*>(m_pools)->get(e)...);
            }
        }
    }

    [[nodiscard]] bool contains(EntityId e) const noexcept {
        return (std::get<ComponentPool<Ts>*>(m_pools)->contains(e) && ...);
    }

    // Upper bound on the number of matches: the size of the smallest pool.
    [[nodiscard]] size_t sizeHint() const noexcept { return leadingPool()->size(); }

    // Exact number of matches (O(sizeHint)).
    [[nodiscard]] size_t count() const noexcept {
        size_t n = 0;
        for (const EntityId e : leadingPool()->entities()) {
            n += contains(e) ? 1U : 0U;
        }
        return n;
    }

private:
    [[nodiscard]] const SparseSet* leadingPool() const noexcept {
        const SparseSet* best = nullptr;
        ((best = (best == nullptr || std::get<ComponentPool<Ts>*>(m_pools)->size() < best->size())
                     ? std::get<ComponentPool<Ts>*>(m_pools)
                     : best),
         ...);
        return best;
    }

    std::tuple<ComponentPool<Ts>*...> m_pools;
};

} // namespace betacraft::ecs
