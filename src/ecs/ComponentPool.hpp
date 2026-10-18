#pragma once

#include "ecs/SparseSet.hpp"

#include <cassert>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace betacraft::ecs {

// Components are plain data stored contiguously (Fiche 3 §2.2). Nothrow moves are required
// because swap-and-pop must never fail half-way and desynchronise the dense arrays.
template<typename T>
concept ComponentType = std::is_standard_layout_v<T> && !std::is_pointer_v<T> &&
                        std::is_nothrow_move_constructible_v<T> &&
                        std::is_nothrow_move_assignable_v<T>;

// Sparse set with a payload array: m_components[i] always belongs to entities()[i].
template<ComponentType T>
class ComponentPool final : public SparseSet {
public:
    ComponentPool() = default;

    // Constructs the component in place. Returns nullptr (pool unchanged) when the entity is
    // invalid or already owns a T. A throwing constructor also leaves the pool unchanged.
    template<typename... Args>
    T* emplace(EntityId e, Args&&... args) {
        if (!canInsert(e)) {
            return nullptr;
        }
        if constexpr (sizeof...(Args) == 0) {
            m_components.emplace_back();
        } else if constexpr (std::is_aggregate_v<T>) {
            m_components.push_back(T{std::forward<Args>(args)...});
        } else {
            m_components.emplace_back(std::forward<Args>(args)...);
        }
        try {
            insertSilently(e);
        } catch (...) {
            m_components.pop_back();
            throw;
        }
        // The observer (owning group) may move the new element, so re-resolve it afterwards.
        notifyInserted(e);
        return &m_components[index(e)];
    }

    [[nodiscard]] T* tryGet(EntityId e) noexcept {
        const uint32_t pos = index(e);
        return pos == NPOS ? nullptr : &m_components[pos];
    }

    [[nodiscard]] const T* tryGet(EntityId e) const noexcept {
        const uint32_t pos = index(e);
        return pos == NPOS ? nullptr : &m_components[pos];
    }

    // Unchecked fast path: the caller guarantees contains(e).
    [[nodiscard]] T& get(EntityId e) noexcept {
        assert(contains(e));
        return m_components[index(e)];
    }

    [[nodiscard]] const T& get(EntityId e) const noexcept {
        assert(contains(e));
        return m_components[index(e)];
    }

    [[nodiscard]] std::span<T> components() noexcept { return m_components; }
    [[nodiscard]] std::span<const T> components() const noexcept { return m_components; }

    // Entities enter a pool only together with their component (emplace).
    bool insert(EntityId e) = delete;

protected:
    [[nodiscard]] bool acceptsBareInsert() const noexcept override { return false; }

    void swapPayload(size_t a, size_t b) noexcept override {
        using std::swap;
        swap(m_components[a], m_components[b]);
    }

    void popPayload() noexcept override { m_components.pop_back(); }
    void clearPayload() noexcept override { m_components.clear(); }

private:
    std::vector<T> m_components;
};

} // namespace betacraft::ecs
