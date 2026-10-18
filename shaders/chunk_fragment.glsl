#version 330 core

in vec3 v_planarPos;
in vec3 v_normal;
in vec2 v_uv;
flat in uint v_matLayer;
flat in uint v_normIdx;
in vec2 v_light;
in float v_ao;
flat in uint v_biomeTint;
in float v_camDist;

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
    return 0.60; // +/- X
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

    // Compute atlas UV from 16x16 tile grid
    float tileX = float(v_matLayer % 16u) / 16.0;
    float tileY = float(v_matLayer / 16u) / 16.0;
    vec2 atlasUv = vec2(tileX + texCoords.x / 16.0, tileY + texCoords.y / 16.0);

    vec4 tex = texture(uAtlas, atlasUv);
    if (uAlphaCutout == 1 && tex.a < 0.35) {
        discard;
    }

    // Directional shading and Ambient Occlusion
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

    // Beta 1.7.3 light curve combining modulated sky light and warm torch light
    float skyL = pow(clamp(v_light.x * uSunBrightness, 0.05, 1.0), 1.35);
    float blkL = pow(clamp(v_light.y, 0.0, 1.0), 1.25);
    float combinedLight = max(skyL, blkL);

    vec3 warmTorchTint = vec3(1.0, 0.94, 0.82) * (blkL * 0.25);
    vec3 litColor = tex.rgb * shadeColor * combinedLight + tex.rgb * warmTorchTint;

    float fogFactor = clamp((uFogEnd - v_camDist) / max(1.0, (uFogEnd - uFogStart)), 0.0, 1.0);
    vec3 finalRgb = mix(uFogColor, litColor, fogFactor);

    FragColor = vec4(finalRgb, tex.a);
}
