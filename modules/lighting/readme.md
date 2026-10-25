# lighting

Dynamic lights: held torches, dropped glowing items and burning entities, shaded per fragment so chunks never re-mesh when a light moves. The module does all the CPU work (selecting the 16 nearest lights, Forward+ tile culling, std140 packing) and holds the GLSL include; it never touches OpenGL, the uniform buffer itself lives in `render`.

## Public entry points

- `lighting/DynamicLight.hpp`: `DynamicLight`, attenuation and the CPU reference of the shader's lighting.
- `lighting/DynamicLightManager.hpp`: collects the frame's lights and selects the ones that fit.
- `lighting/DynamicLightFrame.hpp`: one frame from selection to the bytes to upload.
- `lighting/TileLightCuller.hpp`, `lighting/DynamicLightUbo.hpp`, `lighting/Std140.hpp`: screen tile masks, the uniform block layout and the std140 rules it is derived from.
- `lighting/DynamicLightsGlsl.hpp`, `lighting/ShaderInclude.hpp`: the embedded copy of `shaders/dynamic_lights.glsl` and how to splice it into a shader.

## What it does not do

No block light or skylight propagation: that is `DifferentialLightingEngine` in `world`. No GL calls (`render/DynamicLightUniformBuffer`).
