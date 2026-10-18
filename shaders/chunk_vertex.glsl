#version 330 core

layout(location = 0) in uvec2 a_packedData;

uniform mat4 u_viewProjection;
uniform ivec3 u_sectionOriginWorld;
uniform vec3 uCameraPos;

out vec3 v_planarPos;
out vec3 v_normal;
out vec2 v_uv;
flat out uint v_matLayer;
flat out uint v_normIdx;
out vec2 v_light; // x = sky, y = block
out float v_ao;
flat out uint v_biomeTint;
out float v_camDist;

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

    v_normIdx = normIdx;
    v_normal = NORMALS[min(normIdx, 6u)];
    v_ao = AO_LUT[aoIdx];
    v_light = vec2(skyL, blkL);

    // Planar orthogonal coordinates for fractal UV tiling
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
