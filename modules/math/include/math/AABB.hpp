#pragma once

#include <glm/glm.hpp>

namespace betacraft::math {

struct AABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    [[nodiscard]] constexpr bool intersects(const AABB& other) const noexcept {
        return (min.x < other.max.x && max.x > other.min.x) &&
               (min.y < other.max.y && max.y > other.min.y) &&
               (min.z < other.max.z && max.z > other.min.z);
    }

    [[nodiscard]] constexpr AABB translated(const glm::vec3& delta) const noexcept {
        return {min + delta, max + delta};
    }

    [[nodiscard]] constexpr AABB expanded(const glm::vec3& amount) const noexcept {
        return {min - amount, max + amount};
    }
};

} // namespace betacraft::math
