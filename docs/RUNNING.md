# Running

## Host build

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
./build/linux-debug/bo2nx
ctest --preset linux-debug --output-on-failure
```

The host binary inspects `.big` containers and renders a frame without a
window. The first backend is headless; SDL2 + OpenGL is pending because
the development container has neither the libraries nor a display.

Inspect a container and render a real texture on the free-camera preview:

```sh
BIG=external/installed/media/gogames/bo2/resources/levels/sanctuary/sanctuary.big
./build/linux-debug/bo2nx --list-big "$BIG"
./build/linux-debug/bo2nx --frame --big "$BIG" --entry SewBrick3 \
  --out /tmp/bo2-frame.ppm --eye 2.4,1.6,2.4 --target 0,0,0
```

The frame is a PPM; no display is needed. The camera is `--eye` and
`--target`; `--width`, `--height` and `--fov` control the image.

| Flag | Purpose |
|---|---|
| `--list-big <file>` | List entries (type, size, name) |
| `--frame --big <file>` | Render a textured preview frame |
| `--entry <substring>` | Texture entry to use (default: first texture) |
| `--out <file.ppm>` | Frame output |
| `--eye x,y,z`, `--target x,y,z` | Free camera |
| `--width`, `--height`, `--fov` | Frame size and field of view |

Planned with the level pipeline:

| Flag | Purpose |
|---|---|
| `--data-dir <dir>` | Game installation to read |
| `--headless` | No window; fixed ticks |
| `--frames N` | Run exactly N logic ticks |
| `--dump-state <file>` | Canonical state serialization for regression tests |
| `--screenshot <file>` | Offscreen render of one frame |

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
  `STATE.md`.

## Asset-gated tests

```sh
BO2NX_ASSETS_DIR=/path/to/game ctest --preset linux-debug
```

Tests that need assets skip cleanly when the variable is unset.
