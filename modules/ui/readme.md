# ui

Input and screens: SDL key bindings and `options.txt`, the screen state machine, title, world select, create world, options and death screens, and the in-game HUD (hotbar, inventory, crafting grid). Drawing goes through `render`.

## Public entry points

- `ui/InputManager.hpp`: `GameAction` to `SDL_Scancode` bindings, rebinding, `options.txt`.
- `ui/ScreenState.hpp`, `ui/ScreenKind.hpp`: the screen state machine.
- `ui/TitleScreen.hpp`, `ui/WorldSelectScreen.hpp`, `ui/CreateWorldScreen.hpp`, `ui/OptionsMenu.hpp`, `ui/DeathScreen.hpp`, `ui/TextField.hpp`.
- `ui/HudRenderer.hpp`: HUD and inventory drawing, `hitTestInventory`.
- `ui/CraftingSystem.hpp`, `ui/CreativePalette.hpp`.

## What it does not do

No world session or tick (`game`): screens report what the player chose and `game::Application` acts on it.
