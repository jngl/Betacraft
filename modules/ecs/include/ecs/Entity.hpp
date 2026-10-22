#pragma once

#include <cstdint>

namespace betacraft::ecs {

// A scalar 32-bit handle: low 24 bits index the entity slot, high 8 bits carry the slot's
// generation. Bumping the generation on destroy lets every stale copy of a handle be
// detected in O(1) once its slot has been recycled (roadmap P2.1).
using EntityId = uint32_t;

inline constexpr uint32_t ENTITY_INDEX_BITS = 24U;
inline constexpr uint32_t ENTITY_GENERATION_BITS = 8U;
inline constexpr uint32_t ENTITY_INDEX_MASK = (1U << ENTITY_INDEX_BITS) - 1U;
inline constexpr uint32_t ENTITY_GENERATION_MASK = (1U << ENTITY_GENERATION_BITS) - 1U;

inline constexpr EntityId NULL_ENTITY = 0xFFFFFFFFU;

// Index 0xFFFFFF is reserved so that no live handle can ever compare equal to NULL_ENTITY.
inline constexpr uint32_t MAX_ENTITY_INDEX = ENTITY_INDEX_MASK - 1U;

[[nodiscard]] constexpr EntityId makeEntity(uint32_t index, uint32_t generation) noexcept {
    return (index & ENTITY_INDEX_MASK) | ((generation & ENTITY_GENERATION_MASK) << ENTITY_INDEX_BITS);
}

[[nodiscard]] constexpr uint32_t entityIndex(EntityId e) noexcept {
    return e & ENTITY_INDEX_MASK;
}

[[nodiscard]] constexpr uint32_t entityGeneration(EntityId e) noexcept {
    return (e >> ENTITY_INDEX_BITS) & ENTITY_GENERATION_MASK;
}

} // namespace betacraft::ecs
