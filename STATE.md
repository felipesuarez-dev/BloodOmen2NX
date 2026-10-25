# State

Current milestone: **M1 — containers and assets**. M0 (laboratory and binary truth) is complete.

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
- `tools/bo2fmt/big.py` and `rkv.py` implemented with synthetic tests.
- Installer archive identified: custom encrypted container (32-round
  XTEA-like cipher, fixed S-box, embedded password), not zlib or RAR.
- Installer script read: the numbered `data/*.dat` are compressed chunks
  that expand to `pc.rkv`, `code.big`, level `.big` files and `.san`
  animations; the movies are copied to `data/` unchanged.

## What is not done

- No installed tree yet: parsers cannot be validated against real data.
- `.big` and `pc.rkv` parsers are spec-level, not decoded for this build.
- Per-type payloads (models, cell groups, animation, textures) are open.
- Function map not triaged by subsystem yet.
- Installer decompressor not ported; it is optional, not a gate.
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

1. Triage the 18,097 game candidates by subsystem and recover functions
   missed by auto-analysis.
2. Reverse the installer decompressor and expand one `data/*.dat` chunk.
3. Parse `game.erg` and `pc.rkv`; write the parsers with synthetic
   fixtures.
5. Re-pack one asset and verify it with the original.
6. Start the host backend once M1 passes.

## Rules

- Decompile before guessing. No `FUN_` address and dump, no claim.
- Translate, do not redesign.
- No assets, original code or decompiled dumps in the repository.
- One logical change per commit, with its tests and docs.
