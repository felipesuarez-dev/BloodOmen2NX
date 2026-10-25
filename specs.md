# Blood Omen 2 NX — Technical specification (v0.1)

Status: M0. This document describes the intended system and the verified
facts about the target. Anything not marked **verified** is an open
question and must not be treated as a design decision.

## 1. Purpose

Produce a native Nintendo Switch port of *Blood Omen 2* (PC, 2002) by
functional decompilation of `bo2.exe`, with a host build used for
development and oracle comparison. The port reads the user's own data
files; no game content is distributed.

## 2. Scope

In scope:

- PC 1.02 Spanish GoldMaster, 32-bit Windows build.
- All gameplay, levels, menus, saves, audio and videos.
- Host backend (Linux, SDL2 + OpenGL) and Switch backend (libnx + deko3d).

Out of scope:

- Byte-identical rebuilding of the original binary.
- Online, multiplayer or platform features that never shipped.
- Distributing game assets, original code or decompiled source dumps.
- PS2, GameCube and Xbox builds.

## 3. Target and provenance

| Item | Value |
|---|---|
| Game | Blood Omen 2 |
| Platform | PC (Windows, 32-bit) |
| Version | 1.02 Spanish GoldMaster |
| Executable | `bo2.exe`, 5,378,124 bytes |
| Build marker | `P:\gogames\bo2\build\bo2___Win32_Spanish_GoldMaster\bo2.pdb` |
| Graphics | Direct3D 8 |
| Input | DirectInput |
| Video | Bink (`BIKi` magic) |
| Audio | Ogg Vorbis (PC), per community documentation |
| Data | `data/game.erg`, `data/*.dat` |

Reference media hash and chain of custody: `docs/PROVENANCE.md`.

## 4. Verified technical findings

- `setup.exe` and `install2.dat` are both PE files. `install2.dat` is the
  installer payload; it contains the Eidos licence text and the registry
  key `Software\Crystal Dynamics\Blood Omen 2`.
- The ISO root holds nine Bink videos (`awakens.dat`, `bo2intro.dat`,
  `credits.dat`, `crystal.dat`, `eidos.dat`, `install2.dat`, `outro.dat`,
  `spooky.dat`, `sr2_trailer.dat`).
- The 198 files under `data/` start with a 32-bit little-endian value that
  appears to be a payload length, followed by high-entropy data with no
  recognizable asset markers. Compression or encryption is unconfirmed.
- The installed layout expects `data/game.erg`; the file is not present in
  the ISO, so the installer creates or extracts it.
- `bo2.exe` imports `d3d8.dll`, `d3d8d.dll`, `DINPUT.dll` and
  `binkw32.dll`.

## 5. Terminology

| Term | Meaning |
|---|---|
| Oracle | The original game running under Wine, used as behavioural reference |
| Host backend | Linux SDL2 + OpenGL platform layer, used for development |
| Switch backend | libnx + deko3d platform layer, the release target |
| Function map | `re/FUNCTION_MAP.csv`, one row per recovered function |
| Dump | Ghidra decompilation of one function, stored in `re/decomp/` (local only) |

## 6. Architecture

```
src/game      translated game logic, pure, no OS includes
src/engine    recovered engine services behind interfaces
src/platform  linux/ (SDL2 + OpenGL) and switch/ (libnx + deko3d)
src/common    fixed-width types, little-endian readers, logging
```

Dependency direction is one way: `game -> engine -> platform`. The game
layer receives input as a plain state struct and draws through engine
interfaces. This is what allows the game to be tested without a window,
without assets and without the console.

## 7. Data formats

Status per format is tracked in `docs/FORMATS/`. A format is only
documented after its parser is validated against real data and, where
possible, after a re-pack is accepted by the original game.

| Format | Status |
|---|---|
| `game.erg` | Unknown |
| `.dat` container | Header observed, payload unknown |
| Models | Unknown |
| Textures | Unknown |
| Audio | Ogg Vorbis expected, container unknown |
| Video | Bink, inventory known |
| Saves | Unknown |

## 8. Asset pipeline

1. Materialize an installed copy from the user's media.
2. Parse containers with a Python reference implementation (`tools/bo2fmt`).
3. Validate with synthetic fixtures first, then with real assets.
4. Re-pack one asset and confirm the original game accepts it.
5. Port only the validated readers to C++ in `src/engine`.

## 9. Renderer

Host backend: OpenGL 3.3 through a small loader. Switch backend: deko3d.
Both implement the same interface. The original D3D8 fixed-function path
must be mapped to explicit pipelines; the game data is expected to be
mostly pre-lit, which keeps the shader set small. This is an assumption
until the first level renders.

## 10. Audio

PC data is expected to be Ogg Vorbis. The engine exposes a mixer and a
small set of buses; the platform layer provides the output device. Bink
movies carry their own audio and are handled by the video path.

## 11. Input

The game targets a controller. The Switch mapping is defined once the
input layer exists; the original console releases are the reference for
button semantics. The host build adds keyboard bindings for development.

## 12. Gameplay translation

Subsystem order: platform and engine bring-up, front-end, level pipeline,
player and camera, combat and interactions, enemies and AI, dark gifts and
inventory, meta (saves, videos, audio events).

Every translated function cites `FUN_XXXXXXXX` and has a local dump. The
translation preserves structure, order of operations and constants.

## 13. Determinism

- Fixed logic tick, value to be confirmed from the original.
- 32-bit `float` wherever the original used `float`.
- `-ffp-contract=off`.
- Explicit little-endian readers for binary formats.
- Packed layouts verified with `static_assert(offsetof(...))`.
- Headless mode with canonical state serialization and hashing for
  regression tests.

## 14. Testing and oracle

Test pyramid: unit on synthetic fixtures, Python/C++ parity, asset-gated
integration, render golden, headless regression, oracle comparison,
sanitizers. Asset-gated tests skip cleanly when `BO2NX_ASSETS_DIR` is
unset.

The oracle is optional and time-boxed. It runs the original under Wine
with Xvfb and scripted input, and compares screenshots, state and saves.
No demo is shown before the frame recognizably matches the oracle.

## 15. Performance and memory

Switch homebrew has a limited memory budget and no virtual memory. Level
streaming must be budgeted from the start. The target is 60 fps docked
and handheld, at 720p handheld and 1080p docked, with the original's
visual behaviour preserved. No numbers are promised until the first
level renders.

## 16. Milestones

See `ROADMAP.md`. M1 is the go/no-go gate: if the containers cannot be
decoded and re-packed, the project is replanned.

## 17. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| `.dat` or `game.erg` cannot be decoded | Blocks all content | M1 gate; time-boxed reverse of the installer |
| No symbols and no public dump | Slow naming and triage | Function map + string/RTTI evidence |
| Renderer tightly coupled to D3D8 | Long graphics work | Host backend first; pre-lit asset assumption validated early |
| Scope (full 3D game) | Years of work | Vertical slice first; levels one by one |
| Public repository and derived code | Legal exposure | No assets, no dumps, no verbatim game text in the repo |

## 18. ADRs

ADRs are recorded in `docs/FORMATS/` and `docs/ARCHITECTURE.md` as
decisions are made. No ADR is closed by intuition.

## 19. Repository

The repository is public and contains only original code, tools and
technical documentation. `external/` and `re/decomp/` are gitignored.

## 20. Build and CI

Host: CMake presets `linux-debug`, `linux-release`, `linux-asan`.
Switch preset lands with M2. CI runs the asset/policy check, the host
build and the tests.

## 21. Legal

The project is an independent interoperability and preservation effort.
It distributes no game content and claims no affiliation with the rights
holders. The code licence is decided at M6.
