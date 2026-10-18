// dynamic_lights.glsl — P4.6 real-time dynamic lights (held torch, dropped glowstone...).
//
// Include file (GLSL 3.30 has no #include): splice it right after the `#version` line of the
// chunk fragment shader, e.g. with lighting::injectShaderInclude(). The block layout is
// mirrored byte-for-byte by src/lighting/DynamicLightUbo.hpp and enforced by
// tests/phase2/dynlight/test_glsl_contract.cpp — change both together.
//
// Bind with glUniformBlockBinding(program, index("DynamicLightBlock"), 3).

#define DYN_MAX_LIGHTS 16
#define DYN_TILE_COLS 16
#define DYN_TILE_ROWS 9
#define DYN_TILE_MASK_VEC4S 36

struct DynamicLightGPU {
    vec3  position;   // World space
    float radius;     // Contribution reaches zero at this distance (blocks)
    vec3  color;      // Linear RGB
    float intensity;
};

layout(std140) uniform DynamicLightBlock {
    DynamicLightGPU u_dynLights[DYN_MAX_LIGHTS];
    int   u_dynLightCount;
    int   u_dynTileCols;
    int   u_dynTileRows;
    vec2  u_dynTileSizePx;
    uvec4 u_dynTileMasks[DYN_TILE_MASK_VEC4S]; // 4 tiles per uvec4, bit i == light i
};

// Spec P4.6: Light = Color * (1 - d/R)^2. Must match lighting::attenuation() on the CPU.
// `!(R > 0.0)` (not `R <= 0.0`) so a NaN radius also takes the early-out where IEEE NaN
// compares are honoured; the packer additionally uploads invalid lights as all-zero slots.
float dynLightAttenuation(float d, float R) {
    if (!(R > 0.0)) return 0.0;
    float t = clamp(1.0 - d / R, 0.0, 1.0);
    return t * t;
}

// Screen tile of a fragment; row 0 is the bottom row like gl_FragCoord.
int dynTileIndex(vec2 fragCoord) {
    ivec2 tile = ivec2(fragCoord / max(u_dynTileSizePx, vec2(1.0)));
    tile = clamp(tile, ivec2(0), ivec2(u_dynTileCols - 1, u_dynTileRows - 1));
    return tile.y * u_dynTileCols + tile.x;
}

uint dynTileMask(vec2 fragCoord) {
    int t = dynTileIndex(fragCoord);
    return u_dynTileMasks[t >> 2][t & 3];
}

// Sum of the dynamic lights reaching `worldPos`, culled by the fragment's Forward+ tile.
vec3 computeDynamicLight(vec3 worldPos, vec2 fragCoord) {
    uint mask = dynTileMask(fragCoord);
    vec3 sum = vec3(0.0);
    if (mask == 0u) return sum;
    int count = min(u_dynLightCount, DYN_MAX_LIGHTS);
    for (int i = 0; i < count; ++i) {
        if ((mask & (1u << uint(i))) == 0u) continue;
        DynamicLightGPU l = u_dynLights[i];
        float d = length(worldPos - l.position);
        sum += l.color * (l.intensity * dynLightAttenuation(d, l.radius));
    }
    return sum;
}
