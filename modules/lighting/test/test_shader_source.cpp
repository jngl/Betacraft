// P4.6 — Runtime access to the dynamic-light include and its injection into the REAL chunk
// fragment shader, which WorldRenderer compiles from the inline PACKED_CHUNK_FS string
// (shaders/chunk_fragment.glsl is not loaded by anything).
#include "lighting/DynamicLightUbo.hpp"
#include "lighting/DynamicLightsGlsl.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

using namespace betacraft::lighting;

namespace {

std::string readRepoFile(const char* rel) {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    std::ifstream in(root / rel, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Extracts the raw string literal `constexpr const char* <name> = R"(...)";` (read-only).
std::string inlineShader(const std::string& cpp, const std::string& name) {
    const std::string marker = name + " = R\"(";
    const size_t start = cpp.find(marker);
    if (start == std::string::npos) {
        return {};
    }
    const size_t body = start + marker.size();
    const size_t end = cpp.find(")\";", body);
    return end == std::string::npos ? std::string{} : cpp.substr(body, end - body);
}

// Identifiers the include introduces at global scope.
std::set<std::string> includeGlobals(const std::string& inc) {
    std::set<std::string> names;
    const std::regex decl(R"((?:#define|struct|uniform)\s+(\w+)|\b(?:float|int|uint|vec3|void)\s+(\w+)\s*\()");
    for (std::sregex_iterator it(inc.begin(), inc.end(), decl), end; it != end; ++it) {
        names.insert((*it)[1].matched ? (*it)[1].str() : (*it)[2].str());
    }
    const std::regex member(R"(\b(u_dyn\w+))");
    for (std::sregex_iterator it(inc.begin(), inc.end(), member), end; it != end; ++it) {
        names.insert((*it)[1].str());
    }
    return names;
}

} // namespace

TEST(DynLightShaderSource, EmbeddedCopyIsByteIdenticalToShaderFile) {
    const std::string onDisk = readRepoFile("shaders/dynamic_lights.glsl");
    ASSERT_FALSE(onDisk.empty());
    EXPECT_EQ(std::string(dynamicLightsGlslSource()), onDisk)
        << "shaders/dynamic_lights.glsl changed: update modules/lighting/src/DynamicLightsGlsl.cpp";
}

TEST(DynLightShaderSource, InjectsIntoTheRealInlineChunkFragmentShader) {
    const std::string fs = inlineShader(readRepoFile("src/render/WorldRenderer.cpp"), "PACKED_CHUNK_FS");
    ASSERT_FALSE(fs.empty()) << "PACKED_CHUNK_FS not found in WorldRenderer.cpp";
    const std::string out = injectDynamicLights(fs);
    EXPECT_EQ(out.rfind("#version 330 core", 0), 0u); // still the very first directive
    const size_t blockPos = out.find(std::string("uniform ") + kUboBlockName);
    ASSERT_NE(blockPos, std::string::npos);
    EXPECT_EQ(out.find(std::string("uniform ") + kUboBlockName, blockPos + 1), std::string::npos);
    EXPECT_LT(blockPos, out.find("void main()"));
    EXPECT_NE(out.find("vec3 computeDynamicLight(vec3 worldPos, vec2 fragCoord)"), std::string::npos);
    EXPECT_EQ(out.size(), fs.size() + dynamicLightsGlslSource().size());
}

TEST(DynLightShaderSource, IncludeNamesDoNotCollideWithChunkShaders) {
    // Every global the include adds must be new to both chunk shaders, or the link fails.
    const std::string cpp = readRepoFile("src/render/WorldRenderer.cpp");
    const std::string vs = inlineShader(cpp, "PACKED_CHUNK_VS");
    const std::string fs = inlineShader(cpp, "PACKED_CHUNK_FS");
    ASSERT_FALSE(vs.empty());
    ASSERT_FALSE(fs.empty());
    const auto names = includeGlobals(std::string(dynamicLightsGlslSource()));
    ASSERT_GE(names.size(), 10u);
    for (const std::string& n : names) {
        if (n == "computeDynamicLight") {
            // The integrated FS calls the include's entry point; it must only never redeclare it.
            EXPECT_FALSE(std::regex_search(fs, std::regex(R"(\bvec3\s+computeDynamicLight\s*\()")));
            EXPECT_FALSE(std::regex_search(vs, std::regex(R"(\bcomputeDynamicLight\b)")));
            continue;
        }
        const std::regex word("\\b" + n + "\\b");
        EXPECT_FALSE(std::regex_search(fs, word)) << n << " already used by PACKED_CHUNK_FS";
        EXPECT_FALSE(std::regex_search(vs, word)) << n << " already used by PACKED_CHUNK_VS";
    }
}
