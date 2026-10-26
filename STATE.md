# State

Current milestone: **M2 — host skeleton and first frame**. M1 (containers
and assets) is complete.

## What is done

- Repository scaffold, CMake presets, host smoke binary and test.
- Asset/policy check and CI workflow.
- Target identified: PC 1.02 Spanish GoldMaster, 32-bit PE, MSVC 6.0
  (linker 6.0), D3D8, DirectInput, Bink.
- Reference media and binaries hashed (`docs/PROVENANCE.md`).
- SFXI installer container decoded and implemented in
  `tools/bo2fmt/sfxi.py`, with a synthetic test.
- `game.erg` extracted and identified as the text configuration file.
- `list.txt` extracted: the asset path list (kept local).
- Ghidra project created and auto-analysis completed; 35,767 functions
  exported, 18,097 game candidates seeded in `re/FUNCTION_MAP.csv`.
- Prior art integrated (`docs/PRIOR_ART.md`): `.big` (`goefile`) and
  `pc.rkv` audio formats documented, DDS textures and Ogg audio confirmed.
- Installer chunk pipeline decoded: `tools/bo2fmt/installer.py` and
  `tools/bo2fmt/bo2cipher.c`; 198/198 chunks; a 227-file tree (1.9 GB) in
  `external/installed`, with its manifest hash in `docs/PROVENANCE.md`.
- `.big` parser validated against the installed tree: 179 files, 246,332
  entries, 0 parse errors; 22,483 DDS textures and 5,168 models.
- `pc.rkv` validated: 2,666 Ogg Vorbis streams; the first is mono
  44.1 kHz. The last stream is 49,152 of 50,002 bytes in the reference
  archive; the parser reports it as truncated.
- One DDS (32x16 DXT5, six mip levels) and one model payload extracted;
  the model starts with an `rmd` chunk.
- Installer S-box and password offsets documented in
  `docs/FORMATS/dat.md`; neither is stored in the repository.
- Host skeleton: `src/engine/big_archive.*`, `src/engine/dds.*`,
  `src/engine/math.hpp` and `src/platform/headless/`. `bo2nx --list-big`
  lists real containers and `--frame` renders a real DDS texture on a
  textured cube with a free camera to a PPM, without a window.
- C++ unit tests cover the `goefile` reader, `cellgrup`, DDS, math and
  rasterizer.
- The static level shell is loaded: `cellgrup` parsing, world-space
  vertices, per-material colors and a free camera. `bo2nx --level` renders
  `sanctuary` (75/76 models, 5,107 triangles) to a PPM.
- The level binds its DDS textures: the cellgrup symbol list resolves the
  model materials, the first UV set maps the texture coordinates, and
  33/34 `sanctuary` textures are bound; the rest fall back to the material
  color.
- The level renderer clips triangles at the near plane, so a free camera
  inside the shell no longer tears the projection.
- The cell grid and visibility BSP are decoded: `cells` version 2,
  `attrs` tables and the recursive `cellbsp`; `bo2nx --level` reports the
  counts. `sanctuary` has 3 cells, 6 portals and 33 BSP nodes.
- Cell instances are parsed and applied: the `cellmdls` models are local
  templates and `cellinst` (v3/v4) places them in world space. In
  `sanctuary` 74 of 112 instances are placed (38 sentinel or missing);
  `cy01` places 429 of 575 and `lc01` 400 of 512. Light maps are assigned
  per placed instance, with a per-model fallback; `cy01` covers 15,217
  triangles and `lc01` 7,116.
- Cell lights and markers are parsed: `cellight` version 3 in 114 groups
  and `cellmark` version 4 in 131 groups. `sanctuary` has 13 lights and
  10 markers; the renderer does not consume them yet.
- The host platform services are implemented and tested without a window
  or assets: `FileSystem`, `log_message`, the monotonic clock and the
  resident/peak memory snapshot (`src/platform/linux/platform.cpp`).
- The input layer is platform-neutral: keyboard and Switch maps, analog
  deadzone, SDL game-controller support and a player placeholder marker in
  the SDL viewer. The real actor and its movement come with M3.
- Host diagnostics are measured: `--stats` reports the archive, cell
  group, texture and light-map load times, the headless/SDL render time
  and the resident/peak memory. `sanctuary` 640x480 loads in about 250 ms,
  renders in about 31 ms headless and 20-22 ms in the SDL shader pipeline,
  with 12-16 MB RSS.
  Switch hardware numbers are tracked in #49.
- The `game.erg` configuration parser is verified: quoted keys/values,
  literal backslashes, CRLF, blank lines and an exact round-trip fixture.
  `bo2nx --erg` reads the reference file (52 entries).
- The `.slm` light maps are parsed: the scanner finds 71 unique entries
  across the cell/object groups of `sanctuary.slm` (cryptexithall,
  crypt, cryptentrancehall, coffins, roofbits, meshes, objects, pilars,
  boxes), with 16-color palettes and 4-bit indices. The model reader
  keeps the second UV set and `--lightmap` samples it; 16 of 34 sanctuary
  textures resolve. The loader also matches model vertex counts to the
  group counts: 62 model light maps cover 2,769 triangles. Models are
  consumed in order, so duplicate counts (coffins, roofbits) get
  different groups. Shared-model instance mapping and the per-vertex
  data are open.
- Material layer records are parsed: `mlyrcolr` (three RGBA colors) and
  `mlyrctrl` (three `u32`/`f32`/`u16`/`u16` controls) per material.
  Untextured surfaces use the diffuse layer. The D3D8 trace shows texture
  stage 0 as `MODULATE`/`MODULATE2X` with `TEXTURE × DIFFUSE`; the host
  exposes diagnostic `--material` tints. A direct tint darkens sanctuary
  to mean 0.086 (from 0.107) and clips its maximum, so the static light
  map is still required. `sanctuary.slm` (388,673 bytes, 13 entries) is
  the next input.
- The SDL2 + OpenGL host backend is built: `bo2nx --sdl` opens a window,
  draws the level with textures, a free camera and right-drag orbit, and
  captures a framebuffer with `--out`. The level goes through an explicit
  GLSL 1.20/VBO pipeline with ordered runs, replacing the D3D8
  fixed-function path. It uses the same engine data as the headless path;
  a 640x480 sanctuary frame is within 0.2% RMSE of the software renderer.

- The fixed-step loop (`FixedStep`) and `--headless --frames N` are in
  place; the accumulator is clamped to 0.25 s and the tick rate defaults
  to 60 Hz. `EntityWorld` stores entities and filters them through a
  sphere frustum; `CellStreamer` activates cells by camera distance with
  hysteresis; `CellNavigation` builds a cell graph from portals and finds
  a shortest path. The level viewer populates the world from the 13
  sanctuary region entities and reports frustum-visible entities and active
  cells.
  A triangle-soup `CollisionWorld` provides sphere overlap and ray casts.
  A provisional `PlayerController` moves a body with collision, gravity,
  jumping and a collision-clamped third-person follow camera, and
  `bo2nx --play` drives it in the SDL level viewer. Its speeds and input mapping are not yet a faithful
  translation and require decompilation. The `region` entity table is
  decoded (15,632 entities, 533 model names, 0 errors); entity models are
  resolved and appended as geometry (13 in sanctuary, 119 in cy01, 72 in
  lc01), with named light-map selection and a vertex-count fallback.
  Save states serialize the level name, player transform and tick with a
  versioned binary format; checkpoint slots and corruption checks are
  unit-tested. The HUD and full checkpoint flow remain pending.
  The compressed `collmodc` runtime and game-specific entities remain
  pending.

## What is not done

- Shared-model instance light maps, the per-vertex data, the
  material/light-map combination and the vertex diffuse equation.
- Skeleton pose evaluation and skinning: the hierarchy, weights and joint
  indices are extracted, but the bind-pose and animation keyframe
  evaluation are open. `animatio`/`animreel` payloads are extracted raw.
- Collision semantics: the raw `collmodc` payload is extracted for every
  model, but the compressed vertices, edges/faces and the collision
  runtime are not decoded. Entity/player logic is also open.
- Audio playback is implemented: `pc.rkv` streams decode (floor-0 via
  `libvorbisfile`, floor-1 via the bundled fallback), one music voice and
  one-shot events mix to SDL, and `--event-ogg`/`--music-ogg` demonstrate
  it. Wiring the `sound`/`strmfile` event table to gameplay is pending.
- `region`, `planinfo`, `bpackage`, `strtable`, `musicbnk`, `texfont`,
  `liteinfo`.
- The oracle frame comparison (the original launches under Wine/Xvfb).
- Audio or gameplay (the SDL viewer has camera input only).
- The installer's native RAR decoder is not ported; development
  extraction uses `unrar`. The game does not need the installer decoder.

## Known unknowns (ranked)

| # | Unknown | Next action | Read first |
|---|---|---|---|
| 0 | Shared-model instance light maps and material combination | Map shared `model` instances and combine the light map with `mlyrcolr`/`mlyrctrl` | `docs/FORMATS/litedata.md` |
| 1 | Shared `model` entries and entity semantics | Bind the shared models and decode the consumers | `docs/FORMATS/models.md` |
| 2 | `game.erg` boot-time keys | Decompile the key lookup in `bo2.exe` | `docs/FORMATS/erg.md` |
| 3 | Original logic tick | Measure from the oracle once it runs, or from the main loop | `docs/ARCHITECTURE.md` |
| 4 | Level list and streaming model | Derive it from the installer script and `list.txt` | `docs/GAMEPLAY.md` |
| 5 | Oracle frame comparison | Run the original under Wine/Xvfb and compare frames | `docs/ORACLE_COMPARE.md` |
| 6 | `animatio` and `animreel` layout | Decompile the animation loader | `docs/FORMATS/models.md` |

## Next steps

1. Map the shared-model instances and combine the light map with the material layers.
2. Bind the shared `model` entries and decode animation/skinning.
3. Compare the SDL2/OpenGL frame with the original under Wine/Xvfb.

## Rules

- Decompile before guessing. No `FUN_` address and dump, no claim.
- Translate, do not redesign.
- No assets, original code or decompiled dumps in the repository.
- One logical change per commit, with its tests and docs.
