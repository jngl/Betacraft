#pragma once

#include "ecs/Entity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace betacraft::ecs {

// Type-erased sparse set of entity handles: a packed dense array plus a paged sparse index
// (entity index -> dense position). Pages of PAGE_SIZE entries are allocated on demand so the
// memory cost is O(active pages) instead of O(max entity index) (Fiche 3 §4).
//
// Derived component pools keep a payload array parallel to the dense array; the virtual
// swapPayload/popPayload hooks let this base class perform swap-and-pop on both at once.
class SparseSet {
public:
    static constexpr size_t PAGE_SIZE = 4096;
    static constexpr uint32_t NPOS = 0xFFFFFFFFU;

    // Observes structural changes; used by owning groups to keep their packed prefix in sync.
    class Observer {
    public:
        virtual ~Observer() = default;
        virtual void onInserted(EntityId e) = 0;
        virtual void onRemoving(EntityId e) = 0;
        virtual void onCleared() noexcept = 0;
    };

    SparseSet() = default;
    virtual ~SparseSet() = default;

    SparseSet(const SparseSet&) = delete;
    SparseSet& operator=(const SparseSet&) = delete;
    SparseSet(SparseSet&&) noexcept = default;
    SparseSet& operator=(SparseSet&&) noexcept = default;

    [[nodiscard]] bool contains(EntityId e) const noexcept {
        const uint32_t pos = sparseLookup(entityIndex(e));
        return pos != NPOS && m_dense[pos] == e;
    }

    // Dense position of the exact handle, or NPOS when absent (stale generations included).
    [[nodiscard]] uint32_t index(EntityId e) const noexcept {
        const uint32_t pos = sparseLookup(entityIndex(e));
        return (pos != NPOS && m_dense[pos] == e) ? pos : NPOS;
    }

    [[nodiscard]] size_t size() const noexcept { return m_dense.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_dense.empty(); }
    [[nodiscard]] std::span<const EntityId> entities() const noexcept { return m_dense; }

    [[nodiscard]] size_t allocatedPages() const noexcept;

    // Appends e at the end of the dense array. Fails for NULL_ENTITY, when the slot is
    // already occupied by any generation of the same index, and on sets that carry a payload
    // (a handle without its component would make tryGet read past the payload array).
    bool insert(EntityId e);

    // Swap-and-pop removal; a stale generation never removes the current occupant.
    bool remove(EntityId e);

    void clear() noexcept;

    // True when insert(e) would succeed (valid handle and free slot).
    [[nodiscard]] bool canInsert(EntityId e) const noexcept {
        const uint32_t idx = entityIndex(e);
        return e != NULL_ENTITY && idx <= MAX_ENTITY_INDEX && sparseLookup(idx) == NPOS;
    }

    void setObserver(Observer* observer) noexcept { m_observer = observer; }
    [[nodiscard]] Observer* observer() const noexcept { return m_observer; }

protected:
    // Swaps two dense positions (entities and payload) keeping the sparse index coherent.
    // Not public: on a pool owned by a group, an outside reorder would silently break the
    // packed-prefix invariant. Only the owning-group bookkeeping (a friend) reorders.
    void swapPositions(size_t a, size_t b) noexcept;

    // False for derived sets whose entries need a payload constructed alongside the handle.
    [[nodiscard]] virtual bool acceptsBareInsert() const noexcept { return true; }

    virtual void swapPayload(size_t /*a*/, size_t /*b*/) noexcept {}
    virtual void popPayload() noexcept {}
    virtual void clearPayload() noexcept {}

    // For derived pools: inserts without notifying the observer, so they can construct the
    // payload first and only then announce the entity (observers may read the payload).
    bool insertSilently(EntityId e);
    void notifyInserted(EntityId e) {
        if (m_observer != nullptr) {
            m_observer->onInserted(e);
        }
    }

private:
    friend class GroupHandler;

    using Page = std::array<uint32_t, PAGE_SIZE>;

    [[nodiscard]] uint32_t sparseLookup(uint32_t idx) const noexcept {
        const size_t page = idx / PAGE_SIZE;
        if (page >= m_sparse.size() || !m_sparse[page]) {
            return NPOS;
        }
        return (*m_sparse[page])[idx % PAGE_SIZE];
    }

    uint32_t& sparseSlot(uint32_t idx);

    std::vector<EntityId> m_dense;
    std::vector<std::unique_ptr<Page>> m_sparse;
    Observer* m_observer{nullptr};
};

} // namespace betacraft::ecs
