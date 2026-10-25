# Roadmap (living document)

Current state and next steps: `STATE.md`. The Status cell of each row MUST
start with `DONE`, `WIP` or `TODO`; `tools/gen/progress.py` reads the
platform table separately.

Target: native Nintendo Switch port of Blood Omen 2 (PC 1.02 Spanish
GoldMaster), with a Linux host backend for development and oracle
comparison.

| Gate | Phase | Exit criteria | Status |
|---|---|---|---|
| M0 | Laboratory and binary truth | Installed layout materialized by static extraction, SHA-256 manifest, Ghidra project with recovered functions, compiler and imports identified | WIP — hashes, PE triage, SFXI and `game.erg` done; decompressor and Ghidra pending |
| M1 | Containers and assets (GO/NO-GO) | `game.erg` and `.dat` decoded; one asset extracted, re-packed and accepted by the original; formats documented | TODO |
| M2 | Host skeleton and first frame | Host backend renders one level with textures and a free camera; frame comparable to the oracle | TODO |
| M3 | Playable slice | First level playable start to finish; route replay test and state comparison | TODO |
| M4 | Complete systems | Enemies, combat, dark gifts, scripting, menus, saves, audio and videos | TODO |
| M5 | Complete content | All levels, bosses and unlockables; parity battery green | TODO |
| M6 | Release | Performance, handheld/docked, packaging, docs, licence decision | TODO |

## Platform features

| Feature | State | Note |
|---|---|---|
| Host build skeleton | done | CMake presets, smoke binary and tests |
| Installer container (SFXI) | done | Reader and synthetic test |
| Asset containers | todo | Installer decompressor and `pc.rkv` |
| Host renderer (OpenGL) | todo | M2 |
| Switch renderer (deko3d) | todo | M2+ |
| Audio | todo | M2+ |
| Input | todo | M2 |
| Video (Bink) | todo | M4; transcode at install time |

## Rules

- One milestone at a time. Do not start the next until the exit criteria
  of the current one holds.
- M1 is a hard gate. If the containers do not yield in six weeks, stop
  and replan instead of guessing.
- Every claim needs a `FUN_` address and a local decompile.
- Every new gap becomes a tracked work item, not a note in a doc.
