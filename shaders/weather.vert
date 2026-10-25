#version 330 core
// Rain and snow columns (render/WeatherRenderer.cpp keeps an embedded copy).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;     // u across the quad, v = world height + column offset
layout(location = 2) in vec4 aParams; // x alpha, y light 0..1, z kind (0 rain, 1 snow, 2 bolt), w fall speed

uniform mat4 uViewProj;
uniform float uTime;

out vec2 vUV;
out float vAlpha;
out float vLight;
out float vKind;

void main() {
    // Scrolling the pattern up moves the streaks down: v + t * speed.
    vUV = vec2(aUV.x, aUV.y + uTime * aParams.w);
    vAlpha = aParams.x;
    vLight = aParams.y;
    vKind = aParams.z;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
