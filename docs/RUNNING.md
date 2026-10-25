# Running

## Host build

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
./build/linux-debug/bo2nx
ctest --preset linux-debug --output-on-failure
```

The current binary only reports the project version. The game is not
runnable until the M1/M2 work lands.

Future flags, once the host backend exists:

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
