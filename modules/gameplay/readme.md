# gameplay

Survival rules as pure functions and small value types: the item id scheme, tools and their wear, mining speed, melee damage, knockback, health, armor, food, crops, explosions, mob definitions and spawn rules. None of it sees `World`, so each rule is tested on its own; `world` and `game` feed it from the live state.

## Public entry points

- `gameplay/ItemIds.hpp`: `ItemId`, the engine's single item id scheme (block ids below 256, vanilla Beta item numbers above), stack limits.
- `gameplay/ItemStack.hpp`, `gameplay/ToolDefinitions.hpp`: stacks with tool wear, tool kinds and tiers.
- `gameplay/Harvesting.hpp`: mining progress per tick and what a broken block drops.
- `gameplay/Combat.hpp`, `gameplay/MeleeCombat.hpp`, `gameplay/Knockback.hpp`: weapon damage, melee reach and hits, knockback.
- `gameplay/Health.hpp`, `gameplay/Armor.hpp`, `gameplay/Survival.hpp`, `gameplay/Food.hpp`: damage cooldowns, armor reduction, air, fire, void and suffocation, eating.
- `gameplay/Crops.hpp`, `gameplay/Explosion.hpp`, `gameplay/DeathDrops.hpp`: wheat and farmland, Beta's explosion rays, the death scatter.
- `gameplay/MobDefs.hpp`, `gameplay/MobSpawnRules.hpp`: `MobKind` and its stats and drops, where and when mobs may spawn.

## What it does not do

No entities, ECS systems or world access (that is `ecs`, `world` and `game`), and no crafting recipes (`crafting` re-exports the item ids).
