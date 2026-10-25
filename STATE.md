# State

Current milestone: **M0 — laboratory and binary truth**.

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
- Installer script read: the numbered `data/*.dat` are compressed chunks
  that expand to `pc.rkv`, `code.big`, level `.big` files and `.san`
  animations; the movies are copied to `data/` unchanged.

## What is not done

- The installer decompressor is not reversed: the installed tree cannot
  be materialized yet.
- `pc.rkv` not expanded or parsed.
- The `.dat` payload codec is unknown.
- Ghidra project not created; `bo2.exe` not analysed.
- No function map rows.
- No renderer, audio, input or gameplay.

## Known unknowns (ranked)

| # | Unknown | Next action | Read first |
|---|---|---|---|
| 0 | Installer decompressor for `data/*.dat` | Reverse the routine in `install2.dat` or the reader in `bo2.exe` | `docs/FORMATS/dat.md` |
| 1 | `pc.rkv` layout | Expand one chunk, parse the archive index | `docs/FORMATS/dat.md` |
| 2 | `game.erg` parser details | Decompile the key lookup in `bo2.exe` | `docs/FORMATS/erg.md` |
| 3 | Original logic tick | Measure from the oracle once it runs, or from the main loop | `docs/ARCHITECTURE.md` |
| 4 | Level list and streaming model | Derive it from the installer script and `list.txt` | `docs/GAMEPLAY.md` |
| 5 | Renderer pipeline | First level render on the host backend | `docs/ARCHITECTURE.md` |

## Next steps

1. Reverse the installer decompressor and expand one `data/*.dat` chunk.
2. Create the Ghidra project and recover functions missed by
   auto-analysis.
3. Seed `re/FUNCTION_MAP.csv` from the recovered functions.
4. Parse `game.erg` and `pc.rkv`; write the parsers with synthetic
   fixtures.
5. Re-pack one asset and verify it with the original.
6. Start the host backend once M1 passes.

## Rules

- Decompile before guessing. No `FUN_` address and dump, no claim.
- Translate, do not redesign.
- No assets, original code or decompiled dumps in the repository.
- One logical change per commit, with its tests and docs.
