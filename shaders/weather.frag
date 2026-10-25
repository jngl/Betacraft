#version 330 core
// Procedural rain streaks and snowflakes (no texture; render/WeatherRenderer.cpp keeps
// an embedded copy).
in vec2 vUV;
in float vAlpha;
in float vLight;
in float vKind;

uniform float uTime;

out vec4 FragColor;

float hash11(float n) {
    return fract(sin(n * 127.1) * 43758.5453);
}

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    if (vKind > 1.5) {
        // Lightning bolt: flat bright blue-white.
        FragColor = vec4(0.85, 0.88, 1.0, vAlpha);
        return;
    }
    float mask = 0.0;
    vec3 color;
    if (vKind < 0.5) {
        // Rain: five lanes across the quad, each with dashes of random length.
        float lanes = 5.0;
        float lane = floor(vUV.x * lanes);
        float lu = fract(vUV.x * lanes);
        float centre = 0.25 + 0.5 * hash11(lane + 3.0);
        float width = 0.07;
        float across = 1.0 - smoothstep(width * 0.5, width, abs(lu - centre));
        float v = vUV.y * 0.6 + hash11(lane) * 17.0;
        float segment = floor(v);
        float on = step(0.35, hash11(segment * 7.3 + lane * 13.1));
        float along = step(fract(v), 0.55);
        mask = across * on * along;
        color = vec3(0.62, 0.70, 0.86);
    } else {
        // Snow: one flake per cell at most, drifting sideways as it falls.
        vec2 p = vec2(vUV.x * 3.0 + 0.15 * sin(vUV.y * 0.9 + uTime * 0.7), vUV.y * 2.0);
        vec2 cell = floor(p);
        vec2 local = fract(p);
        float present = step(0.55, hash21(cell));
        vec2 at = vec2(0.2 + 0.6 * hash21(cell + 5.0), 0.2 + 0.6 * hash21(cell + 9.0));
        float d = length(local - at);
        mask = present * (1.0 - smoothstep(0.06, 0.11, d));
        color = vec3(1.0);
    }
    float alpha = mask * vAlpha;
    if (alpha < 0.01) {
        discard;
    }
    FragColor = vec4(color * vLight, alpha);
}
