# State

Current milestone: **M3 — playable slice**. M0 (binary truth), M1
(containers and assets) and M2 (host skeleton and first frame) are complete.

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
  different groups. The palette high byte is a shadow factor, not a
  luminance: the host samples `1 - palette`, which takes the `sl01` spawn
  world MAE from 35.58 to 8.69/255 and the luma correlation from -0.03 to
  0.37. Shared-model instance mapping and the per-vertex data are open.
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

- The original game runs locally as the oracle: the pristine `bo2.exe`
  reaches the menu and `sl01` gameplay under Wine 9.0 on a private Xvfb
  with wined3d/llvmpipe. `scripts/oracle/wine_setup.sh` prepares the
  prefix (HKLM `InstallPath`/`MoviePath` read by `FUN_00578b80`, a valid
  video mode, the flat `data/` layout) and `tools/oracle/wine_capture.py`
  replays a route and records frames, video and inputs. The Wine spawn
  frame is within 4.9/255 mean error of the Windows frame. The same run
  samples Kain's state (position, yaw, velocity, health, lore) and the
  follow camera's pose from the process memory at 50 Hz into `state.csv`;
  every offset has a `FUN_` source. See `docs/ORACLE_WINE.md`.
- The oracle replay and state comparison harness is in place:
  `tools/oracle/prepare_replay.py` converts a Wine capture to a per-tick
  input timeline, `bo2nx --replay` steps the port's controller and follow
  camera, and `tools/oracle/compare_state.py` reports the per-phase error.
  On `sl01_basic` the spawn matches within 0.05 u and the first divergence is
  `after_w` at 2.5 u; on `sl01_camera` the player matches within 0.08 u
  through the turns and diverges at the jump (11 u). A ±2-row shift search
  picks 0 for both captures: the jump apex (281.35) and landing frame (3265)
  match, but the port starts about one row late. The W run shows the
  collision gap: the oracle stops at x = 326.15 while the port stops at
  x = 351.60 (25.44 u). The `collmodc` payload is decoded: a 6-byte point
  list, 4-byte packed triangle records (`v0 = b0`, `v1 = b1 >> 1`,
  `v2 = b2 >> 2` plus the attribute byte), 24-byte solid spheres and 20-byte
  face records closed by a 28-byte trailer. All 660 `sl01` payloads (and 76
  in `sanctuary`) decode, including the variant that wraps the record in a
  second named header. `CollisionWorld` takes an explicit source; gameplay
  uses the `collmodc` hull (36,129 world triangles in `sl01`) and the render
  mesh and the broad-phase boxes stay as diagnostics. The port's stop is the
  hull's model 68 wall at x = 352.0; the oracle's is the model 57 wall at
  x = 326.75, reached after sliding from y = 57.13 to y = 44.14 between
  frames 2311 and 2318. The remaining gap is the player volume: the blocking
  surface is 10 u above the feet (a sphere at the feet cannot reach it),
  while Kain's model ships three spheres (z = 52.0/49.6/23.6,
  r = 19.8/19.3/15.0) that are too wide (they would stop at x = 254). The
  live trace shows the movement has no body cast: a vertical
  ground/step probe (r=1), the run probe (r=8, 32 u ahead at z+48) and the
  camera casts. The collision candidates are filtered by the mover's cell
  and its portal neighbours (the cells' `terrains` lists index the
  `cellmdls` models). The portal fields are **symbols**, not cell indices;
  resolving them (cell01 symbol 925, portal 930 <-> 931 = cell02) links the
  graph, so the move now reaches the cell-1 geometry. The run probe then
  stops at x = 234.14 on a slanted surface at (274, 59, 254) that the oracle
  ignores: `after_w` max 92.57 u, mean 44.20 u. That triangle belongs to
  model 57 itself, so a per-object class filter cannot explain it. A live
  trace of the narrow phase (`wine_narrow_20261112`) shows the run probe does
  **not** go through `FUN_0062ea60`: only the camera cast (z = 284, r = 10,
  object `0x02ad4008`) does. It uses the general shape cast `FUN_0062d8f0`
  and its leaf `FUN_0062dfc0`, where the test is **per object**: the run
  probe passes query flag 2 and the object is tested when its class byte
  (`+0x38`) has bit 0. The ramp and the 45 degree wall are both model 57, so
  no prim filter separates them; the packed triangle's 3-bit category only
  affects the edge/corner test. The port misread the run probe as a hard
  stop: the original only raises the wall flag (`KainState+0x588` bit
  `0x100`, tunedata threshold 0.9). With the flag only, the port follows the
  oracle within 0.4 u until frame 2308 (x = 259.6 against 260.0) and then
  runs to the model 68 wall at x = 351.55; `after_w` improves from mean 44.20
  / max 92.57 to mean 13.31 / max 29.18. The movement response
  `FUN_0041ee60` is a **no-op** in the W run: a live trace
  (`wine_blocker_20261112`) shows the blocker pointers `+0x450/+0x454` stay 0
  and are never written, and the wall projection (`FUN_0041f460` at
  `0x41f54b`, mode `+0x45C`, normal `+0x478`) is inactive (`wine_wall2_20261112`
  reads mode 0 and normal (0,0,1)). The position is written by the
  `FUN_0070eac0` "set position" message (type `DAT_009105cc`, payload
  `+0x30..+0x38`, type `+0x40` 4/8). At the slide it corrects the target by
  (-1.05, -1.05) per tick: the 2.15 u move projected on a plane with normal
  (0.707, 0.707, 0), i.e. 230 cos 45 = 162 u/s along (1, -1), the gate value;
  the stop at 326.35 is the same path clamping the position. The sender and
  the projection source are the open piece. Entity collision is ruled out:
  the `.etp` flags `staticcollision` (+0x63) and `collisionresponse_stop`
  (+0x64) are parsed and counted, and `sl01` has **0** static collision
  entities. The spawn camera pull-in differs because the port's collision
  lacks the helper wall. The full route/state gate remains open (BO2NX-37).
- The native Windows oracle harness is available: a reverse SSH server on
  the Windows host, `tools/oracle/oracle_client.py`, a capture validator and
  an input/frame aligner. `capture_20261028_0150` is the first complete
  capture: it loads the `sl01` checkpoint save, records walk, run and jump,
  and passes `validate_capture.py` (hashes, manifest, 10,233 `state.csv`
  rows, 7 PPM key frames). `VIEW` is the identity in gameplay, so the camera
  pose and the player state are not captured; attack and camera rotation
  are not confirmed. See `docs/ORACLE_WINDOWS.md` and
  `docs/ORACLE_WINDOWS_NATIVE.md`.
- The fixed-step loop (`FixedStep`) and `--headless --frames N` are in
  place; the accumulator is clamped to 0.25 s and the tick rate defaults
  to 60 Hz. `EntityWorld` stores entities and filters them through a
  sphere frustum; `CellStreamer` activates cells by camera distance with
  hysteresis; `CellNavigation` builds a cell graph from portals and finds
  a shortest path. The level viewer populates the world from the 13
  sanctuary region entities and reports frustum-visible entities and active
  cells.
  A triangle-soup `CollisionWorld` provides sphere overlap and ray casts.
  `PlayerController` moves Kain with the original tuning read from the
  `kain` tunedata (`resources/levels/kain/kain.big`): tank turning at
  2.5 rad/s over the 0.1 stick deadzone (`FUN_0041ecf0`), the integrator of
  `FUN_00432030`, two-phase jump gravity (-2500 rising, -700 falling,
  `FUN_00442ae0`) and the held-jump boost derived by `FUN_00451d00`.
  Ground speed is animation root motion in the original: forward is the
  `kain/run` root speed (200 u/s) x the run clip rate (1.15) = 230 u/s and
  back is the `kain/walk_backwards` root speed (62.5) x its clip node speed
  in the `kain` animatio graph (1.7) = 106.25 u/s (`docs/FORMATS/animreel.md`,
  `docs/FORMATS/animatio.md`). Partial stick runs the FUN_00445ba0 run/walk
  blend (walk root speed 56.3485 u/s); at s = 0.5 it gives 134.85 u/s. The 100 u/s start phase and Shift+W speed are
  still measured in the Wine oracle. Jumps have the original air control
  (`FUN_004436e0`, `FUN_00441f20`, `FUN_00442240`, `FUN_004423b0`, launch
  blocks from `FUN_00452030`): 323/205/203 u/s forward/back/side, as the
  oracle measures. The follow camera follows the yaw with a limited
  acceleration (`FUN_00407ec0`), lags in height after a jump
  (`FUN_00407c70`) and smooths the range back after a collision pull-in
  (`FUN_004088c0`). `bo2nx --play` drives it in the SDL level viewer; glide,
  ledge falls and the collision size are still provisional (see
  `docs/GAMEPLAY.md`). Localized resources are looked up in Spanish like the
  original build (`FUN_006c8fb0`, `FUN_004aae40`) and `strtable` text is read
  (`docs/FORMATS/strtable.md`). Entities whose `.etp` template has
  `flag helperobj` are not drawn (`FUN_0070f100`). The `region` entity table is
  decoded (15,632 entities, 533 model names, 0 errors); entity models are
  resolved and appended as geometry (13 in sanctuary, 119 in cy01, 72 in
  lc01), with named light-map selection and a vertex-count fallback.
  Save states serialize the level name, player transform, HUD and tick
  with a versioned binary format; checkpoint slots, checkpoint triggers,
  F5/F9 save/load and corruption checks are implemented. A minimal
  health/mana HUD bar is drawn in `--play`. Combat has attack cooldown,
  combo state, entity health/damage and a forward hit-sphere query; the
  faithful original values and animation/weapon effects are still pending.
  The compressed `collmodc` runtime and game-specific entities remain
  pending.

## What is not done

- Shared-model instance light maps, the per-vertex data, the
  material/light-map combination and the vertex diffuse equation.
- Skeleton pose evaluation and skinning: the hierarchy, weights and joint
  indices are extracted, but the bind-pose and animation keyframe
  evaluation are open. `animreel` clips and the `animatio` graph are decoded
  (`docs/FORMATS/animreel.md`, `docs/FORMATS/animatio.md`); clip blending
  and the graph state machine are not ported.
- Collision semantics: the raw `collmodc` payload is extracted for every
  model, but the compressed vertices, edges/faces and the collision
  runtime are not decoded. Entity/player logic is also open.
- Audio playback is implemented: `pc.rkv` streams decode (floor-0 via
  `libvorbisfile`, floor-1 via the bundled fallback), one music voice and
  one-shot events mix to SDL, and `--event-ogg`/`--music-ogg` demonstrate
  it. Wiring the `sound`/`strmfile` event table to gameplay is pending.
- `planinfo`, `bpackage`, `musicbnk`, `texfont` and `liteinfo`.
- Oracle state beyond position, velocity, health, lore, the animation id
  and the camera: the logic tick and the grounded flag are not located.
- The `sl01` port frame: helper objects are hidden and the `.slm` palette is
  inverted as a shadow map; the world MAE is 8.69/255 with luma correlation
  0.37. Kain's model is loaded and placed in the level viewer
  (`--kain-model kain.big --kain-at x,y,z`: 2,846 triangles and 3 DDS
  textures) but drawn in the bind pose; skeleton evaluation/skinning, the
  HUD, the green `bo2cinematic001` marker and the exact palette texture
  format remain (BO2NX-68).
- Gameplay parity beyond turning, ground speeds, partial stick, jumping,
  air control and the camera range blend: the start phase and Shift speed,
  glide, ledge falls, combat (it needs combat mode; map in BO2NX-34) and
  weapon values are pending.
- The installer's native RAR decoder is not ported; development
  extraction uses `unrar`. The game does not need the installer decoder.

## Known unknowns (ranked)

| # | Unknown | Next action | Read first |
|---|---|---|---|
| 0 | Shared-model instance light maps and material combination | Map shared `model` instances and combine the light map with `mlyrcolr`/`mlyrctrl` | `docs/FORMATS/litedata.md` |
| 1 | Shared `model` entries and entity semantics | Bind the shared models and decode the consumers | `docs/FORMATS/models.md` |
| 2 | `game.erg` boot-time keys | Decompile the key lookup in `bo2.exe` | `docs/FORMATS/erg.md` |
| 3 | Original logic tick | Decompile the main loop and confirm it under the Wine oracle | `docs/ARCHITECTURE.md` |
| 4 | Streaming model | The 11 chapters are recovered (`docs/LEVELS.md`); the region load triggers are not decoded | `docs/LEVELS.md` |
| 5 | Logic tick and animation state in the oracle | Locate them in the decompilation and add them to `state.csv` | `docs/ORACLE_WINE.md` |
| 6 | Graph state machine | Port the `animatio` state machine (FUN_00642c00) for blending and the combo chain | `docs/FORMATS/animatio.md` |

## Next steps

1. Map the shared-model instances and combine the light map with the material layers.
2. Bind the shared `model` entries and decode animation/skinning.
3. Port the first claw combo with combat mode, the cone test and the damage
   table (BO2NX-34), and find what hides the remaining pyramid-model helpers
   and lights `sl01` (BO2NX-68).
4. Compare the port against a Wine `state.csv` route tick by tick
   (BO2NX-37), then wire the Spanish string tables into the front end and
   HUD (BO2NX-44).

## Rules

- Decompile before guessing. No `FUN_` address and dump, no claim.
- Translate, do not redesign.
- No assets, original code or decompiled dumps in the repository.
- One logical change per commit, with its tests and docs.
