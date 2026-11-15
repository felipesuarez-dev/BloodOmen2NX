# Porting guide: from a specification to code

Mandatory workflow for porting a part of the game into `src/`. It has two
roles, kept apart on purpose ([`PROVENANCE.md`](PROVENANCE.md), "Code
provenance"):

**Analysis.** Study the original and write down what it does.

1. Pick the next discovery from the queue. Read the functions in Ghidra and
   cross-check every string, RTTI entry and caller that reveals their
   purpose; check the behaviour in the running game (the oracle) when it
   can be observed.
2. Write the result as a specification: the rule, the data layout the
   game's files use, the numbers a test can check, and the address of each
   function where the rule was observed. It goes into the matching page of
   `docs/` and into an issue for the implementation. Decompiled output and
   disassembly stay local (`re/decomp/`, gitignored).

**Implementation.** Write the code from the specification only.

3. Write the C++ from the issue and the `docs/` pages it names, without
   opening the binary or the dumps. Reproduce the behaviour and the data
   layout of the game's files that the specification documents; the
   structure of the code, its names and its helpers are the port's own.
   No redesign of the behaviour, no cleanup refactors outside the issue.
4. Pure logic gets a unit test with a synthetic fixture. Code that touches
   render, audio or I/O is wired up and verified against the oracle at the
   same game moment.
5. Update `re/FUNCTION_MAP.csv`: `todo -> wip -> translated -> verified`.
   The address in a comment says where the rule was observed.
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
