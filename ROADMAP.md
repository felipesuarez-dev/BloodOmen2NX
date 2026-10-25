# Roadmap (living document)

Current state and next steps: `STATE.md`. The Status cell of each row MUST
start with `DONE`, `WIP` or `TODO`; `tools/gen/progress.py` reads the
platform table separately.

Target: native Nintendo Switch port of Blood Omen 2 (PC 1.02 Spanish
GoldMaster), with a Linux host backend for development and oracle
comparison.

| Gate | Phase | Exit criteria | Status |
|---|---|---|---|
| M0 | Laboratory and binary truth | Hashes, PE triage, SFXI, `game.erg` and Ghidra inventory | DONE — install materialization moved to M1 and completed there |
| M1 | Containers and assets (GO/NO-GO) | `.big` and `pc.rkv` validated against the installed tree; one asset extracted; formats documented | DONE — 198/198 installer chunks, 179 `.big`/246,332 entries, 2,666 RKV streams, DDS and model extracted |
| M2 | Host skeleton and first frame | Host backend renders one level with textures and a free camera; frame comparable to the oracle | WIP — headless first frame and C++ readers done; level geometry, SDL2+OpenGL and the oracle frame pending |
| M3 | Playable slice | First level playable start to finish; route replay test and state comparison | TODO |
| M4 | Complete systems | Enemies, combat, dark gifts, scripting, menus, saves, audio and videos | TODO |
| M5 | Complete content | All levels, bosses and unlockables; parity battery green | TODO |
| M6 | Release | Performance, handheld/docked, packaging, docs, licence decision | TODO |

## Platform features

| Feature | State | Note |
|---|---|---|
| Host build skeleton | done | CMake presets, smoke binary and tests |
| Installer container (SFXI) | done | Reader and synthetic test |
| Installer chunk decompressor | done | 198/198 chunks; 227-file tree |
| BIG/RKV parsers | done | 179 `.big`/246,332 entries; 2,666 RKV streams |
| Validated asset extraction | done | DDS and model extracted; manifest in `docs/PROVENANCE.md` |
| Headless host first frame | done | Real DDS texture, free camera, PPM output |
| Host renderer (OpenGL) | todo | SDL2/OpenGL/display unavailable in the dev container |
| Switch renderer (deko3d) | todo | M2+ |
| Audio | todo | M2+ |
| Input | todo | M2 |
| Video (Bink) | todo | M4; transcode at install time |

## Rules

- One milestone at a time. Do not start the next until the exit criteria
  of the current one holds.
- The M1 gate passed on 2026-10-25; M2 is the current milestone.
- Every claim needs a `FUN_` address and a local decompile.
- Every new gap becomes a tracked work item, not a note in a doc.
