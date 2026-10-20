# math

Pure geometry and noise: axis-aligned boxes, view-frustum culling and Perlin noise. Nothing here knows what a block is.

## Public entry points

- `math/AABB.hpp`: `AABB` (intersection, translation, expansion).
- `math/Frustum.hpp`: `Frustum`, updated from a view-projection matrix, tests boxes against it.
- `math/PerlinNoise.hpp`: seeded `PerlinNoise` (2D/3D noise and fBm) used by world generation.

## What it does not do

No block-specific boxes: a block's collision and selection boxes are in `voxel/BlockBoxes.hpp`. No game or rendering state.
