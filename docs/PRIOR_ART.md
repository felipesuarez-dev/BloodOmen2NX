# Prior art

Public work on Blood Omen 2 and adjacent technology, verified on
2026-10-25. This page records what we can reuse, what we must only use as
an oracle, and what does not apply. The full local research report is
kept outside the repository.

## What we reuse

| Project | License | Why |
|---|---|---|
| [`Grupa-Polonizacyjna-Nosgothica/blood-omen-2-pl`](https://github.com/Grupa-Polonizacyjna-Nosgothica/blood-omen-2-pl) | MIT | `pc.rkv` parser, `.cnv` phrase format, font tool, GOG symbol addresses, no-asset patch model |
| [`vgmstream/vgmstream`](https://github.com/vgmstream/vgmstream) | permissive (ISC-style) | PS2 and GameCube RKV audio, only if console audio is ever needed |

The MIT project is the only code category we can incorporate with
attribution. Its parser is the basis of `tools/bo2fmt/rkv.py`; our
implementation is written from the format facts and covered by synthetic
fixtures.

## What we use only as an oracle

| Project | License | Why not copy |
|---|---|---|
| [`suloku/BO2_patcher`](https://github.com/suloku/BO2_patcher) | GPL-3.0 | Strong copyleft; `BO2BigFile.h` documents the `.big` layout |
| [`wattostudios/GameExtractor`](https://github.com/wattostudios/GameExtractor) | GPL-2.0 | Strong copyleft; `Plugin_BIG_GOEFILE` and `Plugin_RKV` are independent parsers |
| [`DKDave/Scripts`](https://github.com/DKDave/Scripts) | none | No rights granted; useful to confirm the RKV sector formula |
| [`MeinerI/Blood-Omen-2-Legacy-of-Kain`](https://github.com/MeinerI/Blood-Omen-2-Legacy-of-Kain) | none | No rights granted; resource taxonomy and experimental model notes |

Reading a GPL implementation to learn a format is legitimate. Copying or
closely translating it would relicense the port, which is planned as MIT.
Our parsers are written from the documented offsets and tested with
synthetic fixtures.

## What does not apply

| Project | Why |
|---|---|
| `TheSerioliOfNosgoth/ModelEx` | Soul Reaver 1/2, Defiance, Gex 3, Tomb Raider; not Blood Omen 2 |
| `TheSerioliOfNosgoth/OnlySkinDeep-Official` | Soul Reaver 1/2 textures (`.vrm`/`.srm`), not Blood Omen 2 |
| `TheSerioliOfNosgoth/SoulSpiral-Official` | CDC/Gex `bigfile.dat`, not the BO2 `goefile` container |
| `Gh0stBlade/KAIN2` | PS1 Soul Reaver 1 decompilation; method reference only |

Blood Omen 2 uses the engine lineage of *Mad Dash Racing* (Crystal
Dynamics), not the Soul Reaver engine. The platform layers share
Direct3D 8, DirectInput, Bink and FMOD; the game data does not.

## Verified facts and corrections

- **`pc.rkv` is the audio archive, not the master container.** It holds Ogg
  Vorbis streams under `vo/`, `streams/wav/` and `music/wav/`. The level,
  model, texture and entity data lives in `.big` files with a `goefile`
  header. This corrects an earlier assumption in `docs/FORMATS/dat.md`.
- **`0x1a000` is not a format constant.** It is
  `index_size + 8 + 0x800` for one build with `index_size = 0x18000`.
  The parser computes the start and can validate it against the first
  `OggS` signature.
- **Textures are DDS.** The `texture` chunks carry a `DDS ` header and can
  be handed to a standard DDS loader. The font is an uncompressed
  A8R8G8B8 256x256 DDS plus `texfont` glyph records.
- **Audio on PC is Ogg Vorbis.** PS2 uses Sony ADPCM; GameCube uses DSP.
  vgmstream handles the console variants only.
- **The engine uses FMOD**, Direct3D 8, DirectInput 7 and Bink. This is
  consistent with the imports we measured.
- **Entities are identified by name**, not by a global numeric ID:
  `tunedata` files are organized per level and per behavior.
- **The `.san` files are not the only animation path.** `animatio` and
  `animreel` blocks also live inside `.big` files.
- **No public work decodes the installer `data/*.dat` chunks.** That gate
  remains ours.

## Integration actions

1. `tools/bo2fmt/rkv.py`: parser for the audio archive, synthetic tests.
2. `tools/bo2fmt/big.py`: parser for the `goefile` container, synthetic
   tests; validate against a real `.big` before calling it decoded.
3. `docs/FORMATS/big.md` and `docs/FORMATS/rkv.md`: format specs.
4. Texture and font work: extract one DDS from a real `.big` once an
   installed tree is available.
5. Node-to-subsystem triage of the Ghidra inventory using the resource
   taxonomy (`model`, `texture`, `animatio`, `animreel`, `cellgrup`,
   `region`, `planinfo`, `bpackage`, `tunedata`, `litedata`, `lipsync`,
   `sound`, `strmfile`, `effect`, `sample`, `symlist`, `goefile`).
6. Use the GOG symbol addresses from `blood-omen-2-pl` for cross-build
   bindiffs, not as absolute addresses for our Spanish GoldMaster.

## Legal notes

- MIT code may be incorporated with attribution. GPL code is oracle only.
  Unlicensed code and tools are citation only; format facts are not
  expressive content, but code, comments and text are.
- No game assets, textures, fonts, audio, text, binaries or decompiled
  dumps are published in this repository.
- The project is unaffiliated with Crystal Dynamics, Eidos Interactive or
  Square Enix. Names are used nominatively.
