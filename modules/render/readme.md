# render

Everything that talks to OpenGL: GL entry points, shaders, the procedural texture atlas, chunk buffers and draws, sky, weather, particles, mob models and MSDF text. It sits above `streaming` (GL-free meshing) and `lighting` (GL-free dynamic lights), never below them (ADR 0001).

## Public entry points

- `render/GLHeaders.hpp`: GL 3.3 Core entry points, loaded once a context exists.
- `render/WorldRenderer.hpp`: the frame (`renderWorld`): chunk upload and draw through `streaming::WorldMeshStreamer`, entities, selection box, dynamic light UBO.
- `render/ChunkMesher.hpp`: the synchronous reference mesher (`streaming::SnapshotMesher` must match it).
- `render/TextureAtlas.hpp`: tiles generated at startup and exported to `assets/terrain.png` (libpng when found).
- `render/SkyRenderer.hpp`, `render/WeatherRenderer.hpp`, `render/ParticleSystem.hpp`, `render/MobModels.hpp`.
- `render/TextRenderer.hpp`, `render/BitmapFont.hpp`, `render/GuiScale.hpp`: text and HiDPI GUI scale.
- `render/ItemAppearance.hpp`: how an item id is drawn (atlas tile and tint).
- `render/DynamicLightUniformBuffer.hpp`, `render/Shader.hpp`.

## What it does not do

No input, screens or HUD layout (`ui`), no game rules (`game`). No meshing threads: those are `streaming`.
