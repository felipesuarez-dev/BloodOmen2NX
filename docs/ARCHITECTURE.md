# Architecture

Functional decompilation and native port of *Blood Omen 2* (PC, 2002).
The game logic is a faithful C++17 translation of the original; the engine
services are recovered behind interfaces; the platform layer is the only
code that touches an operating system.

## Host first

The first target is a Linux host build (SDL2 + OpenGL). It exists to:

- build and test the game logic without assets or a console;
- compare frames, state and audio against the original running as an
  oracle;
- validate formats and renderer assumptions cheaply.

The Switch backend (libnx + deko3d) replaces the platform layer once the
host build renders a level. The game and engine layers do not change when
the backend changes.

## Layers

Dependency direction is one way, downward:

```
src/game      translated game logic
src/engine    recovered engine services
src/platform  linux/ and switch/ backends
src/common    fixed-width types, LE readers, logging
```

Rules:

- `src/game` never includes SDL, OpenGL, libnx, deko3d or any OS header.
- Input reaches the game as a plain state struct.
- Drawing goes through engine interfaces, never through a raw graphics API.
- Assets are opened through the engine file layer, never with `fopen`.
- Pure rules (collision, camera math, state machines) are always built and
  unit-tested without assets.

## Engine interfaces

The original engine is statically linked into `bo2.exe`; there is no DLL
to mirror. The interfaces are recovered from the executable:

| Interface | Responsibility |
|---|---|
| `FileSystem` | Mount the game directory, open `game.erg`, read containers |
| `TextureCodec` | Decode texture data into a GPU-independent format |
| `ModelCodec` | Decode meshes, skeletons and animations |
| `Scene` | Submit geometry and camera state to the renderer |
| `Audio` | Voices, buses and event playback |
| `Clock` | Fixed logic tick and frame timing |

Each interface is implemented by the platform backend. The engine owns
the data structures; the platform owns the device objects.

## Determinism

Behavioural parity needs a deterministic simulation:

- fixed logic tick, value confirmed from the original before M3;
- 32-bit `float` wherever the original used `float`;
- `-ffp-contract=off`;
- explicit little-endian readers for binary formats;
- packed layouts verified with `static_assert(offsetof(...))`;
- a headless mode with canonical state serialization and hashing for
  regression tests;
- no wall-clock time inside the simulation.

## Memory

Switch homebrew has no virtual memory and a limited budget. The design
must stream levels instead of loading the whole game, and every subsystem
must expose its peak allocation. The numbers are measured on the host
first and then on hardware; no target is promised before M2.

## What is not ported

The MSVC CRT, Direct3D 8, DirectInput and Bink. They are replaced by the
platform layer, SDL2 and a video decoder. The original executables are
never shipped.

## Asset access

The port reads the original installation unchanged:

```
<data-dir>/data/game.erg
<data-dir>/data/*.dat
<data-dir>/*.dat          (Bink videos)
```

The host build takes `--data-dir`; the Switch build reads from the SD
card path documented in `docs/GAME_DATA.md`.
