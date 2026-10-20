// P4.6 — Executes the REAL shaders/dynamic_lights.glsl (through the strict GLSL 3.30 subset
// interpreter in GlslMiniInterpreter.hpp) against the CPU culler / packer / shade(), so tile
// addressing, mask lookup and shading in the shader cannot silently diverge from the CPU side.
#include "GlslMiniInterpreter.hpp"

#include "lighting/DynamicLightFrame.hpp"
#include "lighting/DynamicLightUbo.hpp"
#include "lighting/TileLightCuller.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace betacraft::lighting;
namespace glsl = dynlight_test::glsl;
using betacraft::voxel::BlockId;

namespace {

std::string readShaderFile() {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    std::ifstream in(root / "shaders" / "dynamic_lights.glsl");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

glm::mat4 perspectiveViewProj(const glm::vec2& viewport, glm::vec3 eye, glm::vec3 dir) {
    const float aspect = viewport.y > 0.0f ? viewport.x / viewport.y : 1.0f;
    const glm::mat4 proj = glm::perspective(glm::radians(70.0f), std::max(aspect, 0.01f), 0.05f, 256.0f);
    return proj * glm::lookAt(eye, eye + dir, glm::vec3(0, 1, 0));
}

glsl::Program loadProgram(const std::string& src, const UboBuffer& ubo) {
    glsl::Program p(src);
    p.bindUniformBlock(kUboBlockName, ubo);
    return p;
}

// Number of fragments whose shader tile differs from TileLightGrid::tileIndexForFragCoord().
size_t tileIndexMismatches(const std::string& src, const glm::vec2& viewport, uint32_t seed, int samples) {
    TileLightGrid grid;
    grid.build({}, glm::mat4(1.0f), viewport);
    UboBuffer ubo{};
    packDynamicLightUbo({}, grid.masks(), grid.tileSizePx(), ubo);
    glsl::Program prog = loadProgram(src, ubo);

    std::mt19937 rng(seed);
    const glm::vec2 vp = glm::max(viewport, glm::vec2(1.0f));
    std::uniform_real_distribution<float> fx(-2.0f, vp.x + 2.0f);
    std::uniform_real_distribution<float> fy(-2.0f, vp.y + 2.0f);
    size_t mismatches = 0;
    for (int s = 0; s < samples; ++s) {
        // Half the samples sit on pixel centres, like real gl_FragCoord values.
        glm::vec2 frag{fx(rng), fy(rng)};
        if (s % 2 == 0) {
            frag = glm::floor(glm::clamp(frag, glm::vec2(0.0f), vp - 1.0f)) + 0.5f;
        }
        const int64_t shader = glsl::asInt(prog.call("dynTileIndex", {glsl::vec2v(frag)}));
        if (shader != static_cast<int64_t>(grid.tileIndexForFragCoord(frag))) {
            ++mismatches;
        }
    }
    return mismatches;
}

struct Scenario {
    DynamicLightManager manager;
    DynamicLightFrame frame;
    glm::mat4 viewProj{1.0f};
    glm::vec2 viewport{1600.0f, 900.0f};
};

void buildScenario(Scenario& sc, uint32_t seed, int lights) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> pos(-14.0f, 14.0f);
    const glm::vec3 eye{0.0f, 0.0f, 0.0f};
    sc.viewProj = perspectiveViewProj(sc.viewport, eye, {0, 0, -1});
    sc.manager.clear();
    (void)sc.manager.submitHeldItem(eye, {0, 0, -1}, BlockId::Torch);
    for (int i = 0; i < lights; ++i) {
        DynamicLight l;
        l.position = {pos(rng), pos(rng), pos(rng) - 14.0f};
        l.radius = 1.0f + std::abs(pos(rng));
        l.color = {std::abs(pos(rng)) / 14.0f, std::abs(pos(rng)) / 14.0f, 0.2f + std::abs(pos(rng)) / 20.0f};
        l.intensity = 0.25f + std::abs(pos(rng)) / 10.0f;
        (void)sc.manager.submit(l);
    }
    sc.frame.build(sc.manager, eye, sc.viewProj, sc.viewport);
}

// Full CPU <-> GLSL agreement on one scenario: every tile's mask lookup and the shading of
// random on-screen points. Any interpreter error (type error, out-of-range index...) counts
// as a disagreement.
bool shaderAgreesWithCpu(const std::string& src, std::string* why = nullptr) {
    try {
        Scenario sc;
        buildScenario(sc, 1234u, 24);
        glsl::Program prog = loadProgram(src, sc.frame.uboBytes());
        const TileLightGrid& grid = sc.frame.grid();
        const glm::vec2 tile = grid.tileSizePx();
        for (uint32_t row = 0; row < kTileRows; ++row) {
            for (uint32_t col = 0; col < kTileCols; ++col) {
                const glm::vec2 frag = (glm::vec2(col, row) + 0.5f) * tile;
                const int64_t idx = glsl::asInt(prog.call("dynTileIndex", {glsl::vec2v(frag)}));
                const uint32_t mask = glsl::asUInt(prog.call("dynTileMask", {glsl::vec2v(frag)}));
                if (idx != static_cast<int64_t>(row * kTileCols + col) || mask != grid.mask(col, row)) {
                    if (why) {
                        *why = "tile (" + std::to_string(col) + "," + std::to_string(row) + ") idx=" +
                               std::to_string(idx) + " mask=" + std::to_string(mask) +
                               " cpu=" + std::to_string(grid.mask(col, row));
                    }
                    return false;
                }
            }
        }
        std::mt19937 rng(99u);
        std::uniform_real_distribution<float> pos(-14.0f, 14.0f);
        int shaded = 0;
        for (int s = 0; s < 4000 && shaded < 400; ++s) {
            const glm::vec3 p{pos(rng), pos(rng), pos(rng) - 14.0f};
            const glm::vec4 clip = sc.viewProj * glm::vec4(p, 1.0f);
            if (clip.w <= 0.0f) {
                continue;
            }
            const glm::vec2 ndc = glm::vec2(clip) / clip.w;
            if (glm::any(glm::greaterThan(glm::abs(ndc), glm::vec2(1.0f)))) {
                continue;
            }
            const glm::vec2 frag = (ndc * 0.5f + 0.5f) * sc.viewport;
            const glm::vec3 gpu = glsl::asVec3(prog.call("computeDynamicLight", {glsl::vec3v(p), glsl::vec2v(frag)}));
            const glm::vec3 cpu = sc.frame.shade(p, frag);
            const float tol = 1e-5f * (1.0f + glm::length(cpu));
            if (glm::any(glm::greaterThan(glm::abs(gpu - cpu), glm::vec3(tol)))) {
                if (why) {
                    *why = "shade mismatch at sample " + std::to_string(s);
                }
                return false;
            }
            ++shaded;
        }
        if (shaded < 100) {
            if (why) {
                *why = "too few on-screen samples";
            }
            return false;
        }
        return true;
    } catch (const glsl::GlslError& e) {
        if (why) {
            *why = e.what();
        }
        return false;
    }
}

class GlslExec : public ::testing::Test {
protected:
    void SetUp() override {
        src = readShaderFile();
        ASSERT_FALSE(src.empty());
    }
    std::string src;
};

} // namespace

// --- The interpreter itself is strict enough to be trusted --------------------------------

TEST(GlslInterpreter, RejectsGlsl330TypeErrors) {
    EXPECT_THROW((void)glsl::Program("uint f() { uint m = 1; return m; }").call("f", {}), glsl::GlslError);
    EXPECT_THROW((void)glsl::Program("int f() { return 1u & 3; }").call("f", {}), glsl::GlslError);
    EXPECT_THROW((void)glsl::Program("float f() { return 1.0 & 2.0; }").call("f", {}), glsl::GlslError);
    EXPECT_THROW((void)glsl::Program("int f() { ivec2 v = ivec2(1, 2); return v[2]; }").call("f", {}), glsl::GlslError);
    EXPECT_THROW((void)glsl::Program("int f() { return g; }").call("f", {}), glsl::GlslError);
    EXPECT_THROW((void)glsl::Program("int f() { if (1) return 0; return 1; }").call("f", {}), glsl::GlslError);
    EXPECT_THROW((void)glsl::Program("int f() { int a = 0; }").call("f", {}), glsl::GlslError);
    EXPECT_THROW(glsl::Program("int f() { return 0 }"), glsl::GlslError);
}

TEST(GlslInterpreter, EvaluatesValidCodeLikeGlsl) {
    glsl::Program p(R"(
        #define N 4
        int shifts(int a) { return (a >> 2) * N + (a & 3); }
        float conv(int a) { float x = a; return x / 2.0; }
        int trunc(vec2 v) { ivec2 t = ivec2(v); return t.x * 10 + t.y; }
        uint bits(int i) { uint m = 0u; for (int k = 0; k < 3; ++k) { if (k == i) continue; m |= 1u << uint(k); } return m; }
    )");
    EXPECT_EQ(glsl::asInt(p.call("shifts", {glsl::intv(13)})), 13);
    EXPECT_FLOAT_EQ(glsl::asFloat(p.call("conv", {glsl::intv(3)})), 1.5f);
    EXPECT_EQ(glsl::asInt(p.call("trunc", {glsl::vec2v({3.9f, 2.2f})})), 32);
    EXPECT_EQ(glsl::asUInt(p.call("bits", {glsl::intv(1)})), 5u);
}

// --- The real include -----------------------------------------------------------------------

TEST_F(GlslExec, WholeIncludeParsesAndDeclaresTheBlock) {
    glsl::Program p(src);
    ASSERT_NE(p.block(kUboBlockName), nullptr);
    EXPECT_TRUE(p.hasFunction("dynLightAttenuation"));
    EXPECT_TRUE(p.hasFunction("dynTileIndex"));
    EXPECT_TRUE(p.hasFunction("dynTileMask"));
    EXPECT_TRUE(p.hasFunction("computeDynamicLight"));
    UboBuffer empty{};
    packDynamicLightUbo({}, {}, glm::vec2(1.0f), empty);
    EXPECT_NO_THROW(p.bindUniformBlock(kUboBlockName, empty));
}

TEST_F(GlslExec, AttenuationIsBitIdenticalToCpu) {
    UboBuffer ubo{};
    packDynamicLightUbo({}, {}, glm::vec2(1.0f), ubo);
    glsl::Program p = loadProgram(src, ubo);
    std::mt19937 rng(0xA77Eu);
    std::uniform_real_distribution<float> rd(-2.0f, 40.0f);
    std::vector<std::pair<float, float>> cases{{0.0f, 14.0f}, {14.0f, 14.0f}, {20.0f, 14.0f}, {-3.0f, 10.0f},
                                               {1.0f, 0.0f},  {1.0f, -5.0f},  {7.0f, 14.0f}};
    for (int i = 0; i < 2000; ++i) {
        cases.emplace_back(rd(rng), rd(rng));
    }
    for (const auto& [d, r] : cases) {
        const float gpu = glsl::asFloat(p.call("dynLightAttenuation", {glsl::floatv(d), glsl::floatv(r)}));
        ASSERT_EQ(gpu, attenuation(d, r)) << "d=" << d << " R=" << r;
    }
}

TEST_F(GlslExec, AttenuationGuardRejectsNaNRadiusLikeCpu) {
    // `R <= 0.0` is false for NaN, so the shader would feed NaN to clamp() (implementation-
    // defined). The guard must be written so NaN takes the early-out exactly like the CPU.
    UboBuffer ubo{};
    packDynamicLightUbo({}, {}, glm::vec2(1.0f), ubo);
    glsl::Program p = loadProgram(src, ubo);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    float gpu = -1.0f;
    ASSERT_NO_THROW(gpu = glsl::asFloat(p.call("dynLightAttenuation", {glsl::floatv(1.0f), glsl::floatv(nan)})));
    EXPECT_EQ(gpu, attenuation(1.0f, nan));
}

TEST_F(GlslExec, TileIndexMatchesCpuForCommonViewports) {
    const std::vector<glm::vec2> viewports{{1600, 900}, {1920, 1080}, {1366, 768}, {800, 600}, {333, 197}, {2560, 1440}};
    uint32_t seed = 1;
    for (const glm::vec2& vp : viewports) {
        EXPECT_EQ(tileIndexMismatches(src, vp, seed++, 600), 0u) << vp.x << "x" << vp.y;
    }
}

TEST_F(GlslExec, TileIndexMatchesCpuForTinyViewports) {
    // Minimised / transitional windows: tiles would be smaller than one pixel.
    const std::vector<glm::vec2> viewports{{8, 8}, {10, 10}, {15, 8}, {1, 1}, {0, 0}, {3, 200}, {200, 3}};
    uint32_t seed = 100;
    for (const glm::vec2& vp : viewports) {
        EXPECT_EQ(tileIndexMismatches(src, vp, seed++, 400), 0u) << vp.x << "x" << vp.y;
    }
}

TEST_F(GlslExec, TileMasksAndShadingMatchCpuFrame) {
    std::string why;
    EXPECT_TRUE(shaderAgreesWithCpu(src, &why)) << why;
}

TEST_F(GlslExec, ShadingMatchesAcrossRandomFrames) {
    for (uint32_t seed = 1; seed <= 5; ++seed) {
        Scenario sc;
        buildScenario(sc, seed * 7919u, static_cast<int>(seed * 5));
        glsl::Program prog = loadProgram(src, sc.frame.uboBytes());
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> fx(0.0f, sc.viewport.x);
        std::uniform_real_distribution<float> fy(0.0f, sc.viewport.y);
        std::uniform_real_distribution<float> pos(-14.0f, 14.0f);
        for (int s = 0; s < 150; ++s) {
            // Arbitrary (worldPos, fragCoord) pairs exercise the mask path too, not only
            // consistent projections.
            const glm::vec3 p{pos(rng), pos(rng), pos(rng) - 14.0f};
            const glm::vec2 frag{fx(rng), fy(rng)};
            const glm::vec3 gpu =
                glsl::asVec3(prog.call("computeDynamicLight", {glsl::vec3v(p), glsl::vec2v(frag)}));
            const glm::vec3 cpu = sc.frame.shade(p, frag);
            ASSERT_NEAR(gpu.r, cpu.r, 1e-5f * (1.0f + cpu.r)) << "seed " << seed << " sample " << s;
            ASSERT_NEAR(gpu.g, cpu.g, 1e-5f * (1.0f + cpu.g));
            ASSERT_NEAR(gpu.b, cpu.b, 1e-5f * (1.0f + cpu.b));
        }
    }
}

TEST_F(GlslExec, InvalidLightsReachingThePackerNeverPoisonTheShader) {
    // packDynamicLightUbo() is public: a caller bypassing the manager must not be able to
    // upload NaN data that makes the shader's result implementation-defined.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<DynamicLight> lights(3);
    lights[0].position = {0, 0, -3};
    lights[0].radius = 6.0f;
    lights[1].position = {nan, 0, -3};
    lights[1].radius = 6.0f;
    lights[2].position = {0, 0, -3};
    lights[2].radius = nan;
    std::vector<uint32_t> masks(kTileCount, 0x7u);
    UboBuffer ubo{};
    packDynamicLightUbo(lights, masks, glm::vec2(100.0f), ubo);
    glsl::Program prog = loadProgram(src, ubo);
    glm::vec3 gpu{-1.0f};
    ASSERT_NO_THROW(gpu = glsl::asVec3(prog.call("computeDynamicLight", {glsl::vec3v({0, 0, -4}), glsl::vec2v({5, 5})})));
    const glm::vec3 cpu = evaluateDynamicLight({0, 0, -4}, std::span<const DynamicLight>(lights.data(), 1));
    EXPECT_NEAR(gpu.r, cpu.r, 1e-6f);
    EXPECT_NEAR(gpu.g, cpu.g, 1e-6f);
    EXPECT_NEAR(gpu.b, cpu.b, 1e-6f);
}

// --- Mutation testing: plausible shader bugs must be caught ---------------------------------

TEST_F(GlslExec, PlausibleShaderBugsAreDetected) {
    struct Mutation {
        const char* name;
        const char* from;
        const char* to;
    };
    const std::vector<Mutation> mutations{
        {"swapped uvec4 indices", "u_dynTileMasks[t >> 2][t & 3]", "u_dynTileMasks[t & 3][t >> 2]"},
        {"column-major tiles", "tile.y * u_dynTileCols + tile.x", "tile.x * u_dynTileRows + tile.y"},
        {"flipped Y", "ivec2 tile = ivec2(fragCoord / ",
         "ivec2 tile = ivec2(vec2(fragCoord.x, float(u_dynTileRows) * u_dynTileSizePx.y - fragCoord.y) / "},
        {"off-by-one light bit", "(1u << uint(i))", "(1u << uint(i + 1))"},
        {"linear falloff", "return t * t;", "return t;"},
        {"wrong component count", "t & 3]", "t & 1]"},
    };
    for (const Mutation& m : mutations) {
        std::string mutant = src;
        const size_t at = mutant.find(m.from);
        ASSERT_NE(at, std::string::npos) << "mutation target missing: " << m.name;
        mutant.replace(at, std::string(m.from).size(), m.to);
        EXPECT_FALSE(shaderAgreesWithCpu(mutant)) << "undetected mutation: " << m.name;
    }
}
