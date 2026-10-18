// P4.6 — Contract between shaders/dynamic_lights.glsl and the C++ std140 packer. The shader is
// parsed, its UBO members are replayed through std140::LayoutBuilder and every offset must
// match the constants used by packDynamicLightUbo().
#include "lighting/DynamicLightUbo.hpp"
#include "lighting/Std140.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace betacraft::lighting;

namespace {

std::filesystem::path shaderPath() {
    // CMake passes absolute source paths, so __FILE__ locates the repository root.
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "shaders" /
           "dynamic_lights.glsl";
}

std::string readShader() {
    std::ifstream in(shaderPath());
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string stripComments(const std::string& src) {
    std::string s = std::regex_replace(src, std::regex(R"(/\*[\s\S]*?\*/)"), " ");
    return std::regex_replace(s, std::regex(R"(//[^\n]*)"), " ");
}

std::map<std::string, std::string> parseDefines(const std::string& src) {
    std::map<std::string, std::string> defs;
    const std::regex re(R"(#define\s+(\w+)\s+([^\s]+))");
    for (std::sregex_iterator it(src.begin(), src.end(), re), end; it != end; ++it) {
        defs[(*it)[1]] = (*it)[2];
    }
    return defs;
}

struct Member {
    std::string type;
    std::string name;
    std::optional<size_t> arraySize;
};

// Parses the body between `<header> {` and the matching `};`.
std::optional<std::vector<Member>> parseBody(const std::string& src, const std::regex& header,
                                             const std::map<std::string, std::string>& defs) {
    std::smatch m;
    if (!std::regex_search(src, m, header)) {
        return std::nullopt;
    }
    const size_t open = static_cast<size_t>(m.position(0) + m.length(0));
    const size_t close = src.find('}', open);
    if (close == std::string::npos) {
        return std::nullopt;
    }
    const std::string body = src.substr(open, close - open);
    std::vector<Member> members;
    const std::regex memberRe(R"((\w+)\s+(\w+)\s*(?:\[\s*(\w+)\s*\])?\s*;)");
    for (std::sregex_iterator it(body.begin(), body.end(), memberRe), end; it != end; ++it) {
        Member mem{(*it)[1], (*it)[2], std::nullopt};
        if ((*it)[3].matched) {
            std::string sz = (*it)[3];
            if (auto d = defs.find(sz); d != defs.end()) {
                sz = d->second;
            }
            mem.arraySize = std::stoul(sz);
        }
        members.push_back(mem);
    }
    return members;
}

std::optional<std140::Type> toStd140(const std::string& glslType) {
    static const std::map<std::string, std140::Type> table{
        {"float", std140::Type::Float}, {"int", std140::Type::Int},     {"uint", std140::Type::UInt},
        {"vec2", std140::Type::Vec2},   {"vec3", std140::Type::Vec3},   {"vec4", std140::Type::Vec4},
        {"ivec4", std140::Type::IVec4}, {"uvec4", std140::Type::UVec4}, {"mat4", std140::Type::Mat4},
    };
    if (auto it = table.find(glslType); it != table.end()) {
        return it->second;
    }
    return std::nullopt;
}

class GlslContract : public ::testing::Test {
protected:
    void SetUp() override {
        raw = readShader();
        ASSERT_FALSE(raw.empty()) << "Cannot read " << shaderPath();
        src = stripComments(raw);
        defs = parseDefines(src);
    }
    std::string raw;
    std::string src;
    std::map<std::string, std::string> defs;
};

} // namespace

TEST_F(GlslContract, IsAnIncludeWithoutVersionDirective) {
    EXPECT_EQ(src.find("#version"), std::string::npos);
}

TEST_F(GlslContract, DefinesMatchCppConstants) {
    ASSERT_TRUE(defs.count("DYN_MAX_LIGHTS"));
    ASSERT_TRUE(defs.count("DYN_TILE_COLS"));
    ASSERT_TRUE(defs.count("DYN_TILE_ROWS"));
    ASSERT_TRUE(defs.count("DYN_TILE_MASK_VEC4S"));
    EXPECT_EQ(std::stoul(defs["DYN_MAX_LIGHTS"]), kMaxDynamicLights);
    EXPECT_EQ(std::stoul(defs["DYN_TILE_COLS"]), kTileCols);
    EXPECT_EQ(std::stoul(defs["DYN_TILE_ROWS"]), kTileRows);
    EXPECT_EQ(std::stoul(defs["DYN_TILE_MASK_VEC4S"]), kTileMaskVec4Count);
}

TEST_F(GlslContract, LightStructMemberOrderAndOffsets) {
    const auto members = parseBody(src, std::regex(R"(struct\s+DynamicLightGPU\s*\{)"), defs);
    ASSERT_TRUE(members.has_value()) << "struct DynamicLightGPU not found";
    ASSERT_EQ(members->size(), 4u);
    const std::vector<std::pair<std::string, std::string>> expected{
        {"vec3", "position"}, {"float", "radius"}, {"vec3", "color"}, {"float", "intensity"}};
    const std::vector<size_t> offsets{ubo::kLightPositionOffset, ubo::kLightRadiusOffset, ubo::kLightColorOffset,
                                      ubo::kLightIntensityOffset};
    std140::LayoutBuilder b;
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ((*members)[i].type, expected[i].first);
        EXPECT_EQ((*members)[i].name, expected[i].second);
        EXPECT_FALSE((*members)[i].arraySize.has_value());
        const auto t = toStd140((*members)[i].type);
        ASSERT_TRUE(t.has_value());
        EXPECT_EQ(b.add(*t), offsets[i]) << (*members)[i].name;
    }
    EXPECT_EQ(b.structSize(), ubo::kLightStride);
}

TEST_F(GlslContract, BlockIsStd140WithExpectedName) {
    const std::regex re(R"(layout\s*\(\s*std140\s*\)\s*uniform\s+(\w+)\s*\{)");
    std::smatch m;
    ASSERT_TRUE(std::regex_search(src, m, re));
    EXPECT_EQ(m[1].str(), std::string(kUboBlockName));
}

TEST_F(GlslContract, BlockMemberOrderArraySizesAndOffsets) {
    const auto members =
        parseBody(src, std::regex(R"(layout\s*\(\s*std140\s*\)\s*uniform\s+DynamicLightBlock\s*\{)"), defs);
    ASSERT_TRUE(members.has_value());
    struct Expect {
        std::string type;
        std::string name;
        std::optional<size_t> arraySize;
        size_t offset;
    };
    const std::vector<Expect> expected{
        {"DynamicLightGPU", "u_dynLights", kMaxDynamicLights, ubo::kLightsOffset},
        {"int", "u_dynLightCount", std::nullopt, ubo::kLightCountOffset},
        {"int", "u_dynTileCols", std::nullopt, ubo::kTileColsOffset},
        {"int", "u_dynTileRows", std::nullopt, ubo::kTileRowsOffset},
        {"vec2", "u_dynTileSizePx", std::nullopt, ubo::kTileSizeOffset},
        {"uvec4", "u_dynTileMasks", kTileMaskVec4Count, ubo::kTileMasksOffset},
    };
    ASSERT_EQ(members->size(), expected.size());
    std140::LayoutBuilder b;
    for (size_t i = 0; i < expected.size(); ++i) {
        const Member& mem = (*members)[i];
        EXPECT_EQ(mem.type, expected[i].type) << i;
        EXPECT_EQ(mem.name, expected[i].name) << i;
        EXPECT_EQ(mem.arraySize, expected[i].arraySize) << mem.name;
        size_t offset = 0;
        if (mem.type == "DynamicLightGPU") {
            offset = b.addStructArray(ubo::kLightStride, mem.arraySize.value_or(1));
        } else if (const auto t = toStd140(mem.type); t && mem.arraySize) {
            offset = b.addArray(*t, *mem.arraySize);
        } else if (t) {
            offset = b.add(*t);
        } else {
            FAIL() << "Unsupported GLSL type " << mem.type;
        }
        EXPECT_EQ(offset, expected[i].offset) << mem.name;
    }
    EXPECT_EQ(b.structSize(), ubo::kBlockSize);
}

TEST_F(GlslContract, ProvidesSharedHelperFunctions) {
    EXPECT_NE(src.find("float dynLightAttenuation(float"), std::string::npos);
    EXPECT_NE(src.find("int dynTileIndex(vec2"), std::string::npos);
    EXPECT_NE(src.find("vec3 computeDynamicLight(vec3"), std::string::npos);
}

TEST_F(GlslContract, AttenuationUsesSpecQuadraticFalloff) {
    // (1 - d/R)^2 with the same clamp and R <= 0 guard as lighting::attenuation().
    const std::regex fn(R"(float\s+dynLightAttenuation\s*\([^)]*\)\s*\{([^}]*)\})");
    std::smatch m;
    ASSERT_TRUE(std::regex_search(src, m, fn));
    const std::string body = m[1];
    EXPECT_NE(body.find("!(R > 0.0)"), std::string::npos); // NaN-safe guard, like the CPU
    EXPECT_NE(body.find("clamp(1.0 - d / R, 0.0, 1.0)"), std::string::npos);
    EXPECT_NE(body.find("t * t"), std::string::npos);
}
