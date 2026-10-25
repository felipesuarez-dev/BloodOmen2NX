# Gameplay translation

Inventory of the systems to recover. Status is `todo` until a function is
translated and verified.

| System | Status | Notes |
|---|---|---|
| Boot and front-end | todo | Logos, menus, options, language |
| Level loading and streaming | todo | Container readers, level table |
| Camera | todo | Follow, combat, scripted |
| Player movement | todo | Walk, run, jump, climb, swim |
| Combat | todo | Melee, block, grab, finishers |
| Dark gifts | todo | Unlock and use |
| Weapons and inventory | todo | Pick up, equip, drop |
| Enemies and AI | todo | Behaviours, patrol, combat |
| Scripting and triggers | todo | Level events and dialogue |
| HUD and menus | todo | Health, rage, prompts |
| Saves and checkpoints | todo | Format and flow |
| Audio events | todo | Dialogue, effects, music |

## Rules

- Translate the original, do not redesign it.
- Every row points to at least one `FUN_` address once work starts.
- Behaviour claims need a local decompile and a test or oracle comparison.
- Keep this table current; it is the map of what the port actually does.
