#include "world/DifferentialLightingEngine.hpp"
#include "world/World.hpp"
#include "world/Chunk.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>

namespace betacraft::world {

namespace {

constexpr std::array<glm::ivec3, 6> NEIGHBORS = {{
    { 0,  1,  0}, // +Y
    { 0, -1,  0}, // -Y
    { 0,  0,  1}, // +Z
    { 0,  0, -1}, // -Z
    { 1,  0,  0}, // +X
    {-1,  0,  0}  // -X
}};

constexpr std::array<glm::ivec3, 4> HORIZONTAL_NEIGHBORS = {{
    { 0, 0,  1},
    { 0, 0, -1},
    { 1, 0,  0},
    {-1, 0,  0}
}};

inline bool isVoxelInLoadedChunk(const World& world, int32_t x, int32_t z) noexcept {
    const core::ChunkCoord coord{
        core::floorDiv(x, Chunk::WIDTH),
        core::floorDiv(z, Chunk::DEPTH)
    };
    return world.getChunk(coord) != nullptr;
}

} // namespace

size_t DifferentialLightingEngine::computeDiscreteManhattanVolumeBFS(int maxRadius) {
    if (maxRadius < 0) return 0;
    std::queue<glm::ivec3> queue;
    std::unordered_map<int64_t, int> visitedDist;

    auto encodeKey = [](const glm::ivec3& p) -> int64_t {
        const int64_t x = static_cast<int64_t>(p.x + 100);
        const int64_t y = static_cast<int64_t>(p.y + 100);
        const int64_t z = static_cast<int64_t>(p.z + 100);
        return (x << 40) | (y << 20) | z;
    };

    const glm::ivec3 start(0, 0, 0);
    queue.push(start);
    visitedDist[encodeKey(start)] = 0;

    while (!queue.empty()) {
        const glm::ivec3 curr = queue.front();
        queue.pop();

        const int dist = visitedDist[encodeKey(curr)];
        if (dist >= maxRadius) continue;

        for (const auto& off : NEIGHBORS) {
            const glm::ivec3 next = curr + off;
            const int64_t key = encodeKey(next);
            if (visitedDist.find(key) == visitedDist.end()) {
                visitedDist[key] = dist + 1;
                queue.push(next);
            }
        }
    }

    return visitedDist.size();
}

void DifferentialLightingEngine::onBlockPlaced(const glm::ivec3& pos, BlockId newState) noexcept {
    const BlockDef& def = BlockRegistry::get(newState);

    // 1. If placed block emits light (e.g. Torch)
    if (def.lightEmission > 0) {
        updateLightSource(pos.x, pos.y, pos.z, def.lightEmission);
    }

    // 2. If placed block is opaque and does not emit light, extinguish existing block light
    if (def.lightOpacity > 0 && def.lightEmission == 0) {
        const uint8_t oldBlockLight = m_world.getBlockLight(pos.x, pos.y, pos.z);
        if (oldBlockLight > 0) {
            m_world.setBlockLight(pos.x, pos.y, pos.z, 0);
            m_removalQueue.push({pos, oldBlockLight});
            executeRemovalBFS();
        }
    }

    // 3. If placed block is opaque, obstruct sky light
    if (def.lightOpacity > 0) {
        const uint8_t oldSkyLight = m_world.getSkyLight(pos.x, pos.y, pos.z);
        if (oldSkyLight > 0) {
            m_world.setSkyLight(pos.x, pos.y, pos.z, 0);
            m_skyRemovalQueue.push({pos, oldSkyLight});
            if (def.lightOpacity >= 15) {
                // Pass 1: 1D vertical raycast extinction
                onSkylightColumnObstructed(pos.x, pos.z, pos.y - 1, 0);
            } else {
                executeSkyRemovalBFS();
            }
        }
    }
}

void DifferentialLightingEngine::onBlockDestroyed(const glm::ivec3& pos, BlockId oldState) noexcept {
    const BlockDef& oldDef = BlockRegistry::get(oldState);

    // 1. If destroyed block was emitting light (e.g. Torch broken)
    if (oldDef.lightEmission > 0) {
        updateLightSource(pos.x, pos.y, pos.z, 0);
    }

    // 2. If destroyed block was opaque, propagate incoming light from neighbors
    if (oldDef.lightOpacity > 0) {
        // Block light addition
        uint8_t maxNbBlock = 0;
        glm::ivec3 bestBlockNb = pos;
        for (const auto& off : NEIGHBORS) {
            const glm::ivec3 nb = pos + off;
            if (nb.y < 0 || nb.y >= Chunk::HEIGHT) continue;
            if (!isVoxelInLoadedChunk(m_world, nb.x, nb.z)) continue;
            const uint8_t l = m_world.getBlockLight(nb.x, nb.y, nb.z);
            if (l > maxNbBlock) {
                maxNbBlock = l;
                bestBlockNb = nb;
            }
        }
        if (maxNbBlock > 1) {
            m_additionQueue.push({bestBlockNb, maxNbBlock});
            executeAdditionBFS();
        }

        // Sky light check: check if directly open to sky
        bool openToSky = true;
        for (int32_t y = pos.y + 1; y < Chunk::HEIGHT; ++y) {
            if (BlockRegistry::isOpaque(m_world.getBlock(pos.x, y, pos.z))) {
                openToSky = false;
                break;
            }
        }

        if (openToSky) {
            // Restore vertical sky light column downward
            for (int32_t y = pos.y; y >= 0; --y) {
                if (BlockRegistry::isOpaque(m_world.getBlock(pos.x, y, pos.z))) {
                    break;
                }
                m_world.setSkyLight(pos.x, y, pos.z, 15);
                m_skyAdditionQueue.push({{pos.x, y, pos.z}, 15});
            }
            executeSkyAdditionBFS();
        } else {
            // Sky light from neighbors
            uint8_t maxNbSky = 0;
            glm::ivec3 bestSkyNb = pos;
            for (const auto& off : NEIGHBORS) {
                const glm::ivec3 nb = pos + off;
                if (nb.y < 0 || nb.y >= Chunk::HEIGHT) continue;
                if (!isVoxelInLoadedChunk(m_world, nb.x, nb.z)) continue;
                const uint8_t l = m_world.getSkyLight(nb.x, nb.y, nb.z);
                if (l > maxNbSky) {
                    maxNbSky = l;
                    bestSkyNb = nb;
                }
            }
            if (maxNbSky > 1) {
                m_skyAdditionQueue.push({bestSkyNb, maxNbSky});
                executeSkyAdditionBFS();
            }
        }
    }
}

void DifferentialLightingEngine::onSkylightColumnObstructed(int32_t wx, int32_t wz, int32_t yHigh, int32_t yLow) noexcept {
    if (yHigh < yLow) return;
    m_lastVoxelInspectionCount = 0;

    // Pass 1: 1D vertical raycast direct extinction O(Delta H) < 0.5 microseconds
    for (int32_t y = yHigh; y >= yLow; --y) {
        const BlockId block = m_world.getBlock(wx, y, wz);
        if (BlockRegistry::isOpaque(block)) {
            break; // Hit opaque surface
        }
        const uint8_t oldSky = m_world.getSkyLight(wx, y, wz);
        if (oldSky == 0) {
            break; // Already dark
        }
        m_world.setSkyLight(wx, y, wz, 0);
        ++m_lastVoxelInspectionCount;
        m_skyRemovalQueue.push({{wx, y, wz}, oldSky});
    }

    // Pass 3: Execute BFS propagation
    executeSkyRemovalBFS();
}

void DifferentialLightingEngine::updateLightSource(int32_t wx, int32_t wy, int32_t wz, uint8_t newEmission) noexcept {
    m_lastVoxelInspectionCount = 0;
    const uint8_t oldVal = m_world.getBlockLight(wx, wy, wz);

    if (newEmission > oldVal) {
        // Light addition
        m_world.setBlockLight(wx, wy, wz, newEmission);
        m_additionQueue.push({{wx, wy, wz}, newEmission});
        executeAdditionBFS();
    } else if (newEmission < oldVal) {
        // Light removal
        m_world.setBlockLight(wx, wy, wz, newEmission);
        m_removalQueue.push({{wx, wy, wz}, oldVal});
        executeRemovalBFS();

        if (newEmission > 0) {
            m_additionQueue.push({{wx, wy, wz}, newEmission});
            executeAdditionBFS();
        }
    }
}

void DifferentialLightingEngine::setBlockAndUpdateLighting(
    int32_t wx, int32_t wy, int32_t wz, BlockId newBlock, BlockId oldBlock
) noexcept {
    if (newBlock == oldBlock) return;

    if (newBlock == BlockId::Air) {
        onBlockDestroyed({wx, wy, wz}, oldBlock);
    } else {
        onBlockPlaced({wx, wy, wz}, newBlock);
    }
}

void DifferentialLightingEngine::setBlockAndUpdateLighting(
    World& world, int32_t wx, int32_t wy, int32_t wz, BlockId newBlock
) noexcept {
    const BlockId old = world.getBlock(wx, wy, wz);
    world.setBlock(wx, wy, wz, newBlock);
    DifferentialLightingEngine engine(world);
    engine.setBlockAndUpdateLighting(wx, wy, wz, newBlock, old);
}

void DifferentialLightingEngine::updateLightSource(
    World& world, int32_t wx, int32_t wy, int32_t wz, uint8_t newEmission
) noexcept {
    DifferentialLightingEngine engine(world);
    engine.updateLightSource(wx, wy, wz, newEmission);
}

void DifferentialLightingEngine::executeRemovalBFS() noexcept {
    while (!m_removalQueue.empty()) {
        const LightDeltaNode curr = m_removalQueue.front();
        m_removalQueue.pop();
        ++m_lastVoxelInspectionCount;

        for (const auto& off : NEIGHBORS) {
            const glm::ivec3 nb = curr.worldPos + off;
            if (nb.y < 0 || nb.y >= Chunk::HEIGHT) continue;
            if (!isVoxelInLoadedChunk(m_world, nb.x, nb.z)) continue;

            const uint8_t nbLight = m_world.getBlockLight(nb.x, nb.y, nb.z);
            if (nbLight != 0 && nbLight < curr.lightVal) {
                // Dependent neighbor: extinguish and propagate removal
                m_world.setBlockLight(nb.x, nb.y, nb.z, 0);
                m_removalQueue.push({nb, nbLight});
            } else if (nbLight >= curr.lightVal) {
                // Independent light source: reinject into addition queue
                m_additionQueue.push({nb, nbLight});
            }
        }
    }

    // Propagate light from all remaining independent sources
    executeAdditionBFS();
}

void DifferentialLightingEngine::executeAdditionBFS() noexcept {
    while (!m_additionQueue.empty()) {
        const LightDeltaNode curr = m_additionQueue.front();
        m_additionQueue.pop();
        ++m_lastVoxelInspectionCount;

        if (curr.lightVal <= 1) continue;

        for (const auto& off : NEIGHBORS) {
            const glm::ivec3 nb = curr.worldPos + off;
            if (nb.y < 0 || nb.y >= Chunk::HEIGHT) continue;
            if (!isVoxelInLoadedChunk(m_world, nb.x, nb.z)) continue;

            const BlockId nbBlock = m_world.getBlock(nb.x, nb.y, nb.z);
            const BlockDef& nbDef = BlockRegistry::get(nbBlock);
            if (nbDef.lightOpacity >= 15) continue;

            const int cost = std::max<int>(1, nbDef.lightOpacity);
            if (static_cast<int>(curr.lightVal) <= cost) continue;

            const uint8_t newVal = static_cast<uint8_t>(curr.lightVal - cost);
            const uint8_t curNbLight = m_world.getBlockLight(nb.x, nb.y, nb.z);

            if (newVal > curNbLight) {
                m_world.setBlockLight(nb.x, nb.y, nb.z, newVal);
                m_additionQueue.push({nb, newVal});
            }
        }
    }
}

void DifferentialLightingEngine::executeSkyRemovalBFS() noexcept {
    while (!m_skyRemovalQueue.empty()) {
        const LightDeltaNode curr = m_skyRemovalQueue.front();
        m_skyRemovalQueue.pop();
        ++m_lastVoxelInspectionCount;

        for (const auto& off : NEIGHBORS) {
            const glm::ivec3 nb = curr.worldPos + off;
            if (nb.y < 0 || nb.y >= Chunk::HEIGHT) continue;
            if (!isVoxelInLoadedChunk(m_world, nb.x, nb.z)) continue;

            const uint8_t nbSky = m_world.getSkyLight(nb.x, nb.y, nb.z);
            if (nbSky != 0 && nbSky < curr.lightVal) {
                m_world.setSkyLight(nb.x, nb.y, nb.z, 0);
                m_skyRemovalQueue.push({nb, nbSky});
            } else if (nbSky >= curr.lightVal) {
                m_skyAdditionQueue.push({nb, nbSky});
            }
        }
    }

    executeSkyAdditionBFS();
}

void DifferentialLightingEngine::executeSkyAdditionBFS() noexcept {
    while (!m_skyAdditionQueue.empty()) {
        const LightDeltaNode curr = m_skyAdditionQueue.front();
        m_skyAdditionQueue.pop();
        ++m_lastVoxelInspectionCount;

        if (curr.lightVal <= 1) continue;

        for (const auto& off : NEIGHBORS) {
            const glm::ivec3 nb = curr.worldPos + off;
            if (nb.y < 0 || nb.y >= Chunk::HEIGHT) continue;
            if (!isVoxelInLoadedChunk(m_world, nb.x, nb.z)) continue;

            const BlockId nbBlock = m_world.getBlock(nb.x, nb.y, nb.z);
            const BlockDef& nbDef = BlockRegistry::get(nbBlock);
            if (nbDef.lightOpacity >= 15) continue;

            const int cost = std::max<int>(1, nbDef.lightOpacity);
            if (static_cast<int>(curr.lightVal) <= cost) continue;

            const uint8_t newVal = static_cast<uint8_t>(curr.lightVal - cost);
            const uint8_t curNbSky = m_world.getSkyLight(nb.x, nb.y, nb.z);

            if (newVal > curNbSky) {
                m_world.setSkyLight(nb.x, nb.y, nb.z, newVal);
                m_skyAdditionQueue.push({nb, newVal});
            }
        }
    }
}

} // namespace betacraft::world
