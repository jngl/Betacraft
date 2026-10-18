#version 330 core
in vec2 vUV;
in vec4 vColor;
in float vUseTexture;
out vec4 FragColor;

uniform sampler2D uMsdfAtlas;
uniform float uPxRange;

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    if (vUseTexture < 0.5) {
        FragColor = vColor;
        return;
    }

    vec3 msd = texture(uMsdfAtlas, vUV).rgb;
    float sd = median(msd.r, msd.g, msd.b);

    vec2 screenTexSize = vec2(1.0) / max(fwidth(vUV), vec2(0.0001));
    vec2 unitRange = vec2(uPxRange) / vec2(textureSize(uMsdfAtlas, 0));
    float screenPxDistance = max(0.5 * dot(unitRange, screenTexSize), 1.0) * (sd - 0.5);
    float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);

    if (opacity < 0.01) {
        discard;
    }
    FragColor = vec4(vColor.rgb, vColor.a * opacity);
}
