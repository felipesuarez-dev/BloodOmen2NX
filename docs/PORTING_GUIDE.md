# Porting guide: the per-function translation loop

Mandatory workflow for translating original code into `src/`.

1. Pick a function from `re/FUNCTION_MAP.csv` with status `todo`, highest
   subsystem priority first.
2. Read it in Ghidra and cross-check every string, RTTI entry and caller
   that reveals its purpose.
3. Translate to C++ in the subsystem's file, preserving structure, control
   flow and data layout. Use real names when evidence provides them;
   otherwise `fn_<address>`. No design improvements, no cleanup refactors.
4. Pure logic gets a unit test with a synthetic fixture. Code that touches
   render, audio or I/O is wired up and verified against the oracle at the
   same game moment.
5. Update `re/FUNCTION_MAP.csv`: `todo -> wip -> translated -> verified`.
6. Commit as one work unit: code, tests and docs of that change.
7. The port must stay buildable and runnable at every commit.

## Ambiguity policy

If a behaviour is unclear, leave a documented stub and move on:

```cpp
// TODO(fn_40A5E0): unknown branch when item count is 0
```

If it blocks a milestone, stop and ask the project owner instead of
guessing. Guessing a format or a branch is how a session gets lost.

## Subsystem order

1. Platform and engine bring-up (file, clock, render, input).
2. Front-end (boot, menus, options).
3. Level pipeline (containers, geometry, textures, camera).
4. Player and interactions (movement, combat, grab, dark gifts).
5. Enemies and AI.
6. Special modes and scripted sequences.
7. Meta (saves, videos, audio events, unlockables).

## Definition of done

A function is `translated` when the port reproduces its whole contract, a unit
test pins it and a local dump exists (the rule in `tools/gen/progress_data.py`).
It is `verified` when, on top of that:

- an oracle comparison proves the behaviour in the running game;
- its inputs and outputs match the original at the tested boundary;
- the function map and the affected docs are updated;
- the build and the full test suite pass.
