#pragma once

#include <string>
#include <string_view>

namespace betacraft::lighting {

// GLSL 3.30 core has no #include. Splices `include` right after the `#version` line of
// `shaderSrc` (the directive must stay first), or prepends it when there is no #version.
// Used by the integration step to inject shaders/dynamic_lights.glsl into the chunk shader.
[[nodiscard]] std::string injectShaderInclude(std::string_view shaderSrc, std::string_view include);

} // namespace betacraft::lighting
