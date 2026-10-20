#pragma once

#include <array>
#include <cmath>
#include <glm/glm.hpp>

namespace betacraft::math {

class Frustum {
public:
    void update(const glm::mat4& viewProj) noexcept {
        // Gribb-Hartmann plane extraction (column-major GLM matrix)
        // Left
        m_planes[0] = glm::vec4(
            viewProj[0][3] + viewProj[0][0],
            viewProj[1][3] + viewProj[1][0],
            viewProj[2][3] + viewProj[2][0],
            viewProj[3][3] + viewProj[3][0]
        );
        // Right
        m_planes[1] = glm::vec4(
            viewProj[0][3] - viewProj[0][0],
            viewProj[1][3] - viewProj[1][0],
            viewProj[2][3] - viewProj[2][0],
            viewProj[3][3] - viewProj[3][0]
        );
        // Bottom
        m_planes[2] = glm::vec4(
            viewProj[0][3] + viewProj[0][1],
            viewProj[1][3] + viewProj[1][1],
            viewProj[2][3] + viewProj[2][1],
            viewProj[3][3] + viewProj[3][1]
        );
        // Top
        m_planes[3] = glm::vec4(
            viewProj[0][3] - viewProj[0][1],
            viewProj[1][3] - viewProj[1][1],
            viewProj[2][3] - viewProj[2][1],
            viewProj[3][3] - viewProj[3][1]
        );
        // Near
        m_planes[4] = glm::vec4(
            viewProj[0][3] + viewProj[0][2],
            viewProj[1][3] + viewProj[1][2],
            viewProj[2][3] + viewProj[2][2],
            viewProj[3][3] + viewProj[3][2]
        );
        // Far
        m_planes[5] = glm::vec4(
            viewProj[0][3] - viewProj[0][2],
            viewProj[1][3] - viewProj[1][2],
            viewProj[2][3] - viewProj[2][2],
            viewProj[3][3] - viewProj[3][2]
        );

        for (auto& plane : m_planes) {
            const float len = glm::length(glm::vec3(plane));
            if (len > 1e-6f) {
                plane /= len;
            }
        }
    }

    [[nodiscard]] bool intersectsAABB(const glm::vec3& minCorner, const glm::vec3& maxCorner) const noexcept {
        for (const auto& plane : m_planes) {
            glm::vec3 positiveVertex = minCorner;
            if (plane.x >= 0.0f) positiveVertex.x = maxCorner.x;
            if (plane.y >= 0.0f) positiveVertex.y = maxCorner.y;
            if (plane.z >= 0.0f) positiveVertex.z = maxCorner.z;

            if (glm::dot(glm::vec3(plane), positiveVertex) + plane.w < 0.0f) {
                return false;
            }
        }
        return true;
    }

private:
    std::array<glm::vec4, 6> m_planes{};
};

} // namespace betacraft::math
