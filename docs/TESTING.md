# Testing strategy

## Pyramid

| Level | What | When | Assets needed |
|---|---|---|---|
| Unit (Python) | LE readers, math, parsers on synthetic fixtures (SFXI, BIG, RKV), installer | Always | No |
| Unit (C++) | goefile/DDS/cellgrup/ERD readers, cell grid/BSP, cell instances, lights/markers, material layers/tints, `.slm` light maps and model-group matching, texture binding, math and rasterizer | Always | No |
| Platform services | FS, log sink, monotonic clock, memory snapshot and input maps on Linux | Always | No |
| Parity Python/C++ | Same input to the reference parser and the C++ parser -> identical output | Always | No (fixtures) |
| Integration (asset-gated) | Parsers over the user's real assets -> counts and SHA-256 manifests | `BO2NX_ASSETS_DIR` set | Yes, skips otherwise |
| Render golden | Offscreen render of a scene or texture -> PNG hash | `BO2NX_ASSETS_DIR` set | Yes, goldens local |
| Frame comparator | Side-by-side, diff, tile/region metrics on two PPMs | Manual and Python unit test | No (synthetic PPM) |
| Headless regression | `bo2nx --headless --frames N --dump-state` -> canonical hash | Always (synthetic) / gated (real levels) | Mixed |
| Oracle | Port vs original under Wine (screenshots, state, saves, audio) | Milestones | See `docs/RUNNING.md` |
| SDL smoke | SDL2/OpenGL triangle under Xvfb -> PPM | When SDL2 and Xvfb are present | No |
| Sanitizers | ASan + UBSan over unit and integration | `linux-asan` preset | No |

## Rules

- Every feature or fix lands with its tests in the same commit.
- Parser tests are **spec tests**: they prove the implementation matches
  the documented layout, not that the layout is correct for this build.
  Real-data validation is a separate gate.
- Asset-dependent tests skip cleanly when `BO2NX_ASSETS_DIR` is unset;
  they never fail for lack of assets.
- Committed goldens contain only derived facts (hashes, counts,
  dimensions). Raw asset content and copyrighted strings are never
  committed.
- Regenerate goldens only after an intentional behaviour change, and
  explain why in the commit message.
- No test may depend on a wall clock, a network or the user's home layout.

## Commands

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure

BO2NX_ASSETS_DIR=/path/to/game ctest --preset linux-debug   # asset-gated on
./scripts/ci.sh                                             # everything + policy check

# Level frame (manual, needs the installed tree)
./build/linux-debug/bo2nx --level \
  --big external/installed/media/gogames/bo2/resources/levels/sanctuary/sanctuary.big \
  --out /tmp/sanctuary.ppm

# Texture preview (manual, needs the installed tree)
./build/linux-debug/bo2nx --frame \
  --big external/installed/media/gogames/bo2/resources/levels/sanctuary/sanctuary.big \
  --entry SewBrick3 --out /tmp/bo2-frame.ppm
```

## Visual gate

The headless renderer and the SDL2 + OpenGL backend draw the `cellgrup`
shell with its DDS textures and a free camera. The side-by-side oracle
gate still applies before a demo; do not show a level demo until the
frame recognizably matches the original.
