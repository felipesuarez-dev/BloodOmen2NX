# Running

## Host build

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
./build/linux-debug/bo2nx
ctest --preset linux-debug --output-on-failure
```

The host binary inspects `.big` containers, renders frames without a
window and can open an SDL2 + OpenGL window when the backend is built.

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

Measured on this container for `sanctuary` at 640x480 with `--lightmap`:

| Phase | Time |
|---|---:|
| BIG archive load | 207-236 ms |
| Cell group parse | 6 ms |
| Textures | 5-6 ms |
| Light maps | 8 ms |
| Headless render | 30-31 ms |
| SDL first frame | 34-35 ms |
| RSS / peak | 12-15 MB |

Hardware (Switch) numbers are tracked in #49.

## SDL2 + OpenGL window

`--sdl` opens the SDL2 + OpenGL backend. With `--big` it loads the level;
without `--big` it draws a triangle, which is the backend self-test. The
backend is built when CMake finds SDL2 and OpenGL; set
`-DBO2NX_ENABLE_SDL=OFF` to build headless only.

```sh
BIG=external/installed/media/gogames/bo2/resources/levels/sanctuary/sanctuary.big
./build/linux-debug/bo2nx --sdl --big "$BIG"
xvfb-run -a ./build/linux-debug/bo2nx --sdl --big "$BIG"   --width 640 --height 480 --out /tmp/sdl-sanctuary.ppm
xvfb-run -a ./build/linux-debug/bo2nx --sdl --out /tmp/sdl-triangle.ppm
```

With `--out` the backend renders one frame, captures the framebuffer and
exits; without it the window stays open. Controls: `W`/`S` or the arrow
keys move the target, `A`/`D` strafe, `Q`/`E` change height, the right
mouse button orbits, the wheel zooms and `Escape` quits.

| Flag | Purpose |
|---|---|
| `--list-big <file>` | List entries (type, size, name) |
| `--erg <file>` | Parse and print a `game.erg` configuration |
| `--frame --big <file>` | Render a textured preview frame |
| `--level --big <level.big>` | Render the static `cellgrup` level shell |
| `--sdl [--big <file>]` | SDL2 + OpenGL window; no `--big` draws a triangle |
| `--entry <substring>` | Texture entry to use (default: first texture) |
| `--out <file.ppm>` | Frame output |
| `--eye x,y,z`, `--target x,y,z` | Free camera |
| `--width`, `--height`, `--fov` | Frame size and field of view |
| `--material <mode>` | Diagnostic material tint: `off`, `diffuse`, `layers`, `layers2x` |
| `--lightmap` | Sample the `.slm` light maps with the second UV set |
| `--stats` | Print load breakdown, render time, RSS and peak memory |

Planned with the level pipeline:

| Flag | Purpose |
|---|---|
| `--data-dir <dir>` | Game installation to read |
| `--headless` | No window; fixed ticks |
| `--frames N` | Run exactly N logic ticks |
| `--dump-state <file>` | Canonical state serialization for regression tests |

## Switch build (M2)

The `.nro` is built with devkitPro once the platform backend lands. The
SD card layout will be documented here before the first public build.

## Oracle

The original game is a 32-bit Windows binary. Running it under Wine is
optional and time-boxed: it is used to capture screenshots, state and
audio for comparison, never as a dependency of the build.

Rules:

- Do not spend more than one day making the oracle boot.
- Do not block M0 or M1 on it.
- Record the exact steps and the Wine prefix in this file if it works.
- If it does not boot, continue with static analysis and note it in
  `STATE.md` and `docs/ORACLE_COMPARE.md`.

## Asset-gated tests

```sh
BO2NX_ASSETS_DIR=/path/to/game ctest --preset linux-debug
```

Tests that need assets skip cleanly when the variable is unset.
