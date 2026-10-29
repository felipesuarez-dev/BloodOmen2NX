# src/game - translated game logic

Pure game logic, translated from `bo2.exe` one function at a time. This
layer never includes an OS library: input arrives as a plain state struct,
drawing goes through the engine interfaces, and assets are opened through
the engine file layer.

Rules:

- Every translated function cites its original address (`FUN_00XXXXXX`).
- Translate, do not redesign: same order of operations, same constants.
- Every behaviour claim needs a local decompile under `re/decomp/`.
- Keep the code testable without assets; asset-dependent paths are gated.

Status: Kain movement is in (`kain_tuning`, `player`, `camera`). Run and
walk-back speeds come from the root motion of the `kain/run` and
`kain/walk_backwards` animreel clips (`docs/FORMATS/animreel.md`); the start
phase and Shift+W speeds are still measured in the Wine oracle. See
`docs/GAMEPLAY.md`.
