# physics

The player as a physical body: AABB movement against the world's collision boxes at the fixed 20 TPS step, plus the ray cast that tells what the crosshair points at.

## Public entry points

- `physics/Player.hpp`: `Player` (position, velocity, inventory, fall distance, creative flag `isCreative()`), stepped by `update` against a `World`.
- `physics/Raycaster.hpp`: `Raycaster::cast`, the block a ray stops on (solid blocks, or also liquid sources for the bucket).

## What it does not do

No mobs or items (`ecs`), no damage rules (`gameplay`; player damage goes through `game/EntitySimulation::damagePlayer`).
