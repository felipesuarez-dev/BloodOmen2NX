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

The development container provides SDL2, OpenGL, Xvfb and Wine.
`src/platform/headless/` is the deterministic software rasterizer used by
the tests and the PPM output. `src/platform/sdl/` is the interactive
SDL2 + OpenGL backend (`bo2nx --sdl`); it implements the same engine
interfaces, so the engine and game layers do not change.

The platform layer also provides the host services shared by every
backend: `FileSystem`, `log_message`, the monotonic clock and the memory
snapshot in `include/bo2/platform/platform.hpp`, implemented for Linux in
`src/platform/linux/`. The Switch backend implements the same interfaces.
`--stats` uses them to report the load breakdown, render time and the
resident/peak memory snapshot.

Input is platform-neutral: `include/bo2/platform/input.hpp` defines the
actions, keys and buttons, and `InputMap` binds them into an `InputState`.
`default_keyboard_map`, `default_switch_map` and `default_host_map` provide
the reference mappings; the SDL backend translates keyboard and
game-controller state into the same struct. The Switch backend will feed
libnx buttons through the same map.

The SDL backend renders the level through an explicit pipeline
(`src/platform/sdl/render_pipeline.cpp`): GLSL 1.20 vertex and fragment
shaders, an interleaved vertex buffer and an index buffer. Triangles are
submitted in ordered runs, so the original triangle order is preserved
for blending; each run sets its texture, light map, material tint and
shader uniforms. This replaces the D3D8 fixed-function path and is the
host reference for the Switch deko3d backend.

The engine also owns the M3 foundations: `FixedStep` for the deterministic
logic tick and `EntityWorld`/`CellStreamer` for entity storage, sphere
frustum culling and distance-based cell activation, and `CellNavigation`
for the portal graph, and `CollisionWorld` for sphere overlap and ray
casts against the level triangle soup. The game layer will populate them;
the renderer currently draws the static level as one mesh.

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

Current concrete modules:

| Module | Responsibility | State |
|---|---|---|
| `big_archive` | `goefile` reader | Validated on the installed tree |
| `cell_group` | `cellgrup` static level geometry | World-space triangles and textures |
| `dds` | DDS/BC decoder | DXT1, DXT3, DXT5, A8R8G8B8; top mip |
| `math` | Vec3, Vec4, Mat4 | Used by the headless renderer |
| `render_pipeline` | GLSL/VBO level pipeline | SDL host renderer |
| `PlayerController` | Kain movement and follow camera | Tunedata constants, ground movement from the root motion of the animation graph, combat mode; air control and glide pending |
| `SaveGame` | canonical save/checkpoint bytes | Round-trip and slot tests |
| `EntityWorld` combat | health/damage and sphere queries | Attack foundation |
| `script_memory` | 32-bit arena of the script machine | Checked bounds; blocks zeroed and aligned |
| `script_program` | Links the script packages of a region | Every region of the game links |
| `script_machine` | Runs the compiled scripts | Every instruction the data uses, objects and states, four value types; no native function of the game yet ([`SCRIPTS.md`](SCRIPTS.md), "In the port") |

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

## Script machine

The game's scripts are compiled code for a stack machine
([`SCRIPTS.md`](SCRIPTS.md)). That code addresses memory directly: a local
is the locals of the frame plus an offset, a field is the data of an object
plus an offset, an element is an address plus an index times a size, and
every pointer a script holds (a string, an object handle, the block of a
value) takes 4 bytes, so the offsets in the compiled classes count 4-byte
pointers. The Switch runs 64-bit code only.

Decision (2026-11-14): the port keeps the 32-bit model of the original
inside the machine instead of rewriting the code for 8-byte pointers.

- `ScriptMemory` (`src/engine/script_memory.cpp`) is one byte arena per
  machine, addressed by 32-bit offsets. Address 0 is null and no block starts below 8. Blocks are
  aligned to 4. Reads and writes are little-endian and checked: an address
  outside a live block stops the machine with an error, where the original
  would read garbage or crash.
- Everything a script can address lives in the arena: the data block of
  every object, the stack and the locals of every frame, the cells that
  hold a string, the counted string records, the handles of the objects and
  the blocks of the value types. The global name table is an arena block
  of 4-byte cells, one per name, each holding the address of the record of
  the name, so `0x04` pushes the address of a cell as in the original.
- An object handle is a 12-byte block like the original's (word 0 unused,
  the count at +4, the object at +8); the object is an index into the
  machine's object table, plus one, so 0 stays null.
- The linked code keeps the instruction words of the file. The words the
  original rewrites into pointers at link time hold indices into the
  machine's tables instead (value type, method record, body record, class
  of the state tables), and the words of references 0 and 1 hold the index
  of the name, as in the original. `ScriptProgram`
  (`src/engine/script_program.cpp`) does this linking.
- A native function is bound by class name and body index, as the
  factories of the original do ([`SCRIPTS.md`](SCRIPTS.md), "Native
  functions"), and takes the machine, the object, the argument count, the
  address of the first argument and the address of the result.

Rejected: rewriting the compiled classes for 8-byte pointers (every field
offset, local offset and copy size of the data would change, so the port
would no longer run the game's code as it is), and a 32-bit build (the
Switch toolchain targets AArch64 only).

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

The headless frame takes `--big <file>`; the level pipeline will add
`--data-dir`. The Switch build reads from the SD card path documented in
`docs/GAME_DATA.md`.
