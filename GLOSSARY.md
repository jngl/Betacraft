# BetaCraft

A voxel survival and building game inspired by Minecraft: one player exploring, surviving and building in procedurally generated voxel worlds. Minecraft Beta 1.7.3 is the default reference for how mechanics behave (`docs/adr/0002-beta-as-behaviour-reference.md`).

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

**Player focus**:
What the player is doing that decides whether their actions reach the world: playing, in an inventory screen, sleeping, or dead. Pause and other menus are not part of it: the world does not advance under them.
_Avoid_: Input mode, game state, screen state

**Creative palette**:
The grid in the Creative inventory screen that hands out, for free, every block and item a player can hold, grouped into palette categories.
_Avoid_: Creative menu, creative inventory

**Palette category**:
One of the groups the creative palette is sorted into (Building, Decoration, Nature, Redstone, Tools, Combat, Food, Materials); every palette entry belongs to exactly one.
_Avoid_: Tab, section
