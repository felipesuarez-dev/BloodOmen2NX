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

## What is not done

- `mlyrcolr`/`mlyrctrl` material modulation.
- Animation and skinning (`animatio`, `animreel`, `vtxweigh`, `jointidx`).
- `region`, `planinfo`, `bpackage`, `strtable`, `musicbnk`, `texfont`,
  `liteinfo`.
- SDL2 + OpenGL host backend and the oracle frame comparison (the
  development container has no libraries, display or Wine).
- SDL2 + OpenGL host backend (the development container has no libraries
  or display).
- `game.erg` parser details.
- Audio, input or gameplay.
- The installer's native RAR decoder is not ported; development
  extraction uses `unrar`. The game does not need the installer decoder.

## Known unknowns (ranked)

| # | Unknown | Next action | Read first |
|---|---|---|---|
| 0 | `mlyrcolr`/`mlyrctrl` modulation | Decompile the material setup and apply it | `docs/FORMATS/models.md` |
| 1 | `cellinst`, `cellight` and `cellmark` records | Decompile the dispatcher branches | `docs/FORMATS/models.md` |
| 2 | `game.erg` parser details | Decompile the key lookup in `bo2.exe` | `docs/FORMATS/erg.md` |
| 3 | Original logic tick | Measure from the oracle once it runs, or from the main loop | `docs/ARCHITECTURE.md` |
| 4 | Level list and streaming model | Derive it from the installer script and `list.txt` | `docs/GAMEPLAY.md` |
| 5 | Level renderer | SDL2+OpenGL and the oracle frame | `docs/ARCHITECTURE.md` |
| 6 | `animatio` and `animreel` layout | Decompile the animation loader | `docs/FORMATS/models.md` |

## Next steps

1. Apply the `mlyrcolr`/`mlyrctrl` material modulation.
2. Decode `cellinst`/`cellight`/`cellmark`, animation/skinning and the
   shared `model` entries.
3. Add the SDL2 + OpenGL backend and use the original as the oracle.

## Rules

- Decompile before guessing. No `FUN_` address and dump, no claim.
- Translate, do not redesign.
- No assets, original code or decompiled dumps in the repository.
- One logical change per commit, with its tests and docs.
