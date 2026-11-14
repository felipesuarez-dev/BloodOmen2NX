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

Status: Kain movement is in (`kain_tuning`, `player`, `camera`,
`kain_animation`). On the ground the root motion of the animation moves him
(`KainAnimation::ground_motion`, `docs/FORMATS/animatio.md`): the blends of
the graph give the start of the run, the pause before the walk back and the
slide after the run, and the clips give the run and walk-back speeds. The
combat mode (Shift) plays the combat movement states of the graph. See
`docs/GAMEPLAY.md`.
