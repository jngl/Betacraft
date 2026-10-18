#pragma once

#include <string>
#include <string_view>

namespace betacraft::lighting {

// Embedded copy of shaders/dynamic_lights.glsl. The engine compiles its chunk shaders from
// inline strings (WorldRenderer's PACKED_CHUNK_FS), not from shaders/*.glsl, so integration
// must not depend on the working directory to find the include. A unit test keeps this copy
// byte-identical to the file on disk.
[[nodiscard]] std::string_view dynamicLightsGlslSource() noexcept;

// injectShaderInclude(fragmentShader, dynamicLightsGlslSource()): what WorldRenderer passes to
// Shader::compileFromSource() instead of PACKED_CHUNK_FS.
[[nodiscard]] std::string injectDynamicLights(std::string_view fragmentShader);

} // namespace betacraft::lighting
