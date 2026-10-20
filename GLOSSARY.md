# BetaCraft

A Minecraft Beta 1.7.3 clone: one player exploring, surviving and building in procedurally generated voxel worlds.

## Language

**World session**:
One open world and everything that lives and advances in it — terrain, the player, entities, time of day, weather and spawn points — from opening its save until leaving it.
_Avoid_: Level, game session

**World**:
The voxel terrain of one save: its chunks, blocks, light and fluids.
_Avoid_: Map, level

**Game tick**:
One fixed 1/20 s step in which the world session advances, independent of the frame rate.
_Avoid_: Update, frame

**Player step**:
One fixed 1/60 s step in which the player's movement and collisions advance, three to a game tick, so motion stays smooth between game ticks.
_Avoid_: Physics tick, fixed update

**Entity phase**:
The part of a game tick in which entities move and act — mobs, dropped items, projectiles, primed TNT, explosions — followed by block entities such as furnaces.
_Avoid_: Entity update

**World phase**:
The part of a game tick, after the entity phase, in which the world itself advances: weather, sleep, mob spawning, time of day, scheduled block updates and random block ticks.
_Avoid_: World update, block tick

**Entity simulation**:
The part of a world session that moves and resolves entities — mobs, dropped items, projectiles, primed TNT, falling blocks — together with the player's physical actions on blocks and entities: mining, attacking, using and dropping items, and taking damage.
_Avoid_: Game session, gameplay
