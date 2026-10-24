// Phase 2 integration, P4.6 dynamic lights in the chunk shaders: the program WorldRenderer
// compiles must declare the same DynamicLightBlock that DynamicLightUbo packs, and feed it the
// fragment's world position. Source checks run everywhere; the compile check needs a GL 3.3 context.
#include "test_support/GLTestContext.hpp"

#include "lighting/DynamicLightFrame.hpp"
#include "lighting/DynamicLightManager.hpp"
#include "lighting/DynamicLightUbo.hpp"
#include "lighting/DynamicLightsGlsl.hpp"
#include "render/ChunkMesher.hpp"
#include "render/DynamicLightUniformBuffer.hpp"
#include "render/Shader.hpp"
#include "render/WorldRenderer.hpp"
#include "streaming/PackedChunkVertex.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace betacraft;

namespace {

// Comment-free, whitespace-collapsed text from `startMarker` to the next "};" (inclusive), so a
// reformatted copy still compares equal while any type/name/order change does not.
std::string normalizedDeclaration(std::string_view src, std::string_view startMarker) {
    const size_t start = src.find(startMarker);
    if (start == std::string_view::npos) {
        return {};
    }
    const size_t end = src.find("};", start);
    if (end == std::string_view::npos) {
        return {};
    }
    const std::string_view decl = src.substr(start, end + 2 - start);
    std::string out;
    bool pendingSpace = false;
    for (size_t i = 0; i < decl.size(); ++i) {
        if (decl.compare(i, 2, "//") == 0) {
            i = decl.find('\n', i);
            if (i == std::string_view::npos) {
                break;
            }
            pendingSpace = true;
            continue;
        }
        const char c = decl[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !out.empty()) {
            out.push_back(' ');
        }
        pendingSpace = false;
        out.push_back(c);
    }
    return out;
}

size_t countOf(std::string_view haystack, std::string_view needle) {
    size_t n = 0;
    for (size_t pos = haystack.find(needle); pos != std::string_view::npos; pos = haystack.find(needle, pos + 1)) {
        ++n;
    }
    return n;
}

std::string_view mainBody(std::string_view src) {
    const size_t pos = src.find("void main()");
    return pos == std::string_view::npos ? std::string_view{} : src.substr(pos);
}

const std::string kBlockMarker = std::string("layout(std140) uniform ") + lighting::kUboBlockName;

} // namespace

TEST(DynLightShader, ChunkFragmentShaderDeclaresTheDynamicLightBlockOnce) {
    const std::string fs = render::WorldRenderer::chunkFragmentShaderSource();
    const std::string_view include = lighting::dynamicLightsGlslSource();

    const std::string expectedBlock = normalizedDeclaration(include, kBlockMarker);
    const std::string expectedStruct = normalizedDeclaration(include, "struct DynamicLightGPU");
    ASSERT_FALSE(expectedBlock.empty());
    ASSERT_FALSE(expectedStruct.empty());

    EXPECT_EQ(normalizedDeclaration(fs, kBlockMarker), expectedBlock);
    EXPECT_EQ(normalizedDeclaration(fs, "struct DynamicLightGPU"), expectedStruct);
    EXPECT_EQ(countOf(fs, std::string("uniform ") + lighting::kUboBlockName), 1u);

    // GLSL requires #version first, exactly once; the block must precede its use in main().
    EXPECT_EQ(fs.rfind("#version 330 core", 0), 0u);
    EXPECT_EQ(countOf(fs, "\n#version"), 0u); // the include only mentions it in a comment
    EXPECT_LT(fs.find(kBlockMarker), fs.find("void main()"));
}

TEST(DynLightShader, ChunkShadersFeedTheFragmentWorldPositionToTheDynamicLightTerm) {
    const std::string vs(render::WorldRenderer::chunkVertexShaderSource());
    const std::string fs = render::WorldRenderer::chunkFragmentShaderSource();

    EXPECT_EQ(countOf(vs, "out vec3 v_worldPos;"), 1u);
    EXPECT_NE(mainBody(vs).find("v_worldPos = worldPos"), std::string_view::npos)
        << "v_worldPos must be the swayed world position the vertex is drawn at";
    EXPECT_EQ(countOf(fs, "in vec3 v_worldPos;"), 1u);

    const std::string_view body = mainBody(fs);
    const size_t call = body.find("computeDynamicLight(v_worldPos, gl_FragCoord.xy)");
    ASSERT_NE(call, std::string_view::npos);
    // Lit before fog, so a torch in the distance fades like any other lit surface.
    const size_t fog = body.find("mix(uFogColor");
    ASSERT_NE(fog, std::string_view::npos);
    EXPECT_LT(call, fog);
}

TEST(DynLightShader, ChunkProgramCompilesAndBindsTheBlockWhenGlIsAvailable) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "OpenGL 3.3 Core context not available in this test environment.";
    }

    render::DynamicLightUniformBuffer ubo;
    ASSERT_TRUE(ubo.initialize());

    render::ShaderProgram program;
    ASSERT_TRUE(program.compileFromSource(
        render::WorldRenderer::chunkVertexShaderSource(), render::WorldRenderer::chunkFragmentShaderSource()));
    EXPECT_TRUE(render::DynamicLightUniformBuffer::bindProgram(program.getHandle()));

    // The driver's std140 size must be the byte count the packer uploads.
    auto* getIndex = reinterpret_cast<PFNGLGETUNIFORMBLOCKINDEXPROC>(SDL_GL_GetProcAddress("glGetUniformBlockIndex"));
    auto* getBlockiv =
        reinterpret_cast<PFNGLGETACTIVEUNIFORMBLOCKIVPROC>(SDL_GL_GetProcAddress("glGetActiveUniformBlockiv"));
    ASSERT_NE(getIndex, nullptr);
    ASSERT_NE(getBlockiv, nullptr);
    const GLuint index = getIndex(program.getHandle(), lighting::kUboBlockName);
    ASSERT_NE(index, GL_INVALID_INDEX);
    GLint dataSize = 0;
    getBlockiv(program.getHandle(), index, GL_UNIFORM_BLOCK_DATA_SIZE, &dataSize);
    EXPECT_EQ(static_cast<size_t>(dataSize), lighting::ubo::kBlockSize);
    GLint binding = -1;
    getBlockiv(program.getHandle(), index, GL_UNIFORM_BLOCK_BINDING, &binding);
    EXPECT_EQ(binding, static_cast<GLint>(lighting::kUboBindingPoint));

    lighting::DynamicLightFrame frame;
    ubo.upload(frame.uboBytes());
    ubo.bindBase();
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    ubo.destroy();
}

namespace {

// Off-screen RGBA8 target; GL 3.0 FBO entry points are not part of render/GLHeaders.
class OffscreenTarget {
public:
    static constexpr int kSize = 64;

    bool create() {
        genFbos = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(SDL_GL_GetProcAddress("glGenFramebuffers"));
        bindFbo = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(SDL_GL_GetProcAddress("glBindFramebuffer"));
        attach = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(SDL_GL_GetProcAddress("glFramebufferTexture2D"));
        status = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(SDL_GL_GetProcAddress("glCheckFramebufferStatus"));
        deleteFbos = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(SDL_GL_GetProcAddress("glDeleteFramebuffers"));
        if (!genFbos || !bindFbo || !attach || !status || !deleteFbos) {
            return false;
        }
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        genFbos(1, &fbo);
        bindFbo(GL_FRAMEBUFFER, fbo);
        attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        return status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    std::array<uint8_t, 4> centerPixel() const {
        std::array<uint8_t, 4> px{};
        glReadPixels(kSize / 2, kSize / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        return px;
    }

    ~OffscreenTarget() {
        if (bindFbo) {
            bindFbo(GL_FRAMEBUFFER, 0);
        }
        if (fbo != 0) {
            deleteFbos(1, &fbo);
        }
        if (color != 0) {
            glDeleteTextures(1, &color);
        }
    }

private:
    PFNGLGENFRAMEBUFFERSPROC genFbos{nullptr};
    PFNGLBINDFRAMEBUFFERPROC bindFbo{nullptr};
    PFNGLFRAMEBUFFERTEXTURE2DPROC attach{nullptr};
    PFNGLCHECKFRAMEBUFFERSTATUSPROC status{nullptr};
    PFNGLDELETEFRAMEBUFFERSPROC deleteFbos{nullptr};
    GLuint fbo{0};
    GLuint color{0};
};

} // namespace

TEST(DynLightShader, HeldTorchBrightensAnUnlitWallAtNightOnTheGpu) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "OpenGL 3.3 Core context not available in this test environment.";
    }

    OffscreenTarget target;
    ASSERT_TRUE(target.create());

    render::DynamicLightUniformBuffer ubo;
    ASSERT_TRUE(ubo.initialize());
    render::ShaderProgram program;
    ASSERT_TRUE(program.compileFromSource(
        render::WorldRenderer::chunkVertexShaderSource(), render::WorldRenderer::chunkFragmentShaderSource()));
    ASSERT_TRUE(render::DynamicLightUniformBuffer::bindProgram(program.getHandle()));

    // White 1x1 atlas so the pixel is pure lighting.
    GLuint white = 0;
    const std::array<uint8_t, 4> texel{255, 255, 255, 255};
    glGenTextures(1, &white);
    glBindTexture(GL_TEXTURE_2D, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel.data());

    // One +Z face (x, y in 0..2 at z = 1) with no sky or block light and no biome tint.
    std::vector<streaming::PackedChunkVertex> quad;
    const std::array<glm::uvec2, 4> corners{{{0, 0}, {2, 0}, {2, 2}, {0, 2}}};
    for (uint32_t c = 0; c < 4; ++c) {
        quad.push_back({streaming::PackedChunkVertex::packWord0(corners[c].x, corners[c].y, 1, 2, 3, c, 0),
                        streaming::PackedChunkVertex::packWord1(0, 0, 255, 0, false, false)});
    }
    render::ChunkMeshGL::initSharedEBO(); // default size: the EBO is process-wide
    render::ChunkMeshGL mesh;
    mesh.upload(quad, {});

    const glm::vec3 eye{1.0f, 1.0f, 3.0f};
    const glm::mat4 viewProj = glm::perspective(glm::radians(70.0f), 1.0f, 0.05f, 100.0f) *
                               glm::lookAt(eye, glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const auto drawWith = [&](const lighting::DynamicLightManager& lights) {
        lighting::DynamicLightFrame frame;
        frame.build(lights, eye, viewProj, glm::vec2(OffscreenTarget::kSize));
        ubo.upload(frame.uboBytes());
        ubo.bindBase();

        glViewport(0, 0, OffscreenTarget::kSize, OffscreenTarget::kSize);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        program.bind();
        glBindTexture(GL_TEXTURE_2D, white);
        program.setInt("uAtlas", 0);
        program.setMat4("u_viewProjection", viewProj);
        program.setVec3("uCameraPos", eye);
        program.setFloat("uSunBrightness", 0.0f); // midnight
        program.setVec3("uFogColor", glm::vec3(0.0f));
        program.setFloat("uFogStart", 1000.0f);
        program.setFloat("uFogEnd", 2000.0f);
        program.setInt("uAlphaCutout", 0);
        program.setIVec3("u_sectionOriginWorld", glm::ivec3(0));
        mesh.drawOpaque();
        return target.centerPixel();
    };

    const lighting::DynamicLightManager none;
    const auto dark = drawWith(none);

    lighting::DynamicLightManager torch;
    ASSERT_TRUE(torch.submitHeldItem(eye, {0.0f, 0.0f, -1.0f}, voxel::BlockId::Torch));
    const auto lit = drawWith(torch);

    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    EXPECT_LT(dark[0], 16) << "night with no light must stay near black";
    EXPECT_GT(lit[0], dark[0] + 120) << "the held torch must light the wall in front of the player";
    EXPECT_GE(lit[0], lit[2]) << "torch light is warm";

    glDeleteTextures(1, &white);
    ubo.destroy();
}
