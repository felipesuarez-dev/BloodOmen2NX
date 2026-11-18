# Provenance

How every external artifact used by this project was obtained. Game files
and original binaries are never committed; only hashes and derived facts
are recorded here.

## Target build

- Game: Blood Omen 2
- Platform: PC (Windows, 32-bit)
- Edition: Spanish GoldMaster, version 1.02
- Developer: Crystal Dynamics
- Publisher: Eidos Interactive

The embedded build path in `bo2.exe` is
`P:\gogames\bo2\build\bo2___Win32_Spanish_GoldMaster\bo2.pdb`, which
identifies the GOG-derived Spanish GoldMaster build. The version comes from
`data/version.raw`, the in-game version overlay (a 128-byte `!WAR` header and
a 50x30 RGBA bitmap), which reads "1.02". `readme.rtf` still says 1.00.

## Reference media

| File | Size (bytes) | SHA-256 |
|---|---:|---|
| `Legacy Of Kain Blood Omen II.iso` | 1,207,830,528 | `cb118cd46d778ce4e8cb768a8f2bb71def006b8211611e6d4e05cf0b1b64456e` |

Local copy: outside the repository, in the workspace materials
directory (gitignored).

## Extracted binaries

| File | Size (bytes) | SHA-256 |
|---|---:|---|
| `setup.exe` | 634,880 | `14da8333c668ff47573cd967427d48deae0725d5489487c15a524998f3fe2d10` |
| `install2.dat` | 3,579,904 | `0ad7ed62aba32bd78ce3e9680ef607c8f457f90fcf4d747656a6be8f0b54473b` |
| `bo2.exe` | 5,378,124 | `91758398a17de9c9f974a8a102762a534fb523d40fa6868a5e5a4f0483b2753c` |

Local copies live under `external/iso/` (gitignored).

## Installer contents

`install2.dat` contains an SFXI resource (`FILESYS/107`) with 24 entries.
Two are relevant to the port:

| Entry | Compressed | Uncompressed |
|---|---:|---:|
| `game.erg` | 754 | 1,708 |
| `list.txt` | 6,816 | 68,932 |

The extracted copies live under `external/installer/sfxi/` (gitignored).
The repository documents the container format and the configuration
shape, not their content.

## Installed tree

Rebuilt during M1 from the reference ISO, the SFXI resources of
`install2.dat` and the 198 `data/*.dat` chunks with a tool that was removed
from the repository on 2026-11-15 ([`FORMATS/dat.md`](FORMATS/dat.md)).
The official installer gives the same tree. The local copy is
`external/installed/` (gitignored).

| Metric | Value |
|---|---:|
| Files | 227 |
| Size | 1,968,053,271 bytes |
| Manifest SHA-256 | `b17a1e8ab9a9af108c7c6949821713f78f1d89424674167706f5fef5c86032a1` |

Manifest command:

```sh
cd external/installed
find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum
```

## Code provenance

The policy for everything written into this repository:

- **What the port reproduces.** The behaviour of the game and the layout
  of its data files, which the port must read to work with the player's
  own copy. Both are studied in a legally obtained copy, by static analysis
  and by observing the running game, for interoperability on other
  platforms.
- **Two roles.** The analysis reads the binary and the oracle and writes
  specifications into `docs/` and the tracker. The implementation writes
  code from those specifications only, without opening the binary or the
  decompiled output ([`PORTING_GUIDE.md`](PORTING_GUIDE.md)). This split is
  the rule since 2026-11-14; code written before then came from the
  analysis directly, and the table below says so per file.
- **Exception, decided by the maintainer on 2026-11-15.** The script machine
  (the readers of the script packages, its memory, its linker and its
  interpreter) is written by the analysis role too, from the specification
  in [`SCRIPTS.md`](SCRIPTS.md) and the tracker, without opening the
  decompiled output while writing. Those files are marked *analysis* in the
  table. Everything else keeps the split.
- **Extension, decided by the maintainer on 2026-11-15 (night).** An issue
  of the specification queue that blocks the work of the analysis role on
  the script machine may be done by the analysis role. The tracker says so
  in the issue, and the files it touches are marked *analysis* in the table.
- **References, not copies.** An address such as `FUN_00443a50` in a
  comment, in `docs/` or in `re/FUNCTION_MAP.csv` records where a rule was
  observed. Decompiled output, disassembly listings and the game's text,
  art and audio are never committed (`scripts/check_no_assets.sh` checks
  names and contents).
- **Third parties.** Code from another project enters only with its license
  and notice (`NOTICE`); GPL-only projects are read for facts, never copied
  ([`PRIOR_ART.md`](PRIOR_ART.md)).
- **Removed.** The decoder of the installer chunks reproduced the
  installer's encryption and was removed on 2026-11-15
  ([`FORMATS/dat.md`](FORMATS/dat.md)).
- **Releases.** No binary of the port is published before a legal review
  (`scripts/release.sh` refuses `--publish`).

## Origin of each file

"Based on": *data format* is the layout of the game's files, *behaviour*
is a rule of the original observed in the analysis, *port's own* has no
counterpart in the original. "Written from": *analysis* means the code was
written while studying the original, *specification* means it was written
from `docs/` and an issue only (the `[sin descubrimiento]` issues of the
tracker), *own design* means there was nothing to reproduce. "Functions
cited" counts the distinct `FUN_` addresses named in the file. The platform
backends (`src/platform/`), the oracle tools (`tools/oracle/`, which read
the running game's memory), the generators (`tools/gen/`), the Ghidra
helpers (`tools/ghidra/`) and the scripts (`scripts/`) are the port's own
design. `third_party/stb_vorbis.c` is third-party code (`NOTICE`).

| File | What it is | Based on | Written from | Functions cited |
|---|---|---|---|---:|
| `src/engine/big_archive.cpp` | Reader of the `.big` containers | data format | analysis | 0 |
| `src/engine/cell_group.cpp` | Reader of the level geometry, models, markers and light maps | data format; load rules | analysis, then specification | 16 |
| `src/engine/dds.cpp` | DDS texture decoder and blend class | public format; blend rule | own design | 1 |
| `src/engine/anim_reel.cpp` | Reader of the animation clips | data format | analysis | 10 |
| `src/engine/anim_graph.cpp` | Reader of the animation graphs | data format | analysis | 24 |
| `src/engine/anim_machine.cpp` | Animation state machine | behaviour | analysis | 15 |
| `src/engine/skeleton_pose.cpp` | Skeleton pose, blending and skinning | behaviour | analysis | 11 |
| `src/engine/region.cpp` | Reader of the region entities, their properties and the script package | data format | analysis | 2 |
| `src/engine/script_package.cpp` | Reader of the compiled script packages | data format | analysis | 10 |
| `src/engine/script_program.cpp` | Linker of the script packages of a region | behaviour | analysis | 4 |
| `src/engine/script_machine.cpp` | Interpreter of the script machine | behaviour | analysis | 76 |
| `src/engine/script_creature_natives.cpp` | Natives of the creature scripts: the searches, position and rotation, existence, kill, hibernate, the drawing and the simulation flags | behaviour | analysis | 47 |
| `src/engine/script_natives.cpp` | The natives of basescript | behaviour | specification | 34 |
| `src/engine/script_value_natives.cpp` | The natives of the value classes | behaviour | specification | 54 |
| `src/engine/script_sound_natives.cpp` | Natives of sound, soundgroup and effect | behaviour | specification | 70 |
| `src/engine/script_timer.cpp` | Timers of the scripts: value type, task and natives | behaviour | analysis | 18 |
| `src/engine/script_level.cpp` | The creatures of a level for the script machine (its host) | behaviour | analysis | 27 |
| `src/engine/crt_random.cpp` | The rand of the C library the game links | behaviour | analysis | 1 |
| `src/engine/random.cpp` | The game's random number generator | behaviour | specification | 3 |
| `src/engine/script_memory.cpp` | 32-bit memory of the script machine | port's own; the 4-byte cells of the original | analysis | 0 |
| `src/engine/string_table.cpp` | Reader of the string tables | data format | analysis, then specification | 0 |
| `src/engine/erg.cpp` | Reader of `game.erg` | data format | analysis | 0 |
| `src/engine/entity_template.cpp` | Reader of the entity templates | data format | analysis | 1 |
| `src/engine/pixmap_font.cpp` | Reader of the game font and glyph placing | data format; behaviour | specification | 3 |
| `src/engine/text.cpp` | Parser of the text commands of a string | behaviour | specification | 1 |
| `src/engine/render_order.cpp` | Sort value and key of the sorted pass | behaviour | specification | 3 |
| `src/engine/collision.cpp` | Collision queries | port's own; one rule from the original | own design | 1 |
| `src/engine/audio.cpp` | Audio mixer | port's own | own design | 0 |
| `src/engine/ogg.cpp` | Ogg Vorbis decoding through stb_vorbis | port's own | own design | 0 |
| `src/engine/navigation.cpp` | Navigation helpers | port's own | own design | 0 |
| `src/engine/tick.cpp` | Fixed-step clock | port's own | own design | 0 |
| `src/engine/world.cpp` | Entity world | port's own | own design | 0 |
| `src/engine/data_check.cpp` | Data checker over every container | port's own tooling | own design | 0 |
| `src/game/camera.cpp` | Follow camera | behaviour | analysis | 11 |
| `src/game/hud_scene.cpp` | HUD scene | behaviour | analysis, then specification | 11 |
| `src/game/kain_actor.cpp` | Kain's actor state | behaviour | analysis | 2 |
| `src/game/kain_animation.cpp` | Kain's animation state | behaviour | analysis | 8 |
| `src/game/kain_tuning.cpp` | Reader of Kain's tuning data | data format | analysis | 11 |
| `src/game/kain_vitals.cpp` | Kain's health, lore and rage | behaviour | analysis, then specification | 5 |
| `src/game/player.cpp` | Kain's movement | behaviour | analysis | 23 |
| `src/game/replay.cpp` | Replay of an oracle input timeline | port's own tooling | own design | 0 |
| `src/game/save.cpp` | Save slots of the port | port's own | own design | 0 |
| `src/main.cpp` | Host program and its options; the script tick of `--replay --scripts` | port's own; the tick order is behaviour | own design; the script tick, analysis | 4 |
| `src/tools/check_data.cpp` | Command line of the data checker | port's own tooling | own design | 0 |
| `src/common/version.cpp` | Version string | port's own | own design | 0 |
| `tools/bo2fmt/big.py` | Python reader of the `.big` containers | data format | analysis | 1 |
| `tools/bo2fmt/animatio.py` | Python reader of the animation graphs | data format | analysis | 40 |
| `tools/bo2fmt/animreel.py` | Python reader of the animation clips | data format | analysis | 42 |
| `tools/bo2fmt/audio.py` | Python reader of the sound tables | data format | analysis | 0 |
| `tools/bo2fmt/bo2fmt.py` | Command line of the Python readers | port's own tooling | own design | 0 |
| `tools/bo2fmt/font.py` | Python reader of the game font | data format | specification | 1 |
| `tools/bo2fmt/model.py` | Python reader of the models | data format | analysis | 1 |
| `tools/bo2fmt/region.py` | Python reader of the region entities | data format | analysis | 0 |
| `tools/bo2fmt/rkv.py` | Python reader of `pc.rkv` | data format (layout first published by an MIT project, see NOTICE) | analysis | 0 |
| `tools/bo2fmt/sfxi.py` | Python reader of the installer resource container | data format | analysis | 0 |
| `tools/bo2fmt/strtable.py` | Python reader of the string tables | data format | analysis, then specification | 1 |
| `tools/bo2fmt/textures.py` | Python texture export | public format | own design | 0 |

## Chain of custody

1. Keep the reference ISO and the extracted files under `external/`.
2. Record hashes in this file before first use.
3. Run `scripts/check_no_assets.sh` before every commit.
4. Never commit game data, original code, decompiled dumps or verbatim
   game text, and follow "Code provenance" above.
