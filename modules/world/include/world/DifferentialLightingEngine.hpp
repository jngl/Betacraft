#pragma once

#include "core/Types.hpp"
#include "voxel/Block.hpp"

#include <cstdint>
#include <cstddef>
#include <queue>
#include <unordered_map>
#include <glm/glm.hpp>

namespace betacraft::world {

class World;

struct LightDeltaNode {
    glm::ivec3 worldPos{0};
    uint8_t lightVal{0};
};

class DifferentialLightingEngine {
public:
    explicit DifferentialLightingEngine(World& world) noexcept : m_world(world) {}

    // Event hooks for block placing and breaking
    void onBlockPlaced(const glm::ivec3& pos, voxel::BlockId newState) noexcept;
    void onBlockDestroyed(const glm::ivec3& pos, voxel::BlockId oldState) noexcept;

    // 3-pass hybrid skylight extinction for obstructed vertical solar column
    void onSkylightColumnObstructed(int32_t wx, int32_t wz, int32_t yHigh, int32_t yLow) noexcept;

    // Direct interface for lighting source update and block change
    void updateLightSource(int32_t wx, int32_t wy, int32_t wz, uint8_t newEmission) noexcept;
    void setBlockAndUpdateLighting(int32_t wx, int32_t wy, int32_t wz, voxel::BlockId newBlock, voxel::BlockId oldBlock = voxel::BlockId::Air) noexcept;

    // Static convenience helpers
    static void setBlockAndUpdateLighting(World& world, int32_t wx, int32_t wy, int32_t wz, voxel::BlockId newBlock) noexcept;
    static void updateLightSource(World& world, int32_t wx, int32_t wy, int32_t wz, uint8_t newEmission) noexcept;

    // Mathematical analytical bounds
    [[nodiscard]] static constexpr size_t analyticalManhattanShell(size_t r) noexcept {
        if (r == 0) return 1;
        return 4 * r * r + 2;
    }

    [[nodiscard]] static constexpr size_t analyticalManhattanVolume(size_t R) noexcept {
        return ((2 * R + 1) * (2 * R * R + 2 * R + 3)) / 3;
    }

    // Discrete 3D lattice BFS verification
    [[nodiscard]] static size_t computeDiscreteManhattanVolumeBFS(int maxRadius);

    [[nodiscard]] size_t getLastVoxelInspectionCount() const noexcept {
        return m_lastVoxelInspectionCount;
    }

private:
    void executeRemovalBFS() noexcept;
    void executeAdditionBFS() noexcept;

    void executeSkyRemovalBFS() noexcept;
    void executeSkyAdditionBFS() noexcept;

    World& m_world;
    std::queue<LightDeltaNode> m_removalQueue;
    std::queue<LightDeltaNode> m_additionQueue;

    std::queue<LightDeltaNode> m_skyRemovalQueue;
    std::queue<LightDeltaNode> m_skyAdditionQueue;

    size_t m_lastVoxelInspectionCount{0};
};

} // namespace betacraft::world
