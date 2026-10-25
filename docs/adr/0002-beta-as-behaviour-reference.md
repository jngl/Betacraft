# Minecraft Beta 1.7.3 is the default behaviour reference, not a target

Betacraft is a game inspired by Minecraft, not a clone. Many of its mechanics (tick order, fluids, redstone, mining speeds, recipes, mob spawning) were first written to match Minecraft Beta 1.7.3, and the code and tests still cite the Beta classes they follow (`BlockFlowing`, `ItemPickaxe`, `TileEntityFurnace`…). We keep Beta 1.7.3 as the reference for any mechanic we have not deliberately redesigned: when a rule is unspecified, do what Beta does. A change that departs from Beta on purpose is allowed, and it updates the tests that pinned the Beta value with a comment saying why.

The player never sees the reference: the name, window title, title screen, HUD and logs say "Betacraft" with the project's own version (`project(... VERSION ...)` → `core/Version.hpp`), and no Minecraft assets, names or trademarks ship with the game.

## Considered Options

- **Strict parity with Beta 1.7.3** would make every gameplay change a bug and keep the project a clone.
- **Dropping the reference entirely** would leave hundreds of tuned values and tests without a rationale, and every unspecified rule open to debate.

## Consequences

- Readmes and docs describe what a mechanic does, not that it copies Beta. Comments and tests may cite the Beta class or value a rule comes from.
- A deliberate departure from Beta is a design decision: record it (in the test, and in an ADR when it changes how a whole system behaves).
