# Beta 1.7.3 game tick order, and when block-break drops appear

Research for issue #3 (part of map #1, WorldSession). Investigated 2026-10-18.

## Answer in brief

In single-player Beta 1.7.3, one game tick (`Minecraft.runTick`) does three things in this order. It handles player input, which includes mining. It then updates all entities (`World.updateEntities`), and only after that the world (`World.tick`: weather, sleep, mob spawning, time, scheduled block ticks, random block ticks). Block drops are never queued. `dropBlockAsItem` creates the `EntityItem` and adds it to `loadedEntityList` right away, at the moment the block breaks. So:

- **Drops from blocks broken during scheduled ticks, fluid flow or random ticks** exist in the same tick they break, but they do not move until the **next** tick, because entities were already updated earlier in that tick.
- **Drops from blocks broken by the player** (input phase) and **drops spawned during the entity update** (falling sand that cannot land, TNT explosions, mob deaths) move in the **same** tick. `updateEntities` reads the list size again on every loop pass, so entities added during the loop are updated in that same loop.

BetaCraft gets the same "first moves next tick" timing for block-tick and fluid drops, but by a different route. It mixes up several other phases, and it delays the drops recorded in World's queues by one tick: they do not exist during the tick in which their block broke.

## Sources

The main source is the MCP-deobfuscated Beta 1.7.3 client, mirrored at `jacobo-mc/mc_b1.7.3_release` ("The code of Minecraft b1.7.3"), commit `740c583`. Paths below are under `1.7.3-LTS/src/minecraft/net/minecraft/`. Line numbers refer to that commit.

- [S1] `client/Minecraft.java` `runTick()` L944-1178: <https://github.com/jacobo-mc/mc_b1.7.3_release/blob/740c583901e1ff1150e9ef37e37dab5bc0e4f807/1.7.3-LTS/src/minecraft/net/minecraft/client/Minecraft.java#L944-L1178>
- [S2] `src/World.java`: `entityJoinedWorld` L896, `notifyBlockOfNeighborChange` L515, `scheduleBlockUpdate` L1152, `updateEntities` L1178, `tick` L1745, `updateWeather` L1792, `updateBlocksAndPlayCaveSounds` L1868, `TickUpdates` L1967: <https://github.com/jacobo-mc/mc_b1.7.3_release/blob/740c583901e1ff1150e9ef37e37dab5bc0e4f807/1.7.3-LTS/src/minecraft/net/minecraft/src/World.java>
- [S3] `src/NextTickListEntry.java` (`compareTo` sorts by scheduled time, then by a global insertion counter)
- [S4] `src/BlockFlowing.java` `updateTick` L21, `flowIntoBlock` L116-128; `src/BlockFluid.java` `tickRate` L187 (water 5, lava 30)
- [S5] `src/BlockStationary.java` `onNeighborBlockChange` / `setNotStationary`
- [S6] `src/Block.java` `dropBlockAsItemWithChance` L339, `dropBlockAsItem_do` L355
- [S7] `src/BlockFlower.java` `checkFlowerChange` L31; `src/BlockSand.java` `tryToFall` L24; `src/EntityFallingSand.java` `onUpdate` L37; `src/EntityItem.java` `onUpdate` L36; `src/EntityPlayer.java` `onLivingUpdate` L172-211 (item pickup)
- [S8] For contrast, the Beta 1.7.3 dedicated server (Project Poseidon, a CraftBukkit b1.7.3 fork, commit `6a7facd`): `MinecraftServer.java` L604-610 calls `worldserver.doTick()` and then `worldserver.cleanUp()`; `World.java` L1069 `cleanUp()` is the entity update. <https://github.com/retromcorg/Project-Poseidon/blob/6a7facd62fe9ad985fee51ce4b150cb0a4d9c70d/src/main/java/net/minecraft/server/MinecraftServer.java#L604-L610>
- [S9] Minecraft Wiki, "Tick": <https://minecraft.wiki/w/Tick>. It describes the **modern** Java order (world phases first, then entities, then block entities, with fluid ticks in their own queue). It has no Beta-era history. It is cited only to show that the modern order is not the Beta client order.

## Beta 1.7.3 single-player tick, step by step

### `Minecraft.runTick()` [S1]

1. GUI and stats bookkeeping, `getMouseOver`, `playerController.updateController()`. The controller step only rolls the previous block damage forward.
2. **Input.** Screen input, then mouse and keyboard events. `clickMouse(0/1)` handles attack and use. `func_6254_a` (L779, called at L1023) calls `playerController.sendBlockRemoving` while the button is held, so a block mined to completion breaks here and its drop is spawned here.
3. Every 30 ticks, `joinEntityInSurroundings(player)`. Difficulty sync, `entityRenderer.updateRenderer()`, `renderGlobal.updateClouds()`.
4. **`theWorld.updateEntities()`** (L1160).
5. **`theWorld.tick()`** (L1165).
6. `randomDisplayUpdates` (client particles), `effectRenderer.updateEffects()`.

Entities therefore run **before** the world tick on the client. The dedicated server does it the other way round [S8]. BetaCraft is single-player, so the client is the reference. See "Open uncertainties".

### `World.updateEntities()` [S2 L1178]

1. Weather effects (lightning bolts) `onUpdate`.
2. Remove the entities queued for unload.
3. Loop `for (i = 0; i < loadedEntityList.size(); ++i)`: update each live entity, and remove it if it is dead. The bound is read again on every pass, so **entities added during the loop are updated in the same loop**. That covers a falling sand's drop, an explosion's drops and a mob's loot. The player is one of these entities. Item pickup happens inside the player's own update (`EntityPlayer.onLivingUpdate` → `onCollideWithPlayer`) [S7].
4. **Tile entities** (furnaces and others) `updateEntity()`, **after** all entities. Then pending tile entities are added and removed.

### `World.tick()` [S2 L1745]

1. `updateWeather()`: rain and thunder timers and strengths.
2. Sleep check. If every player is fully asleep: sleep-spawning, or jump time to the next morning and `wakeUpAllPlayers()`.
3. `SpawnerAnimals.performSpawning` (natural mob spawning).
4. `unload100OldestChunks`.
5. Recompute `skylightSubtracted`.
6. `worldTime + 1`, then autosave if due.
7. **`TickUpdates(false)`**: the scheduled block ticks.
8. **`updateBlocksAndPlayCaveSounds()`**: the random block ticks.

**`TickUpdates`** (L1967). There is **one** `TreeSet` for every scheduled tick: fluids, sand and gravel, redstone torches and repeaters, buttons, plates. Entries are ordered by `scheduledTime`, then by global insertion id [S3], so equal-time entries run first-in, first-out. At most 1000 entries run per tick. An entry runs only if the chunks within ±8 exist and the block id still matches.

**`updateBlocksAndPlayCaveSounds`** (L1868). For every chunk within ±9 chunks of each player (19×19), in `HashSet` iteration order:
- cave ambience;
- a 1/100000 chance of lightning while it thunders, which adds an `EntityLightningBolt` to the weather effects;
- a 1/16 chance of snow or ice;
- **80 random block ticks per chunk**, at LCG positions over the whole 16×16×128 chunk, each calling `updateTick` when `Block.tickOnLoad[id]` is set.

### Fluids [S4, S5]

- `BlockFlowing.updateTick` runs from the shared scheduled-tick queue. It reschedules itself with `scheduleBlockUpdate(..., tickRate())` (water 5 ticks, lava 30) and spreads with `flowIntoBlock`.
- `flowIntoBlock`: if the target is not air, lava calls `triggerLavaMixEffects`, and water calls `Block.dropBlockAsItem` on the displaced block **synchronously**. Then it runs `setBlockAndMetadataWithNotify`.
- `BlockStationary.onNeighborBlockChange` → `setNotStationary` swaps the still fluid for its flowing id. While it does so, `editingBlocks = true` suppresses neighbour notifications (`notifyBlockOfNeighborChange` returns early, S2 L515). It then **schedules** the flowing tick on the shared queue after `tickRate()`. That scheduled tick is not immediate: only `scheduledUpdatesAreImmediate`, used by world generation, makes `scheduleBlockUpdate` run at once (S2 L1155).
- Still lava (`BlockStationary`) is a random-tick block, used for fire spread. Still water is not.

### Drops [S6, S7]

- `dropBlockAsItemWithChance` → `dropBlockAsItem_do` builds an `EntityItem` with `delayBeforeCanPickup = 10` and calls `world.entityJoinedWorld`. That call adds the item to its chunk and to `loadedEntityList` **immediately**, provided the chunk is loaded (S2 L896).
- Broken support is handled the same way, synchronously. `setBlockWithNotify` → `notifyBlocksOfNeighborChange` → for example `BlockFlower.checkFlowerChange` → `dropBlockAsItem` + `setBlockWithNotify(0)`. The whole cascade, with its drops, happens inside whichever phase made the first edit.
- Sand: the scheduled `BlockSand.updateTick` → `tryToFall` spawns an `EntityFallingSand` with `entityJoinedWorld` during `TickUpdates`. That entity first updates in the next tick's `updateEntities`. When it lands and cannot place itself, `dropItem` spawns an item during `updateEntities`, and that item updates in the same tick.
- `EntityItem.onUpdate` handles motion, the pickup delay countdown and `age` (despawn at 6000).

### When a block-break drop first exists, and first moves

| Where the block breaks (Beta client) | Item exists from | First `onUpdate` |
|---|---|---|
| Player mining (input phase) | tick N, before entities | tick N |
| Entity update: falling sand that cannot place, TNT or creeper explosion, mob loot | tick N, during entities | tick N (added to the list being looped) |
| Scheduled tick: fluid wash-away, sand leaving support, redstone or button cascades | tick N, during `World.tick` | tick N+1 |
| Random tick: leaf decay, crops in darkness, cactus | tick N, during `World.tick` | tick N+1 |
| Neighbour-change cascade: flowers, torches or reeds above a broken block | same tick and phase as the triggering edit | follows the triggering phase |

## BetaCraft's current order (commit `6008ecb`)

`Application::update` runs `fixedUpdate` at 60 Hz (player movement, particles), then `gameTick()` at 20 Hz. `m_timeOfDay` advances **every frame** by real `dt`, not every tick (`src/game/Application.cpp` ~L1276). One `Application::gameTick()` (`src/game/Application.cpp` L1017-1058) does:

1. Latch the attack actions, `++m_gameTicks`, `world::tickWeather`, `m_session.setWeather`.
2. `GameSession::tick` (`src/game/GameSession.cpp` L65-92):
   1. pending events, `tickPlayerSurvival`;
   2. `handleAttack`: mining, which spawns the mined block's drop directly (L577);
   3. `tickBlockEntities` (furnaces);
   4. **`spawnUnsupportedDrops`**: empties `World::takeUnsupportedBreaks`. These come from the player's edits this tick, **and from last tick's** `World::tick` / `tickRandomBlocks` (leaf decay and crops via `breakBlockNaturally`, flora above a removed block, piston-destroyed blocks);
   5. **`spawnRemovedBlockEntityDrops`**: empties `World::takeRemovedBlockEntities`. Again, this tick's player edits plus last tick's world edits;
   6. `runEntitySystems`: mob AI, attacks, creeper blasts, movement physics, voxel collision, arrows, burning, health and death loot, **natural spawning**, void kill, command flush, and `ItemDropSystem` (pickup delay and age);
   7. `tickFallingBlocks`: first empties **`World::takeFallingBlockSpawns`** (queued by last tick's `World::tick`), then steps every falling block. A falling block that lands and cannot place spawns its drop here, **after** the item and physics systems;
   8. `tickPressurePlates`, `tickPrimedTnt`, `runExplosionQueue` (explosion drops spawn here, after the entity systems), `pickUpItems`, `tickSleep`.
3. Handle `wokeInBed` (time reset), the Sleeping screen and death; `closeContainerIfOutOfReach`.
4. `World::tick` (`src/world/World.cpp` L202): `++m_tickCount`, then due entries from `m_scheduledTicks` (sand, redstone; at most 512), then `RedstoneEngine::tick`, then **`FluidSimulator::tick`, a separate fluid queue** (at most 256 updates plus a search-node budget).
5. `World::tickRandomBlocks` (L313): 80 per chunk spread over the 8 sections, ±8 chunks, chunks sorted by coordinates.
6. `takeFluidEvents` → **`spawnFluidDrops` straight away** (wash-away drops), then lava fizz sounds.
7. `playRedstoneEffects`: empties `takeRedstoneEvents`. These are sounds only (click, door, piston, torch burnout) and never cause drops.
8. `tickWeatherEffects`: the lightning roll, bolt and thunder.

## Where BetaCraft differs from Beta

Ordered by how much each one matters for WorldSession.

1. **Entities and world are in the right order, but other phases surround them differently.** Beta runs input → entities → world. BetaCraft runs input → (block entities, queue draining) → entities → (TNT, explosions, pickup, sleep) → world. The core entities-before-world order matches Beta, which is good. Several phases are attached to the wrong side, though (items 4-8).
2. **World-tick and random-tick drops are delayed by one tick.** In Beta, an unsupported break, leaf decay, crop break or piston-destroyed block makes its `EntityItem` exist at tick N, during `World.tick`. In BetaCraft the break is queued and only spawned at the start of `GameSession::tick` in tick N+1. The **first movement** lands on N+1 in both. However, for one tick the item does not exist: it is not rendered, not saved, and not collectable by anything that runs at the end of tick N. The drop RNG is also consumed in a different order.
3. **Falling-block spawns are delayed the same way.** Beta spawns the `EntityFallingSand` during `TickUpdates` at tick N and updates it first at N+1. BetaCraft queues the spawn at N and spawns and steps it at N+1 in `tickFallingBlocks`. The first step lands on the same tick, but there is a one-tick gap with no entity, and the block is already air.
4. **Removed block-entity drops (chests, furnaces).** Beta drops the contents synchronously, in the phase that removed the block. BetaCraft queues them. A removal by world logic (fluids, pistons) spawns its drops a tick late. A removal by an explosion, which `runExplosionQueue` makes after `spawnRemovedBlockEntityDrops`, also spawns a tick late. Beta spawns those during the entity update and updates them in the same tick.
5. **Drops from explosions and landed falling blocks move a tick late.** Beta spawns these during `updateEntities`, and they update in the same loop. BetaCraft spawns them after `ItemDropSystem` and the physics have run (`runExplosionQueue` and `tickFallingBlocks` come after `runEntitySystems`), so they first move at N+1.
6. **One scheduled-tick queue against three.** Beta runs fluids, sand and redstone from one queue ordered by (time, insertion id), with a cap of 1000 a tick. BetaCraft runs `m_scheduledTicks` (cap 512), then the redstone engine, then a separate `FluidSimulator` queue (cap 256 plus a search budget). Within a tick, every fluid update now comes after every sand and redstone tick, whatever order they were scheduled in.
7. **Block entities run before entities.** BetaCraft calls `tickBlockEntities` before `runEntitySystems`. Beta updates tile entities after all entities, at the end of `updateEntities`.
8. **World-phase work is done outside the world phase.** In Beta, the client runs all of these inside `World.tick`, after entities:
   - weather: BetaCraft runs `tickWeather` before the session tick;
   - the sleep check and wake: BetaCraft runs `tickSleep` at the end of `GameSession::tick`, and handles waking before `World::tick`;
   - natural mob spawning: BetaCraft runs it inside `runEntitySystems`, before the item system;
   - the lightning roll: BetaCraft runs it in `tickWeatherEffects`, after the world tick, rather than inside the random-tick chunk loop.
9. **Time of day.** Beta advances `worldTime` by 1 in `World.tick`, after the sleep check and before scheduled ticks. BetaCraft advances `m_timeOfDay` per frame by wall-clock `dt`, so the skylight used for random ticks is not tick-locked.
10. **Smaller constants.**
    - Random-tick radius: ±8 chunks against Beta's ±9.
    - Scheduled-tick cap: 512 against 1000.
    - Chunk order: sorted by coordinates against Beta's `HashSet` order. This one is a deliberate choice for reproducibility.
    - Item pickup: BetaCraft runs `pickUpItems` after the entity systems; Beta does it inside the player's own entity update. The 10-tick pickup delay makes this hard to see.
    - Player movement: runs at 60 Hz in `fixedUpdate`, outside the game tick. Beta updates the player inside `updateEntities` at 20 Hz. This is out of scope here.

## Recommended Beta-faithful order for WorldSession

Model one WorldSession tick on `Minecraft.runTick`, and empty each World queue **in the same tick, right after the phase that fills it**. That is the closest a queue can get to Beta's synchronous `entityJoinedWorld`.

1. **Input phase.** Latch actions, then `handleAttack` (mining). Then empty `takeUnsupportedBreaks` and `takeRemovedBlockEntities` at once, so that the flora above a mined block and a mined chest's contents spawn before entities, as in Beta.
2. **Entity phase** (= `updateEntities`):
   1. weather effects (the lightning bolt's life);
   2. all entities: mobs, arrows, items, falling blocks (landing and their drops), primed TNT and the explosions they cause, death loot, the player's item pickup;
   3. make sure drops spawned in this phase are still moved in this phase. Either run explosions and falling-block landing before `MovementPhysicsSystem` and `ItemDropSystem`, or run a second item pass. Empty `takeUnsupportedBreaks` and `takeRemovedBlockEntities` after explosions and falling-block placement, inside this phase;
   4. **block entities** (furnaces) last.
3. **World phase** (= `World.tick`), in this order: weather → sleep check and wake → natural mob spawning → skylight → `time + 1` → scheduled ticks → random ticks (including the lightning roll and snow and ice).
4. **Empty the world-phase queues straight after the world phase, in the same tick:** `takeFallingBlockSpawns` → `takeUnsupportedBreaks` → `takeRemovedBlockEntities` → fluid `BlockWashedAway` drops (`takeFluidEvents`). This order follows the order in which Beta would have created them: scheduled ticks first (sand, then the cascades they trigger), with fluid updates part of the same scheduled pass. These entities then exist at the end of tick N and are first updated in tick N+1's entity phase, exactly as in Beta.
5. **Effects last:** `takeRedstoneEvents` and the fluid `Fizz` events go to sound and particles. These are effects only, so their position does not change the simulation.

For strict Beta fidelity inside step 4, World would need **one ordered event stream**, or a merged scheduled-tick queue for fluids, sand and redstone, rather than separate queues. The order of spawns within a tick decides RNG consumption and the order of the entity list. Emptying all queues at the end of the world phase gets every visible timing right. A merged queue is needed only if exact per-tick RNG parity matters.

## Open uncertainties

- **Client or server reference.** The single-player client runs entities, then world [S1]. The dedicated server runs world, then entities [S8], so on a server, drops from block ticks move in the same tick. The recommendation above follows the client, because BetaCraft is single-player. The map should confirm that choice.
- **The decompiled mirror is unofficial.** `jacobo-mc/mc_b1.7.3_release` is an MCP-style decompile from a community repo, not Mojang source. The method bodies quoted here match other MCP b1.7.3 decompiles found by code search (for example `Dereku/minecraft-1.7.3b`, `SnowshoeIceboot/mcmods`), but they were not checked against a fresh MCP 4.3 run.
- **`tickPressurePlates` and arrows.** Their exact Beta counterparts (`BlockPressurePlate` scheduled ticks and entity collision) were not traced. They are assumed to belong to the entity phase and the scheduled ticks respectively.
- **The cost of moving the time of day onto ticks** (difference 9) is a design choice for the WorldSession spec. Beta-faithfulness alone does not settle it.
