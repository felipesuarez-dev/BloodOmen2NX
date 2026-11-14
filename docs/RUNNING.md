# Running

## Host build

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
./build/linux-debug/bo2nx
ctest --preset linux-debug --output-on-failure
```

The host binary inspects `.big` containers, renders frames without a
window, runs a fixed-step headless loop and can open an SDL2 + OpenGL
window when the backend is built.

Inspect a container and render a real texture on the free-camera preview:

```sh
BIG=external/installed/media/gogames/bo2/resources/levels/sanctuary/sanctuary.big
./build/linux-debug/bo2nx --list-big "$BIG"
./build/linux-debug/bo2nx --erg external/installed/data/game.erg
./build/linux-debug/bo2nx --frame --big "$BIG" --entry SewBrick3 \
  --out /tmp/bo2-frame.ppm --eye 2.4,1.6,2.4 --target 0,0,0
```

The frame is a PPM; no display is needed. The camera is `--eye` and
`--target`; `--width`, `--height` and `--fov` control the image.

## Level frame

`--level` reads the `cellgrup` entry and renders the static level shell.
`--kain-model <kain.big> --kain-at x,y,z` appends Kain's model (2,846
triangles, 3 textures, 104 bones and 1,988 skin weights in the reference
`kain.big`) at the given position, which is how the spawn comparison frame
draws him. He stands in the bind pose unless `--kain-clip <name>` names one
of the container's `animreel` clips; `--kain-frame <n>` picks the frame
(fractions interpolate). The clip poses the skeleton and skins the mesh
(`docs/FORMATS/models.md`, Skeleton pose and skinning):

```sh
LEVELS=external/installed/media/gogames/bo2/resources/levels
./build/linux-debug/bo2nx --level --big "$LEVELS/slums/sl01.big" \
  --kain-model "$LEVELS/kain/kain.big" --kain-clip kain/run --kain-frame 6 \
  --kain-at -8.8,57.1,208 --eye -8.8,-62.9,248 --target -8.8,57.1,244 \
  --out /tmp/kain_run.ppm
```

`--hud <code.big>` draws the HUD scene (`ui/hud`) over the `--level` frame:
the frame, the health vial and the lore vial, with the health and lore Kain
enters the level with ([`HUD.md`](HUD.md)). It reads the Kain tuning from
`--kain` or `<level dir>/../kain/kain.big`.

`--replay` and `--play` take `--kain-model` too. The replay end frame
(`--out`) draws Kain at the replayed position and yaw; the player skins him
every frame and sends his slice of the vertex buffer again
(`LevelPipeline::update_vertices`), in place of the position marker. In both
the clip is the one his animation state plays: the idle stance, the run, the
walk back, the jump poses and the landings (`docs/GAMEPLAY.md`, Animation
state). In the player a change of clip blends over the time the graph gives
it; the replay draws one frame, so it shows the clip without a blend. With
`--kain-clip` that one reel loops instead, whatever the player does.
Kain is an actor, not level geometry: his triangles and `collmodc` boxes
stay out of the `Render` and `Boxes` collision sources, so a played or
replayed Kain never collides with his own mesh.
The default camera is an isometric view of the whole level; `--eye` and
`--target` select a free camera. The level axes are Z-up. The command
also prints the parsed cell, portal, terrain, BSP, instance, light and marker counts.

```sh
BIG=external/installed/media/gogames/bo2/resources/levels/sanctuary/sanctuary.big
./build/linux-debug/bo2nx --level --big "$BIG" --out /tmp/sanctuary.ppm
./build/linux-debug/bo2nx --level --big "$BIG" --lightmap --out /tmp/sanctuary_lit.ppm
./build/linux-debug/bo2nx --level --big "$BIG" --out /tmp/sanctuary_inside.ppm \
  --eye 0,-20,4 --target 0,20,4 --fov 70
```

The geometry is drawn with the DDS textures resolved from the cellgrup
symbols; materials without a texture entry use the diffuse material
layer. `--material` applies the material layers as a diagnostic tint on
textured surfaces; the default `off` keeps the preview frame. The tint is
not the final lighting equation (see `docs/FORMATS/models.md`). A triangle near-plane clipper keeps interior cameras stable; a
camera inside the closed shell sees the shell interior, while the
isometric frame shows the level.

## Diagnostics

`--stats` prints the level load breakdown (archive, cell group, textures,
light maps), the render time and the resident/peak memory snapshot. It
works with `--level` and `--sdl`; the SDL backend adds its first-frame
time.

`--collision-dump` prints every `collmodc` model with its instances, local
and world AABBs, the u32 values after the AABB blob and the raw payload.
`--collision-model <substring>` filters by model name and `--collision-out`
writes to a file. In `sl01` the W AABB is model 57 `default`, instance 41,
starting at x = 256; the oracle passes that face and stops at x = 326.15, so
the exact hull is still open (BO2NX-72).

Measured on this container for `sanctuary` at 640x480 with `--lightmap`:

| Phase | Time |
|---|---:|
| BIG archive load | 207-236 ms |
| Cell group parse | 6 ms |
| Textures | 5-6 ms |
| Light maps | 8 ms |
| Headless render | 30-31 ms |
| SDL first frame | 20-22 ms |
| RSS / peak | 12-15 MB |

Hardware (Switch) numbers are tracked in #49.

## Audio

The host audio path decodes Ogg Vorbis and mixes one looping music track
with one-shot events. Extract streams from the game's `pc.rkv` with the
Python tool, then play them:

```sh
python3 tools/bo2fmt/rkv.py extract \
  external/installed/media/gogames/bo2/pc.rkv --out /tmp/bo2-audio
./build/linux-debug/bo2nx --event-ogg /tmp/bo2-audio/STR_CHEAT \
  --music-ogg /tmp/bo2-audio/SD_bar_insidehum_03 --audio-seconds 5
```

The event plays once and the music loops; `--audio-seconds` bounds the
demo. The reference streams use the Vorbis floor-0 mode, which the host
reads through `libvorbisfile.so.3`; the bundled `stb_vorbis` fallback
covers floor-1 streams when the system library is unavailable. The mixer
is unit-tested with synthetic PCM.

## SDL2 + OpenGL window

`--sdl` opens the SDL2 + OpenGL backend. With `--big` it loads the level
through the GLSL 1.20/VBO pipeline; without `--big` it draws a triangle,
which is the backend self-test. The
backend is built when CMake finds SDL2 and OpenGL; set
`-DBO2NX_ENABLE_SDL=OFF` to build headless only.

```sh
BIG=external/installed/media/gogames/bo2/resources/levels/sanctuary/sanctuary.big
./build/linux-debug/bo2nx --sdl --big "$BIG"
xvfb-run -a ./build/linux-debug/bo2nx --sdl --big "$BIG"   --width 640 --height 480 --out /tmp/sdl-sanctuary.ppm
xvfb-run -a ./build/linux-debug/bo2nx --sdl --out /tmp/sdl-triangle.ppm
```

With `--out` the backend renders one frame, captures the framebuffer and
exits; without it the window stays open. The orbit viewer uses `W`/`S` or
the arrow keys to move the target, `A`/`D` to strafe, the right mouse
button to orbit, the wheel to zoom and `Escape` to quit.

`--play` opens the Kain third-person player instead. It needs the level
`--big` and the `kain` tunedata, which it reads from `--kain <kain.big>`
or, when that is omitted, from `<level dir>/../kain/kain.big`. It stops with
an error if neither exists.

```sh
LEVELS=external/installed/media/gogames/bo2/resources/levels
./build/linux-debug/bo2nx --play --big "$LEVELS/sanctuary/sanctuary.big"
./build/linux-debug/bo2nx --play --big "$LEVELS/sanctuary/sanctuary.big" \
  --kain "$LEVELS/kain/kain.big"
./build/linux-debug/bo2nx --play --big "$LEVELS/slums/sl01.big" \
  --kain-model "$LEVELS/kain/kain.big"
```

With `--kain-model` the player is Kain's skinned model instead of the
marker, animated by his state.

Controls are tank style: `W`/`S` walk forward and back, `A`/`D` turn in
place, `Shift` (or `R`) holds combat mode (the stick then moves and strafes
without turning), `Space` jumps (hold it for
the higher jump), `F` attacks, `F5`/`F9` save/load the checkpoint slot and
`Escape` quits. The right mouse button looks around as a debug aid. The
follow camera sits 190.9 behind and 96.1 above Kain and is pulled in by the
level collision mesh; two bars show health and mana. With `--kain-model` the root
motion of the animation moves Kain on the ground; without it he keeps the
steady run and walk-back speeds of the clips, and combat mode does nothing;
see
[`GAMEPLAY.md`](GAMEPLAY.md#kain-movement) for every value and what is still
pending. The body size and eye height are still provisional.

Controls are defined in `include/bo2/platform/input.hpp`; the mapping is
provisional until the original controls are recovered from `bo2.exe`.

| Action | Keyboard | Switch / controller |
|---|---|---|
| Move (`--sdl` orbit viewer) | `WASD` or arrows | Left stick or D-pad |
| Move (`--play`): forward/back, turn | `W`/`S`, `A`/`D` or arrows | Left stick or D-pad |
| Combat mode in `--play` (hold) | `Shift` or `R` | `L` or `R` |
| Jump | `Space` | `A` |
| Attack | `F` | `B` |
| Use | `E` | `X` |
| Menu | `Enter` | `Y` or `Minus` |
| Pause / quit | `Escape` | `Plus` |
| Look / orbit | Right mouse drag | Right stick, `ZL` / `ZR` |
| Zoom | Wheel | - |

| Flag | Purpose |
|---|---|
| `--list-big <file>` | List entries (type, size, name) |
| `--erg <file>` | Parse and print a `game.erg` configuration |
| `--frame --big <file>` | Render a textured preview frame |
| `--level --big <level.big>` | Render the static `cellgrup` level shell |
| `--collision-dump --big <level.big>` | Dump `collmodc` models, instances, world AABBs and raw payloads |
| `--collision-model <substring>` | Filter the collision dump by model name |
| `--collision-out <file>` | Write the collision dump to a file |
| `--sdl [--big <file>]` | SDL2 + OpenGL window; no `--big` draws a triangle |
| `--play --big <level.big>` | Kain third-person player in the SDL window |
| `--kain <kain.big>` | `kain` tunedata for `--play` (default `<level dir>/../kain/kain.big`) |
| `--kain-model <kain.big>` | Append Kain's model to the level: at `--kain-at` for `--level` and `--sdl`, at the player for `--replay` and `--play` |
| `--kain-at x,y,z` | Kain's position for `--level` and `--sdl` (default 0,0,0) |
| `--kain-clip <name>` | Pose Kain with one `animreel` clip of `--kain-model`, for example `kain/run` (default: the bind pose in `--level` and `--sdl`, the clip of the animation state in `--replay` and `--play`) |
| `--kain-frame <n>` | Clip frame for `--level` and `--sdl`; fractions interpolate (default 0) |
| `--entry <substring>` | Entry to use: the texture for `--frame` (default: first texture); the cell group for `--level`, with the region of the same name (default: the first cell group and every region). `code.big` has four UI scenes, for example `--entry ui/hud` |
| `--out <file.ppm>` | Frame output |
| `--eye x,y,z`, `--target x,y,z` | Free camera |
| `--width`, `--height`, `--fov` | Frame size and field of view |
| `--material <mode>` | Diagnostic material tint: `off`, `diffuse`, `layers`, `layers2x` |
| `--lightmap` | Light the level with its `.slm`: light maps on the second UV set and, when present, the per-vertex colours (`texture × vertex colour × light map`) |
| `--stats` | Print load breakdown, render time, RSS and peak memory |
| `--headless` | Run the fixed-step loop without a window |
| `--frames <n>` | Fixed-step iterations for `--headless` |
| `--tick-rate <hz>` | Logic ticks per second (default 60) |

Planned with the level pipeline:

| Flag | Purpose |
|---|---|
| `--data-dir <dir>` | Game installation to read |
| `--dump-state <file>` | Canonical state serialization for regression tests |

## Switch build (M2)

The `.nro` is built with devkitPro once the platform backend lands. The
SD card layout will be documented here before the first public build.

## Oracle

The original game is a 32-bit Windows binary. It is run only to capture
screenshots, state and audio for comparison, never as a dependency of the
build. Two harnesses exist:

| Harness | Where | Guide |
|---|---|---|
| Wine + Xvfb (pristine `bo2.exe`, no GPU) | This host | [`ORACLE_WINE.md`](ORACLE_WINE.md) |
| Native Windows (camera hook, `state.csv`) | Windows host over an SSH reverse tunnel | [`ORACLE_WINDOWS_NATIVE.md`](ORACLE_WINDOWS_NATIVE.md) |

Wine capture from the main menu to the `sl01` checkpoint, with checks:

```sh
scripts/oracle/wine_setup.sh external/captures/capture_20261028_0150
python3 tools/oracle/wine_capture.py tools/oracle/routes/sl01_basic.txt
python3 tools/oracle/validate_capture.py external/captures/wine_YYYYMMDD_HHMM
python3 tools/oracle/align.py external/captures/wine_YYYYMMDD_HHMM
```

Rules:

- Do not block M0 or M1 on the oracle.
- Captures live under `external/captures/` and are never committed.
- Record divergences in `docs/ORACLE_COMPARE.md`.

## Asset-gated tests

```sh
BO2NX_ASSETS_DIR=/path/to/game ctest --preset linux-debug
```

Tests that need assets skip cleanly when the variable is unset.
