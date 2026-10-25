#pragma once

#include "core/Types.hpp"
#include "render/ChunkMesher.hpp"
#include "voxel/Block.hpp"
#include "world/World.hpp"

#include <glm/glm.hpp>
#include <random>
#include <vector>

namespace betacraft::render {

struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec2 uvMin{0.0f};
    glm::vec2 uvMax{0.0f};
    glm::vec3 color{1.0f};
    float size{0.10f};
    float life{0.0f};
    float maxLife{0.6f};
};

class ParticleSystem {
public:
    ParticleSystem() noexcept;

    void spawnBlockBreakBurst(const world::World& world, core::BlockPos pos, voxel::BlockId blockId, int count = 24);
    void spawnMiningHitParticles(const world::World& world, core::BlockPos pos, core::FaceDir face, voxel::BlockId blockId, int count = 3);

    // Explosion: a puff of grey smoke particles thrown out of `centre`, wider for a larger
    // `power`.
    void spawnExplosionBurst(const glm::vec3& centre, float power, int count = 48);

    // Rain splash (Beta EntityRainFX): a few tiny water droplets bouncing off `at`, the
    // top of the block the rain hits.
    void spawnRainSplash(const glm::vec3& at, int count = 2);

    void update(float dt, const world::World& world) noexcept;

    [[nodiscard]] size_t particleCount() const noexcept { return m_particles.size(); }

    void buildBillboardVertices(
        const glm::vec3& cameraRight,
        const glm::vec3& cameraUp,
        std::vector<ChunkVertex>& outVertices
    ) const;

private:
    std::vector<Particle> m_particles;
    std::mt19937 m_rng;
};

} // namespace betacraft::render
