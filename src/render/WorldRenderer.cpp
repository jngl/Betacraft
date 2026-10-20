#include "render/WorldRenderer.hpp"
#include "render/ItemAppearance.hpp"
#include "core/Logger.hpp"
#include "lighting/DynamicLightsGlsl.hpp"
#include "math/AABB.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace betacraft::render {

size_t countDistinctColumns(std::vector<core::ChunkCoord>& columns) {
    std::sort(columns.begin(), columns.end(), [](const core::ChunkCoord& a, const core::ChunkCoord& b) {
        return a.x != b.x ? a.x < b.x : a.z < b.z;
    });
    columns.erase(std::unique(columns.begin(), columns.end()), columns.end());
    return columns.size();
}

namespace {

constexpr const char* DYNAMIC_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec2 aLight;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec3 uCameraPos;

out vec2 vUV;
out vec3 vColor;
out vec2 vLight;
out float vCamDist;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vUV = aUV;
    vColor = aColor;
    vLight = aLight;
    vCamDist = length(worldPos.xyz - uCameraPos);
    gl_Position = uViewProj * worldPos;
}
)";

constexpr const char* DYNAMIC_FS = R"(#version 330 core
in vec2 vUV;
in vec3 vColor;
in vec2 vLight;
in float vCamDist;

uniform sampler2D uAtlas;
uniform float uSunBrightness;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform int uAlphaCutout;

out vec4 FragColor;

void main() {
    vec4 tex = texture(uAtlas, vUV);
    if (uAlphaCutout == 1 && tex.a < 0.35) {
        discard;
    }

    // Beta 1.7.3 light curve combining modulated sky light and warm torch light
    float skyL = pow(clamp(vLight.x * uSunBrightness, 0.05, 1.0), 1.35);
    float blkL = pow(clamp(vLight.y, 0.0, 1.0), 1.25);
    float combinedLight = max(skyL, blkL);

    vec3 warmTorchTint = vec3(1.0, 0.94, 0.82) * (blkL * 0.25);
    vec3 litColor = tex.rgb * vColor * combinedLight + tex.rgb * warmTorchTint;

    float fogFactor = clamp((uFogEnd - vCamDist) / max(1.0, (uFogEnd - uFogStart)), 0.0, 1.0);
    vec3 finalRgb = mix(uFogColor, litColor, fogFactor);

    FragColor = vec4(finalRgb, tex.a);
}
)";

constexpr const char* PACKED_CHUNK_VS = R"(#version 330 core
layout(location = 0) in uvec2 a_packedData;

uniform mat4 u_viewProjection;
uniform ivec3 u_sectionOriginWorld;
uniform vec3 uCameraPos;

out vec3 v_planarPos;
out vec3 v_normal;
out vec2 v_uv;
flat out uint v_matLayer;
flat out uint v_normIdx;
out vec2 v_light;
out float v_ao;
flat out uint v_biomeTint;
out float v_camDist;
out vec3 v_worldPos;

const vec3 NORMALS[7] = vec3[7](
    vec3( 0.0,  1.0,  0.0), // 0: +Y
    vec3( 0.0, -1.0,  0.0), // 1: -Y
    vec3( 0.0,  0.0,  1.0), // 2: +Z
    vec3( 0.0,  0.0, -1.0), // 3: -Z
    vec3( 1.0,  0.0,  0.0), // 4: +X
    vec3(-1.0,  0.0,  0.0), // 5: -X
    vec3( 0.0,  1.0,  0.0)  // 6: Cross Flora
);

const float AO_LUT[4] = float[4](0.50, 0.66, 0.82, 1.00);

const vec2 UV_CORNERS[4] = vec2[4](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    uint w0 = a_packedData.x;
    uint w1 = a_packedData.y;

    float lx = float(w0 & 0x1Fu);
    float ly = float((w0 >> 5u) & 0x1Fu);
    float lz = float((w0 >> 10u) & 0x1Fu);
    uint normIdx = (w0 >> 15u) & 0x07u;
    uint aoIdx   = (w0 >> 18u) & 0x03u;
    uint uvIdx   = (w0 >> 20u) & 0x03u;
    v_matLayer   = (w0 >> 22u) & 0xFFu; // 256 atlas tiles; bits 30..31 carry fine Z

    float skyL = float(w1 & 0x0Fu) / 15.0;
    float blkL = float((w1 >> 4u) & 0x0Fu) / 15.0;
    v_biomeTint = (w1 >> 8u) & 0xFFu;
    uint animFrame = (w1 >> 16u) & 0x1Fu;
    bool sway      = bool((w1 >> 21u) & 0x01u);
    float secY     = float((w1 >> 23u) & 0x07u);

    vec3 localPos = vec3(lx, ly + secY * 16.0, lz);
    // S5.4 fine offsets (PackedChunkVertex::setFineOffset): sixteenths below the integer grid.
    uint fineX = (w1 >> 26u) & 0xFu;
    uint fineY = sway ? 0u : animFrame & 0xFu;
    uint fineZ = ((w1 >> 30u) & 0x3u) | (((w0 >> 30u) & 0x3u) << 2u);
    localPos -= vec3(float(fineX), float(fineY), float(fineZ)) / 16.0;
    vec3 worldPos = localPos + vec3(u_sectionOriginWorld);

    if (sway) {
        worldPos.x += sin(worldPos.y * 2.0 + worldPos.z + float(animFrame) * 0.2) * 0.06;
    }

    v_worldPos = worldPos;
    v_normIdx = normIdx;
    v_normal = NORMALS[min(normIdx, 6u)];
    v_ao = AO_LUT[aoIdx];
    v_light = vec2(skyL, blkL);

    if (normIdx == 6u) {
        v_planarPos = vec3(UV_CORNERS[uvIdx], 0.0);
    } else if (normIdx <= 1u) {
        v_planarPos = vec3(worldPos.x, worldPos.z, 0.0);
    } else if (normIdx >= 4u) {
        v_planarPos = vec3(worldPos.z, worldPos.y, 0.0);
    } else {
        v_planarPos = vec3(worldPos.x, worldPos.y, 0.0);
    }
    v_uv = UV_CORNERS[uvIdx];

    v_camDist = length(worldPos.xyz - uCameraPos);
    gl_Position = u_viewProjection * vec4(worldPos, 1.0);
}
)";

constexpr const char* PACKED_CHUNK_FS = R"(#version 330 core
in vec3 v_planarPos;
in vec3 v_normal;
in vec2 v_uv;
flat in uint v_matLayer;
flat in uint v_normIdx;
in vec2 v_light;
in float v_ao;
flat in uint v_biomeTint;
in float v_camDist;
in vec3 v_worldPos;

uniform sampler2D uAtlas;
uniform float uSunBrightness;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform int uAlphaCutout;

out vec4 FragColor;

float faceDirectionalShade(vec3 normal) {
    if (normal.y > 0.5) return 1.00;
    if (normal.y < -0.5) return 0.50;
    if (abs(normal.z) > 0.5) return 0.80;
    return 0.60;
}

void main() {
    // For cross-billboard flora (v_normIdx == 6u), use direct quad UVs.
    // For vertical voxel block faces (v_normIdx >= 2u), invert Y so texture top matches world top.
    // For horizontal voxel block faces (v_normIdx <= 1u), use standard fractal repeating coordinates.
    vec2 texCoords;
    if (v_normIdx == 6u) {
        texCoords = clamp(v_uv, 0.001, 0.999);
    } else if (v_normIdx >= 2u) {
        texCoords = vec2(fract(v_planarPos.x), 1.0 - fract(v_planarPos.y));
    } else {
        texCoords = fract(v_planarPos.xy);
    }

    float tileX = float(v_matLayer % 16u) / 16.0;
    float tileY = float(v_matLayer / 16u) / 16.0;
    vec2 atlasUv = vec2(tileX + texCoords.x / 16.0, tileY + texCoords.y / 16.0);

    vec4 tex = texture(uAtlas, atlasUv);
    if (uAlphaCutout == 1 && tex.a < 0.35) {
        discard;
    }

    float dirShade = faceDirectionalShade(v_normal);
    vec3 shadeColor = vec3(dirShade * v_ao);

    // Biome grass/foliage tinting modulation if active
    if (v_biomeTint == 160u) {
        // Foliage (Leaves, vines, etc.): tint all 6 faces
        vec3 foliageTint = vec3(0.45, 0.72, 0.27);
        tex.rgb *= foliageTint;
    } else if (v_biomeTint == 128u) {
        // Grass top or cross-billboard flora: tint top face or flora
        vec3 grassTint = vec3(0.55, 0.75, 0.35);
        if (v_normal.y > 0.5 || v_normIdx == 6u) {
            tex.rgb *= grassTint;
        }
    } else if (v_biomeTint < 250u) {
        vec3 foliageTint = vec3(0.45, 0.72, 0.27);
        tex.rgb *= foliageTint;
    }

    float skyL = pow(clamp(v_light.x * uSunBrightness, 0.05, 1.0), 1.35);
    float blkL = pow(clamp(v_light.y, 0.0, 1.0), 1.25);

    // Held torch / dropped glowstone (dynamic_lights.glsl, injected at compile time): taken
    // as max with the baked block light so a held torch reads like a placed one and never
    // over-brightens a lit room. The emitter tints match the warm tint below.
    vec3 dynLight = min(computeDynamicLight(v_worldPos, gl_FragCoord.xy), vec3(1.0));
    blkL = max(blkL, max(dynLight.r, max(dynLight.g, dynLight.b)));
    float combinedLight = max(skyL, blkL);

    vec3 warmTorchTint = vec3(1.0, 0.94, 0.82) * (blkL * 0.25);
    vec3 litColor = tex.rgb * shadeColor * combinedLight + tex.rgb * warmTorchTint;

    float fogFactor = clamp((uFogEnd - v_camDist) / max(1.0, (uFogEnd - uFogStart)), 0.0, 1.0);
    vec3 finalRgb = mix(uFogColor, litColor, fogFactor);

    FragColor = vec4(finalRgb, tex.a);
}
)";

constexpr const char* LINE_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

constexpr const char* LINE_FS = R"(#version 330 core
uniform vec4 uColor;
out vec4 FragColor;
void main() {
    FragColor = uColor;
}
)";

void pushCubeFaces(
    std::vector<ChunkVertex>& verts,
    const glm::vec3& minP,
    const glm::vec3& maxP,
    const std::array<uint8_t, 6>& tiles,
    const glm::vec3& topTint,
    const glm::vec2& light
) {
    auto emitFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, uint8_t tile, glm::vec3 col) {
        const glm::vec4 uv = TextureAtlas::getTileUV(tile);
        verts.push_back({a, {uv.x, uv.w}, col, light});
        verts.push_back({b, {uv.z, uv.w}, col, light});
        verts.push_back({c, {uv.z, uv.y}, col, light});
        verts.push_back({a, {uv.x, uv.w}, col, light});
        verts.push_back({c, {uv.z, uv.y}, col, light});
        verts.push_back({d, {uv.x, uv.y}, col, light});
    };

    // PosX, NegX, PosY, NegY, PosZ, NegZ
    emitFace({maxP.x, minP.y, maxP.z}, {maxP.x, minP.y, minP.z}, {maxP.x, maxP.y, minP.z}, {maxP.x, maxP.y, maxP.z}, tiles[0], glm::vec3(0.65f));
    emitFace({minP.x, minP.y, minP.z}, {minP.x, minP.y, maxP.z}, {minP.x, maxP.y, maxP.z}, {minP.x, maxP.y, minP.z}, tiles[1], glm::vec3(0.65f));
    emitFace({minP.x, maxP.y, maxP.z}, {maxP.x, maxP.y, maxP.z}, {maxP.x, maxP.y, minP.z}, {minP.x, maxP.y, minP.z}, tiles[2], topTint);
    emitFace({minP.x, minP.y, minP.z}, {maxP.x, minP.y, minP.z}, {maxP.x, minP.y, maxP.z}, {minP.x, minP.y, maxP.z}, tiles[3], glm::vec3(0.50f));
    emitFace({minP.x, minP.y, maxP.z}, {maxP.x, minP.y, maxP.z}, {maxP.x, maxP.y, maxP.z}, {minP.x, maxP.y, maxP.z}, tiles[4], glm::vec3(0.85f));
    emitFace({maxP.x, minP.y, minP.z}, {minP.x, minP.y, minP.z}, {maxP.x, maxP.y, minP.z}, {minP.x, maxP.y, minP.z}, tiles[5], glm::vec3(0.85f));
}

// Cube for blocks, thin card for items, tinted as a whole (pushCubeFaces only tints the top).
void pushItemModel(
    std::vector<ChunkVertex>& verts,
    const ItemAppearance& look,
    float halfSize,
    const glm::vec3& topTint,
    const glm::vec2& light
) {
    const size_t first = verts.size();
    const float halfDepth = look.flat ? halfSize * 0.14f : halfSize;
    const bool slab = look.block && world::BlockRegistry::get(*look.block).shape == world::RenderShape::Slab;
    pushCubeFaces(
        verts,
        glm::vec3(-halfSize, -halfSize, -halfDepth),
        glm::vec3(halfSize, slab ? 0.0f : halfSize, halfDepth),
        look.faceTiles,
        topTint,
        light
    );
    for (size_t i = first; i < verts.size(); ++i) {
        verts[i].color *= look.tint;
    }
}

// Beta RenderItem: dropped blocks are quarter-size cubes, items larger cards; both spin about
// Y one radian per second and bob 0..0.2 blocks.
void appendItemEntity(std::vector<ChunkVertex>& out, const world::World& world, const ItemEntityInstance& item) {
    const ItemAppearance look = itemAppearance(item.id);
    const float half = look.flat ? 0.2f : 0.125f;
    const glm::vec3 centre = item.position +
        glm::vec3(0.0f, half + std::sin(item.ageSeconds * 2.0f + item.phase) * 0.1f + 0.1f, 0.0f);
    const auto cx = static_cast<int32_t>(std::floor(centre.x));
    const auto cy = static_cast<int32_t>(std::floor(centre.y));
    const auto cz = static_cast<int32_t>(std::floor(centre.z));
    const glm::vec2 light(static_cast<float>(world.getSkyLight(cx, cy, cz)) / 15.0f,
                          static_cast<float>(world.getBlockLight(cx, cy, cz)) / 15.0f);
    glm::vec3 topTint(1.0f);
    if (look.tintGrassTop) topTint = world.getGrassTint(cx, cz);
    if (look.tintFoliage)  topTint = world.getFoliageTint(cx, cz);

    const size_t first = out.size();
    pushItemModel(out, look, half, topTint, light);
    const float spin = item.ageSeconds + item.phase;
    const float c = std::cos(spin);
    const float s = std::sin(spin);
    for (size_t i = first; i < out.size(); ++i) {
        const glm::vec3 p = out[i].position;
        out[i].position = centre + glm::vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
    }
}

// Beta RenderFallingSand: the block itself, unrotated, lit by the cell holding its centre.
void appendFallingBlock(std::vector<ChunkVertex>& out, const world::World& world, const FallingBlockInstance& block) {
    ItemAppearance look = itemAppearance(gameplay::toItemId(block.id));
    look.flat = false;
    constexpr float half = 0.49f;
    const glm::vec3 centre = block.position + glm::vec3(0.0f, half, 0.0f);
    const auto cx = static_cast<int32_t>(std::floor(centre.x));
    const auto cy = static_cast<int32_t>(std::floor(centre.y));
    const auto cz = static_cast<int32_t>(std::floor(centre.z));
    glm::vec2 light(static_cast<float>(world.getSkyLight(cx, cy, cz)) / 15.0f,
                    static_cast<float>(world.getBlockLight(cx, cy, cz)) / 15.0f);
    // Beta RenderTNTPrimed: a white overlay; here full light and an over-bright tint.
    const float flash = std::clamp(block.flash, 0.0f, 1.0f);
    light = glm::mix(light, glm::vec2(1.0f), flash);
    const size_t first = out.size();
    pushItemModel(out, look, half, glm::vec3(1.0f + 2.0f * flash), light);
    for (size_t i = first; i < out.size(); ++i) {
        out[i].position += centre;
    }
}

glm::mat4 cameraProjection(const core::EngineConfig& config, float aspect) noexcept {
    const float maxDrawDist = static_cast<float>(config.renderDistanceChunks * world::Chunk::WIDTH);
    return glm::perspective(glm::radians(config.fovDegrees), aspect, config.nearPlane, std::max(160.0f, maxDrawDist + 32.0f));
}

float viewportAspect(int32_t width, int32_t height) noexcept {
    return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.777f;
}

} // namespace

WorldRenderer::~WorldRenderer() noexcept {
    shutdown();
    if (m_dynamicVao != 0 && bc_glDeleteVertexArrays) {
        bc_glDeleteVertexArrays(1, &m_dynamicVao);
        bc_glDeleteBuffers(1, &m_dynamicVbo);
    }
    if (m_lineVao != 0 && bc_glDeleteVertexArrays) {
        bc_glDeleteVertexArrays(1, &m_lineVao);
        bc_glDeleteBuffers(1, &m_lineVbo);
    }
}

bool WorldRenderer::initialize() {
    if (!m_atlas.initialize("assets/terrain.png")) {
        return false;
    }
    if (!m_skyRenderer.initialize()) {
        return false;
    }
    if (!m_chunkShader.compileFromSource(chunkVertexShaderSource(), chunkFragmentShaderSource())) {
        return false;
    }
    // The chunk shader reads DynamicLightBlock unconditionally; without a bound buffer it
    // would sample undefined memory, so a missing UBO is as fatal as a failed compile.
    if (!m_dynamicLightUbo.initialize() ||
        !DynamicLightUniformBuffer::bindProgram(m_chunkShader.getHandle())) {
        core::Logger::error("WorldRenderer", "Dynamic light uniform buffer unavailable.");
        return false;
    }
    if (!m_dynamicShader.compileFromSource(DYNAMIC_VS, DYNAMIC_FS)) {
        return false;
    }
    if (!m_lineShader.compileFromSource(LINE_VS, LINE_FS)) {
        return false;
    }
    // Not fatal: without it the world is drawn without rain or snow.
    (void)m_weatherRenderer.initialize();

    ChunkMeshGL::initSharedEBO();

    bc_glGenVertexArrays(1, &m_dynamicVao);
    bc_glGenBuffers(1, &m_dynamicVbo);
    bc_glBindVertexArray(m_dynamicVao);
    bc_glBindBuffer(GL_ARRAY_BUFFER, m_dynamicVbo);
    bc_glEnableVertexAttribArray(0);
    bc_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<const void*>(offsetof(ChunkVertex, position)));
    bc_glEnableVertexAttribArray(1);
    bc_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<const void*>(offsetof(ChunkVertex, uv)));
    bc_glEnableVertexAttribArray(2);
    bc_glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<const void*>(offsetof(ChunkVertex, color)));
    bc_glEnableVertexAttribArray(3);
    bc_glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<const void*>(offsetof(ChunkVertex, light)));

    bc_glGenVertexArrays(1, &m_lineVao);
    bc_glGenBuffers(1, &m_lineVbo);
    bc_glBindVertexArray(m_lineVao);
    bc_glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    bc_glEnableVertexAttribArray(0);
    bc_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    bc_glBindVertexArray(0);

    m_streamer = std::make_unique<streaming::WorldMeshStreamer>();
    return true;
}

glm::mat4 WorldRenderer::cameraViewProjection(
    const physics::Player& player, const core::EngineConfig& config, float aspect) noexcept {
    return cameraProjection(config, aspect) * player.getViewMatrix();
}

void WorldRenderer::buildDynamicLightFrame(
    lighting::DynamicLightFrame& frame,
    const lighting::DynamicLightManager& lights,
    const physics::Player& player,
    const core::EngineConfig& config,
    glm::ivec2 framebufferPx
) noexcept {
    // Same aspect as renderWorld() even for a minimised (zero-height) window.
    const glm::mat4 viewProj = cameraViewProjection(player, config, viewportAspect(framebufferPx.x, framebufferPx.y));
    frame.build(lights, player.getCameraEyePosition(player.getRenderAlpha()), viewProj, glm::vec2(glm::max(framebufferPx, glm::ivec2(1))));
}

uint8_t WorldRenderer::handBlockLight(
    uint8_t staticBlockLight, const lighting::DynamicLightFrame& frame, const physics::Player& player) noexcept {
    const glm::vec3 hand =
        lighting::DynamicLightManager::heldLightPosition(player.getEyePosition(), player.getLookDirection());
    return std::max(staticBlockLight, frame.blockLightLevelAt(hand));
}

std::string_view WorldRenderer::chunkVertexShaderSource() noexcept {
    return PACKED_CHUNK_VS;
}

std::string WorldRenderer::chunkFragmentShaderSource() {
    return lighting::injectDynamicLights(PACKED_CHUNK_FS);
}

void WorldRenderer::shutdown() noexcept {
    m_streamer.reset();
    m_sectionMeshes.clear();
    m_dynamicLightUbo.destroy();
}

void WorldRenderer::releaseWorld() {
    if (!m_streamer) {
        return; // Never initialised, or shut down
    }
    m_streamer.reset();
    m_sectionMeshes.clear();
    m_visibleMeshes.clear();
    m_visibleColumns.clear();
    m_streamer = std::make_unique<streaming::WorldMeshStreamer>();
}

void WorldRenderer::uploadSection(streaming::ChunkMeshResult& result) {
    if (result.buffers.empty()) {
        m_sectionMeshes.erase(result.coord);
        return;
    }
    m_sectionMeshes[result.coord].upload(result.buffers.opaqueVertices, result.buffers.translucentVertices);
}

void WorldRenderer::markAllChunksDirty(world::World& world) noexcept {
    for (auto& [coord, chunk] : world.getChunks()) {
        if (chunk) {
            chunk->setMeshDirty(true);
        }
    }
}

RenderStats WorldRenderer::renderWorld(
    world::World& world,
    const physics::Player& player,
    const physics::RaycastHit& targetHit,
    int32_t crackStage,
    const ParticleSystem& particles,
    const core::EngineConfig& config,
    float timeOfDay,
    float totalTimeSeconds,
    int32_t viewportWidth,
    int32_t viewportHeight,
    const lighting::DynamicLightManager& dynamicLights,
    std::span<const ItemEntityInstance> itemEntities,
    std::span<const MobRenderInstance> mobs,
    std::span<const FallingBlockInstance> fallingBlocks,
    std::span<const ArrowRenderInstance> arrows
) {
    RenderStats stats{};
    stats.totalChunks = world.getChunks().size();

    const glm::vec3 eyePos = player.getCameraEyePosition(player.getRenderAlpha());
    const int32_t playerCX = core::floorDiv(static_cast<int32_t>(std::floor(eyePos.x)), world::Chunk::WIDTH);
    const int32_t playerCZ = core::floorDiv(static_cast<int32_t>(std::floor(eyePos.z)), world::Chunk::DEPTH);

    // Meshing runs on the pipeline's workers; this thread only captures dirty sections and
    // uploads finished meshes within the frame budget. Unloaded columns come back as
    // deletions, so no separate sweep over the mesh map is needed.
    if (m_streamer) {
        m_streamer->setSmoothLighting(config.smoothLighting);
        m_streamer->update(world, eyePos, player.getLookDirection());
        const auto budget = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<float, std::milli>(std::max(0.0f, config.meshUploadBudgetMs)));
        const streaming::DrainStats drained =
            m_streamer->drainUploads(budget, [this](streaming::ChunkMeshResult& r) { uploadSection(r); });
        stats.meshUploads = drained.uploaded;
        stats.pendingMeshJobs = m_streamer->pipeline().pendingJobs() + m_streamer->pipeline().inFlightJobs();
    }

    // Setup viewport & clear sky
    glViewport(0, 0, viewportWidth, viewportHeight);
    const float aspect = viewportAspect(viewportWidth, viewportHeight);

    const SkyAtmosphere atmos = SkyRenderer::evaluateAtmosphere(
        timeOfDay, player.isHeadUnderWater(), m_weather.rainStrength, m_weather.thunderStrength, m_weather.lightningFlash);
    glClearColor(atmos.horizonColor.r, atmos.horizonColor.g, atmos.horizonColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    const glm::mat4 view = player.getViewMatrix();
    const float maxDrawDist = static_cast<float>(config.renderDistanceChunks * world::Chunk::WIDTH);
    const glm::mat4 proj = cameraProjection(config, aspect);
    const glm::mat4 viewProj = proj * view;

    // Dynamic lights for this exact camera and framebuffer, bound before any chunk draw.
    buildDynamicLightFrame(m_dynamicLightFrame, dynamicLights, player, config, {viewportWidth, viewportHeight});
    m_dynamicLightUbo.upload(m_dynamicLightFrame.uboBytes());
    m_dynamicLightUbo.bindBase();

    // 1. Sky, Stars, Sun, Moon & Blocky Clouds
    m_skyRenderer.renderSkyAndCelestials(view, proj, eyePos, timeOfDay, totalTimeSeconds, atmos);

    // 2. Frustum Culling of chunks
    math::Frustum frustum;
    frustum.update(viewProj);

    auto& visibleMeshes = m_visibleMeshes;
    visibleMeshes.clear();
    visibleMeshes.reserve(m_sectionMeshes.size());
    m_visibleColumns.clear();

    for (const auto& [coord, mesh] : m_sectionMeshes) {
        const int32_t dx = coord.x - playerCX;
        const int32_t dz = coord.z - playerCZ;
        if (dx * dx + dz * dz > (config.renderDistanceChunks + 1) * (config.renderDistanceChunks + 1)) {
            continue;
        }

        const glm::vec3 minCorner(coord.worldOrigin());
        const glm::vec3 maxCorner = minCorner + glm::vec3(static_cast<float>(streaming::SectionCoord::SIZE));

        if (frustum.intersectsAABB(minCorner, maxCorner)) {
            visibleMeshes.emplace_back(coord, &mesh);
            m_visibleColumns.push_back(coord.chunk());
            stats.totalVertices += mesh.getOpaqueVertexCount() + mesh.getTranslucentVertexCount();
        }
    }
    stats.visibleSections = visibleMeshes.size();
    stats.visibleChunks = countDistinctColumns(m_visibleColumns);

    // 3. Opaque + AlphaCutout Pass
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    m_chunkShader.bind();
    m_atlas.bind(0);
    m_chunkShader.setInt("uAtlas", 0);
    m_chunkShader.setMat4("u_viewProjection", viewProj);
    m_chunkShader.setVec3("uCameraPos", eyePos);
    m_chunkShader.setFloat("uSunBrightness", atmos.sunBrightness);
    m_chunkShader.setVec3("uFogColor", atmos.horizonColor);
    const float fogEnd = player.isHeadUnderWater() ? 28.0f : (maxDrawDist * 0.95f);
    const float fogStart = player.isHeadUnderWater() ? 4.0f : (fogEnd * 0.58f);
    m_chunkShader.setFloat("uFogStart", fogStart);
    m_chunkShader.setFloat("uFogEnd", fogEnd);
    m_chunkShader.setInt("uAlphaCutout", 1);

    for (const auto& [coord, mesh] : visibleMeshes) {
        // Vertices carry their section index (word 1), so the origin is the column's.
        m_chunkShader.setIVec3("u_sectionOriginWorld", {coord.x * world::Chunk::WIDTH, 0, coord.z * world::Chunk::DEPTH});
        mesh->drawOpaque();
    }

    // 4. Block Break Particles, dropped item entities and mobs (same unlit-by-dynamics pass)
    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);
    std::vector<ChunkVertex> particleVerts;
    particles.buildBillboardVertices(camRight, camUp, particleVerts);
    for (const ItemEntityInstance& item : itemEntities) {
        appendItemEntity(particleVerts, world, item);
    }
    for (const MobRenderInstance& mob : mobs) {
        appendMobModel(particleVerts, world, mob);
    }
    for (const FallingBlockInstance& block : fallingBlocks) {
        appendFallingBlock(particleVerts, world, block);
    }
    for (const ArrowRenderInstance& arrow : arrows) {
        appendArrowModel(particleVerts, world, arrow);
    }
    if (!particleVerts.empty()) {
        m_dynamicShader.bind();
        m_atlas.bind(0);
        m_dynamicShader.setInt("uAtlas", 0);
        m_dynamicShader.setMat4("uViewProj", viewProj);
        m_dynamicShader.setMat4("uModel", glm::mat4(1.0f));
        m_dynamicShader.setVec3("uCameraPos", eyePos);
        m_dynamicShader.setFloat("uSunBrightness", atmos.sunBrightness);
        m_dynamicShader.setVec3("uFogColor", atmos.horizonColor);
        m_dynamicShader.setFloat("uFogStart", fogStart);
        m_dynamicShader.setFloat("uFogEnd", fogEnd);
        m_dynamicShader.setInt("uAlphaCutout", 1);

        glDisable(GL_CULL_FACE);
        bc_glBindVertexArray(m_dynamicVao);
        bc_glBindBuffer(GL_ARRAY_BUFFER, m_dynamicVbo);
        bc_glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(particleVerts.size() * sizeof(ChunkVertex)),
            particleVerts.data(),
            GL_DYNAMIC_DRAW
        );
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(particleVerts.size()));
        bc_glBindVertexArray(0);
        glEnable(GL_CULL_FACE);
    }

    // 5. Block Selection Wireframe & Crack Animation Overlay
    if (targetHit.hit) {
        renderSelectionAndCrack(viewProj, targetHit, crackStage);
    }

    // 6. Translucent Pass (Water, Ice)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    m_chunkShader.bind();
    m_chunkShader.setInt("uAlphaCutout", 0);

    for (const auto& [coord, mesh] : visibleMeshes) {
        m_chunkShader.setIVec3("u_sectionOriginWorld", {coord.x * world::Chunk::WIDTH, 0, coord.z * world::Chunk::DEPTH});
        mesh->drawTranslucent();
    }

    glDepthMask(GL_TRUE);

    // 6b. S8.11 rain and snow, after the translucent blocks.
    m_weatherRenderer.render(world, eyePos, viewProj, m_weather, atmos.sunBrightness, totalTimeSeconds);

    // 7. First-Person Held Block / Pickaxe / Hand in bottom-right corner
    renderFirstPersonHand(player, world, atmos.sunBrightness, aspect);

    return stats;
}

void WorldRenderer::renderSelectionAndCrack(
    const glm::mat4& viewProj,
    const physics::RaycastHit& hit,
    int32_t crackStage
) {
    const math::AABB box = math::AABB::selectionBox(hit.blockPos.x, hit.blockPos.y, hit.blockPos.z, hit.blockId, hit.blockMeta)
                                  .expanded(glm::vec3(0.003f));
    const glm::vec3 p0 = box.min;
    const glm::vec3 p1 = box.max;

    const std::array<glm::vec3, 24> lines = {{
        // Bottom rectangle
        {p0.x, p0.y, p0.z}, {p1.x, p0.y, p0.z},
        {p1.x, p0.y, p0.z}, {p1.x, p0.y, p1.z},
        {p1.x, p0.y, p1.z}, {p0.x, p0.y, p1.z},
        {p0.x, p0.y, p1.z}, {p0.x, p0.y, p0.z},
        // Top rectangle
        {p0.x, p1.y, p0.z}, {p1.x, p1.y, p0.z},
        {p1.x, p1.y, p0.z}, {p1.x, p1.y, p1.z},
        {p1.x, p1.y, p1.z}, {p0.x, p1.y, p1.z},
        {p0.x, p1.y, p1.z}, {p0.x, p1.y, p0.z},
        // Vertical pillars
        {p0.x, p0.y, p0.z}, {p0.x, p1.y, p0.z},
        {p1.x, p0.y, p0.z}, {p1.x, p1.y, p0.z},
        {p1.x, p0.y, p1.z}, {p1.x, p1.y, p1.z},
        {p0.x, p0.y, p1.z}, {p0.x, p1.y, p1.z}
    }};

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_lineShader.bind();
    m_lineShader.setMat4("uMVP", viewProj);
    m_lineShader.setVec4("uColor", glm::vec4(0.0f, 0.0f, 0.0f, 0.65f));

    bc_glBindVertexArray(m_lineVao);
    bc_glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    bc_glBufferData(GL_ARRAY_BUFFER, sizeof(lines), lines.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));
    bc_glBindVertexArray(0);

    // Block breaking crack overlay (tiles 240..249)
    if (crackStage >= 0) {
        const uint8_t crackTile = static_cast<uint8_t>(240 + std::min(crackStage, 9));
        const std::array<uint8_t, 6> crackTiles = {crackTile, crackTile, crackTile, crackTile, crackTile, crackTile};

        std::vector<ChunkVertex> crackVerts;
        crackVerts.reserve(36);
        pushCubeFaces(crackVerts, p0, p1, crackTiles, glm::vec3(1.0f), glm::vec2(1.0f, 1.0f));

        m_dynamicShader.bind();
        m_atlas.bind(0);
        m_dynamicShader.setInt("uAtlas", 0);
        m_dynamicShader.setMat4("uViewProj", viewProj);
        m_dynamicShader.setMat4("uModel", glm::mat4(1.0f));
        m_dynamicShader.setVec3("uCameraPos", glm::vec3(0.0f));
        m_dynamicShader.setFloat("uSunBrightness", 1.0f);
        m_dynamicShader.setVec3("uFogColor", glm::vec3(1.0f));
        m_dynamicShader.setFloat("uFogStart", 1000.0f);
        m_dynamicShader.setFloat("uFogEnd", 2000.0f);
        m_dynamicShader.setInt("uAlphaCutout", 0);

        bc_glBindVertexArray(m_dynamicVao);
        bc_glBindBuffer(GL_ARRAY_BUFFER, m_dynamicVbo);
        bc_glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(crackVerts.size() * sizeof(ChunkVertex)),
            crackVerts.data(),
            GL_DYNAMIC_DRAW
        );
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(crackVerts.size()));
        bc_glBindVertexArray(0);
    }
}

void WorldRenderer::renderFirstPersonHand(
    const physics::Player& player,
    const world::World& world,
    float sunBrightness,
    float aspect
) {
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);

    const physics::InventorySlot held = player.getHeldItem();
    const float bobPhase = player.getBobbingPhase();
    const float bobAmt = player.getBobbingIntensity();
    const float swing = player.getHandSwingProgress(); // 0..1

    const float bobX = std::sin(bobPhase * 0.5f) * 0.03f * bobAmt;
    const float bobY = -std::abs(std::cos(bobPhase)) * 0.03f * bobAmt;
    const float swingAngle = (swing < 1.0f) ? std::sin(swing * 3.14159265f) : 0.0f;

    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(0.56f + bobX - swingAngle * 0.18f, -0.52f + bobY - swingAngle * 0.12f, -0.78f - swingAngle * 0.15f));
    model = glm::rotate(model, glm::radians(-28.0f * swingAngle), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(38.0f + 25.0f * swingAngle), glm::vec3(0.0f, 1.0f, 0.0f));

    const glm::vec3 pPos = player.getPosition();
    const int32_t px = static_cast<int32_t>(std::floor(pPos.x));
    const int32_t py = static_cast<int32_t>(std::floor(pPos.y + 1.0f));
    const int32_t pz = static_cast<int32_t>(std::floor(pPos.z));
    const uint8_t blockLight = handBlockLight(world.getBlockLight(px, py, pz), m_dynamicLightFrame, player);
    const glm::vec2 light(
        static_cast<float>(world.getSkyLight(px, py, pz)) / 15.0f,
        static_cast<float>(blockLight) / 15.0f
    );

    std::vector<ChunkVertex> handVerts;
    handVerts.reserve(36);

    if (held.isEmpty()) {
        // Render classic Steve skin arm rectangle
        const std::array<uint8_t, 6> skinTiles = {18, 18, 18, 18, 18, 18};
        pushCubeFaces(
            handVerts,
            glm::vec3(-0.12f, -0.25f, -0.12f),
            glm::vec3(0.12f, 0.25f, 0.12f),
            skinTiles,
            glm::vec3(0.88f, 0.68f, 0.56f),
            light
        );
        for (auto& v : handVerts) {
            v.color *= glm::vec3(0.88f, 0.68f, 0.56f);
        }
    } else {
        const ItemAppearance look = itemAppearance(held.id);
        glm::vec3 topTint(1.0f);
        if (look.tintGrassTop) topTint = world.getGrassTint(px, pz);
        if (look.tintFoliage)  topTint = world.getFoliageTint(px, pz);
        pushItemModel(handVerts, look, 0.22f, topTint, light);
    }

    const glm::mat4 handProj = glm::perspective(glm::radians(55.0f), aspect, 0.05f, 10.0f);
    m_dynamicShader.bind();
    m_atlas.bind(0);
    m_dynamicShader.setInt("uAtlas", 0);
    m_dynamicShader.setMat4("uViewProj", handProj);
    m_dynamicShader.setMat4("uModel", model);
    m_dynamicShader.setVec3("uCameraPos", glm::vec3(0.0f));
    m_dynamicShader.setFloat("uSunBrightness", sunBrightness);
    m_dynamicShader.setVec3("uFogColor", glm::vec3(0.7f, 0.8f, 1.0f));
    m_dynamicShader.setFloat("uFogStart", 50.0f);
    m_dynamicShader.setFloat("uFogEnd", 100.0f);
    m_dynamicShader.setInt("uAlphaCutout", 1);

    glDisable(GL_CULL_FACE);
    bc_glBindVertexArray(m_dynamicVao);
    bc_glBindBuffer(GL_ARRAY_BUFFER, m_dynamicVbo);
    bc_glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(handVerts.size() * sizeof(ChunkVertex)),
        handVerts.data(),
        GL_DYNAMIC_DRAW
    );
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(handVerts.size()));
    bc_glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}

} // namespace betacraft::render
