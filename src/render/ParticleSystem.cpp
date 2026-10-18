#include "render/ParticleSystem.hpp"
#include "render/TextureAtlas.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::render {

ParticleSystem::ParticleSystem() noexcept
    : m_rng(173173U) {
    m_particles.reserve(512);
}

void ParticleSystem::spawnBlockBreakBurst(const world::World& world, core::BlockPos pos, world::BlockId blockId, int count) {
    if (blockId == world::BlockId::Air) return;
    const auto& def = world::BlockRegistry::get(blockId);
    const glm::vec4 tileUV = TextureAtlas::getTileUV(def.tileForFace(core::FaceDir::PosZ));

    glm::vec3 tint(1.0f);
    if (def.tintGrassTop && def.tileForFace(core::FaceDir::PosZ) == 39) {
        tint = world.getGrassTint(pos.x, pos.z);
    } else if (def.tintFoliage) {
        tint = world.getFoliageTint(pos.x, pos.z);
    }

    std::uniform_real_distribution<float> offsetDist(0.15f, 0.85f);
    std::uniform_real_distribution<float> velDist(-2.4f, 2.4f);
    std::uniform_real_distribution<float> uvDist(0.0f, 0.7f);
    std::uniform_real_distribution<float> lifeDist(0.35f, 0.85f);

    for (int i = 0; i < count && m_particles.size() < 600; ++i) {
        const float uStart = uvDist(m_rng);
        const float vStart = uvDist(m_rng);
        const glm::vec2 uv0(
            glm::mix(tileUV.x, tileUV.z, uStart),
            glm::mix(tileUV.y, tileUV.w, vStart)
        );
        const glm::vec2 uv1(
            glm::mix(tileUV.x, tileUV.z, uStart + 0.25f),
            glm::mix(tileUV.y, tileUV.w, vStart + 0.25f)
        );

        const glm::vec3 p(
            static_cast<float>(pos.x) + offsetDist(m_rng),
            static_cast<float>(pos.y) + offsetDist(m_rng),
            static_cast<float>(pos.z) + offsetDist(m_rng)
        );

        Particle part{};
        part.position = p;
        part.velocity = glm::vec3(velDist(m_rng), std::abs(velDist(m_rng)) + 1.2f, velDist(m_rng));
        part.uvMin = uv0;
        part.uvMax = uv1;
        part.color = tint * 0.88f;
        part.size = 0.09f;
        part.life = 0.0f;
        part.maxLife = lifeDist(m_rng);
        m_particles.push_back(part);
    }
}

void ParticleSystem::spawnMiningHitParticles(const world::World& world, core::BlockPos pos, core::FaceDir face, world::BlockId blockId, int count) {
    if (blockId == world::BlockId::Air) return;
    const auto& def = world::BlockRegistry::get(blockId);
    const glm::vec4 tileUV = TextureAtlas::getTileUV(def.tileForFace(face));
    const glm::ivec3 n = core::faceNormal(face);

    std::uniform_real_distribution<float> spread(0.2f, 0.8f);
    std::uniform_real_distribution<float> uvDist(0.0f, 0.7f);

    for (int i = 0; i < count && m_particles.size() < 600; ++i) {
        glm::vec3 local(spread(m_rng), spread(m_rng), spread(m_rng));
        if (n.x != 0) local.x = (n.x > 0) ? 1.04f : -0.04f;
        if (n.y != 0) local.y = (n.y > 0) ? 1.04f : -0.04f;
        if (n.z != 0) local.z = (n.z > 0) ? 1.04f : -0.04f;

        const float uStart = uvDist(m_rng);
        const float vStart = uvDist(m_rng);

        Particle part{};
        part.position = glm::vec3(pos.x, pos.y, pos.z) + local;
        part.velocity = glm::vec3(n) * 1.4f + glm::vec3(spread(m_rng) - 0.5f, 1.2f, spread(m_rng) - 0.5f);
        part.uvMin = {glm::mix(tileUV.x, tileUV.z, uStart), glm::mix(tileUV.y, tileUV.w, vStart)};
        part.uvMax = {glm::mix(tileUV.x, tileUV.z, uStart + 0.25f), glm::mix(tileUV.y, tileUV.w, vStart + 0.25f)};
        part.color = (def.tintGrassTop && face == core::FaceDir::PosY)
            ? world.getGrassTint(pos.x, pos.z)
            : glm::vec3(0.88f);
        part.size = 0.075f;
        part.life = 0.0f;
        part.maxLife = 0.42f;
        m_particles.push_back(part);
    }
}

void ParticleSystem::spawnExplosionBurst(const glm::vec3& centre, float power, int count) {
    const glm::vec4 tileUV = TextureAtlas::getTileUV(
        world::BlockRegistry::get(world::BlockId::Wool).tileForFace(core::FaceDir::PosZ));
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> uvDist(0.0f, 0.7f);
    std::uniform_real_distribution<float> lifeDist(0.6f, 1.4f);
    std::uniform_real_distribution<float> greyDist(0.35f, 0.8f);
    const float spread = std::max(1.0f, power);
    for (int i = 0; i < count && m_particles.size() < 600; ++i) {
        const glm::vec3 offset(unit(m_rng), unit(m_rng), unit(m_rng));
        const float uStart = uvDist(m_rng);
        const float vStart = uvDist(m_rng);
        Particle part{};
        part.position = centre + offset * spread * 0.5f;
        part.velocity = offset * spread * 2.0f + glm::vec3(0.0f, 4.0f, 0.0f);
        part.uvMin = {glm::mix(tileUV.x, tileUV.z, uStart), glm::mix(tileUV.y, tileUV.w, vStart)};
        part.uvMax = {glm::mix(tileUV.x, tileUV.z, uStart + 0.25f), glm::mix(tileUV.y, tileUV.w, vStart + 0.25f)};
        part.color = glm::vec3(greyDist(m_rng));
        part.size = 0.22f;
        part.life = 0.0f;
        part.maxLife = lifeDist(m_rng);
        m_particles.push_back(part);
    }
}

void ParticleSystem::spawnRainSplash(const glm::vec3& at, int count) {
    const glm::vec4 tileUV = TextureAtlas::getTileUV(
        world::BlockRegistry::get(world::BlockId::Water).tileForFace(core::FaceDir::PosY));
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> uvDist(0.0f, 0.7f);
    std::uniform_real_distribution<float> lifeDist(0.15f, 0.35f);
    for (int i = 0; i < count && m_particles.size() < 600; ++i) {
        const float uStart = uvDist(m_rng);
        const float vStart = uvDist(m_rng);
        Particle part{};
        part.position = at + glm::vec3(unit(m_rng) * 0.1f, 0.05f, unit(m_rng) * 0.1f);
        part.velocity = glm::vec3(unit(m_rng) * 0.6f, 1.2f + 0.8f * std::abs(unit(m_rng)), unit(m_rng) * 0.6f);
        part.uvMin = {glm::mix(tileUV.x, tileUV.z, uStart), glm::mix(tileUV.y, tileUV.w, vStart)};
        part.uvMax = {glm::mix(tileUV.x, tileUV.z, uStart + 0.2f), glm::mix(tileUV.y, tileUV.w, vStart + 0.2f)};
        part.color = glm::vec3(0.55f, 0.65f, 0.95f);
        part.size = 0.035f;
        part.life = 0.0f;
        part.maxLife = lifeDist(m_rng);
        m_particles.push_back(part);
    }
}

void ParticleSystem::update(float dt, const world::World& world) noexcept {
    for (size_t i = 0; i < m_particles.size();) {
        auto& p = m_particles[i];
        p.life += dt;
        if (p.life >= p.maxLife) {
            m_particles[i] = m_particles.back();
            m_particles.pop_back();
            continue;
        }

        p.velocity.y -= 18.0f * dt;
        const glm::vec3 nextPos = p.position + p.velocity * dt;
        const int32_t bx = static_cast<int32_t>(std::floor(nextPos.x));
        const int32_t by = static_cast<int32_t>(std::floor(nextPos.y));
        const int32_t bz = static_cast<int32_t>(std::floor(nextPos.z));

        if (world::BlockRegistry::isSolid(world.getBlock(bx, by, bz))) {
            p.velocity.y = 0.0f;
            p.velocity.x *= 0.6f;
            p.velocity.z *= 0.6f;
        } else {
            p.position = nextPos;
        }
        ++i;
    }
}

void ParticleSystem::buildBillboardVertices(
    const glm::vec3& cameraRight,
    const glm::vec3& cameraUp,
    std::vector<ChunkVertex>& outVertices
) const {
    outVertices.clear();
    outVertices.reserve(m_particles.size() * 6);

    for (const auto& p : m_particles) {
        const glm::vec3 r = cameraRight * p.size;
        const glm::vec3 u = cameraUp * p.size;
        const glm::vec2 light(0.95f, 0.2f);

        const ChunkVertex v0{p.position - r - u, {p.uvMin.x, p.uvMax.y}, p.color, light};
        const ChunkVertex v1{p.position + r - u, {p.uvMax.x, p.uvMax.y}, p.color, light};
        const ChunkVertex v2{p.position + r + u, {p.uvMax.x, p.uvMin.y}, p.color, light};
        const ChunkVertex v3{p.position - r + u, {p.uvMin.x, p.uvMin.y}, p.color, light};

        outVertices.push_back(v0);
        outVertices.push_back(v1);
        outVertices.push_back(v2);
        outVertices.push_back(v0);
        outVertices.push_back(v2);
        outVertices.push_back(v3);
    }
}

} // namespace betacraft::render
