#pragma once

#include "core/Config.hpp"
#include "core/Types.hpp"
#include "gameplay/ItemIds.hpp"
#include "lighting/DynamicLightFrame.hpp"
#include "math/Frustum.hpp"
#include "physics/Player.hpp"
#include "physics/Raycaster.hpp"
#include "render/ChunkMesher.hpp"
#include "render/DynamicLightUniformBuffer.hpp"
#include "render/MobModels.hpp"
#include "render/ParticleSystem.hpp"
#include "render/Shader.hpp"
#include "render/SkyRenderer.hpp"
#include "render/TextureAtlas.hpp"
#include "render/WeatherRenderer.hpp"
#include "streaming/SectionCoord.hpp"
#include "streaming/WorldMeshStreamer.hpp"
#include "world/World.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace betacraft::render {

struct RenderStats {
    size_t visibleChunks{0};
    size_t totalChunks{0};
    size_t totalVertices{0};
    size_t visibleSections{0};
    size_t meshUploads{0};     // section meshes uploaded this frame
    size_t pendingMeshJobs{0}; // queued + running in the AsyncChunkPipeline
};

// Number of distinct columns in `columns`, which it sorts and deduplicates in place. Meant
// for a buffer reused across frames: once grown to the visible set it allocates nothing.
[[nodiscard]] size_t countDistinctColumns(std::vector<core::ChunkCoord>& columns);

// A dropped item entity to draw this frame.
struct ItemEntityInstance {
    glm::vec3 position{0.0f}; // Render-interpolated feet position of the entity
    gameplay::ItemId id{gameplay::kNoItem};
    float ageSeconds{0.0f};   // Drives Beta's spin and bob
    float phase{0.0f};        // Per-entity offset (Beta EntityItem.hoverStart)
};

// S5.3 falling sand or gravel to draw this frame: a full-size block cube.
struct FallingBlockInstance {
    glm::vec3 position{0.0f}; // Render-interpolated feet position of the entity
    world::BlockId id{world::BlockId::Sand};
    float flash{0.0f}; // S6.6 primed TNT: 0..1 white flash of the last fuse ticks
};

class WorldRenderer : private core::NonCopyable {
public:
    WorldRenderer() noexcept = default;
    ~WorldRenderer() noexcept;

    [[nodiscard]] bool initialize();
    // Joins the meshing workers and frees the section meshes. Call while the GL context is
    // current and before the World is destroyed; the renderer meshes nothing afterwards.
    void shutdown() noexcept;
    // S7.9 leaving a world: joins the meshing workers and frees the section meshes like
    // shutdown(), then starts fresh workers for the next world (GL programs are kept). Call
    // before the World is destroyed.
    void releaseWorld();

    void markAllChunksDirty(world::World& world) noexcept;

    RenderStats renderWorld(
        world::World& world,
        const physics::Player& player,
        const physics::RaycastHit& targetHit,
        int32_t crackStage, // 0..9 on targetHit, -1: no crack (gameplay::MiningProgress::crackStage)
        const ParticleSystem& particles,
        const core::EngineConfig& config,
        float timeOfDay,
        float totalTimeSeconds,
        int32_t viewportWidth, // framebuffer (drawable) pixels, not window points
        int32_t viewportHeight,
        const lighting::DynamicLightManager& dynamicLights,
        std::span<const ItemEntityInstance> itemEntities = {},
        std::span<const MobRenderInstance> mobs = {},
        std::span<const FallingBlockInstance> fallingBlocks = {},
        std::span<const ArrowRenderInstance> arrows = {}
    );

    [[nodiscard]] const TextureAtlas& getAtlas() const noexcept { return m_atlas; }

    // S8.10 / S8.11 weather of the next renderWorld() calls: darker, greyer sky and light
    // (SkyRenderer::evaluateAtmosphere overload), rain or snow columns after the translucent
    // pass, and the lightning flash and bolt. The default is clear weather.
    void setWeather(const WeatherFrame& frame) noexcept { m_weather = frame; }
    [[nodiscard]] const WeatherFrame& weather() const noexcept { return m_weather; }

    // View-projection renderWorld() draws the world with, for a viewport of this aspect.
    [[nodiscard]] static glm::mat4 cameraViewProjection(
        const physics::Player& player, const core::EngineConfig& config, float aspect) noexcept;

    // CPU half of the P4.6 dynamic lights: selects and tile-culls the collected lights for
    // renderWorld()'s camera. `framebufferPx` is the size given to glViewport (the drawable
    // size on HiDPI), because the shader's tile lookup uses gl_FragCoord.
    static void buildDynamicLightFrame(
        lighting::DynamicLightFrame& frame,
        const lighting::DynamicLightManager& lights,
        const physics::Player& player,
        const core::EngineConfig& config,
        glm::ivec2 framebufferPx
    ) noexcept;

    // Block light (0..15) the first-person hand is shaded with: the static light at the player
    // or the frame's dynamic light at the hand, whichever is brighter.
    [[nodiscard]] static uint8_t handBlockLight(
        uint8_t staticBlockLight, const lighting::DynamicLightFrame& frame, const physics::Player& player) noexcept;

    // Exact sources of the chunk program initialize() compiles.
    [[nodiscard]] static std::string_view chunkVertexShaderSource() noexcept;
    [[nodiscard]] static std::string chunkFragmentShaderSource();

private:
    void renderSelectionAndCrack(
        const glm::mat4& viewProj,
        const physics::RaycastHit& hit,
        int32_t crackStage
    );

    void uploadSection(streaming::ChunkMeshResult& result);

    void renderFirstPersonHand(
        const physics::Player& player,
        const world::World& world,
        float sunBrightness,
        float aspect
    );

    TextureAtlas m_atlas;
    SkyRenderer m_skyRenderer;
    WeatherRenderer m_weatherRenderer;
    WeatherFrame m_weather{};
    ShaderProgram m_chunkShader;
    ShaderProgram m_dynamicShader;
    ShaderProgram m_lineShader;
    DynamicLightUniformBuffer m_dynamicLightUbo;
    lighting::DynamicLightFrame m_dynamicLightFrame;

    std::unordered_map<streaming::SectionCoord, ChunkMeshGL> m_sectionMeshes;
    // Per-frame culling results, kept to reuse their storage.
    std::vector<std::pair<streaming::SectionCoord, const ChunkMeshGL*>> m_visibleMeshes;
    std::vector<core::ChunkCoord> m_visibleColumns;

    GLuint m_dynamicVao{0};
    GLuint m_dynamicVbo{0};
    GLuint m_lineVao{0};
    GLuint m_lineVbo{0};

    // Declared last so its workers are joined before anything else here is torn down.
    std::unique_ptr<streaming::WorldMeshStreamer> m_streamer;
};

} // namespace betacraft::render
