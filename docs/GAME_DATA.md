# Game data

The port does not ship game data. You need your own copy of *Blood Omen 2*
for PC, version 1.02 Spanish GoldMaster.

## What the port needs

The port reads an **installed** tree, not the ISO. The ISO is an
installer whose numbered chunks are encrypted; see `docs/FORMATS/dat.md`.
The expected installed layout is:

```
<data-dir>/
  bo2.exe                 (reference only; never executed by the port)
  binkw32.dll
  data/
    game.erg              text configuration
    *.dat                 Bink videos (copy entries)
    list.txt, *.raw, *.fnt, *.dds, *.react, dsstdfx.bin
  media/gogames/bo2/
    pc.rkv                audio archive (Ogg Vorbis)
    Code.big, PreCode.big code, font and shared resources
    resources/levels/...  level .big files
    resources/animations/levels/*.san
```

`pc.rkv` is audio, not the master asset file. Level, model, texture and
entity data lives in the `.big` files (`docs/FORMATS/big.md`).

## Where it lives

- Local development: `external/installed/` (gitignored).
- The Ghidra project and the extracted installer: `external/` (gitignored).
- The reference ISO: outside the repository or under `external/`
  (gitignored).
- On the Switch: the SD card path is fixed when the platform backend
  lands. It will be documented here and in `docs/RUNNING.md`.

## How the build finds it

- Host build: `--big <path>` for `--list-big` and `--frame` (headless
  preview). `--data-dir` is planned with the level pipeline.
- Tests: `BO2NX_ASSETS_DIR=<dir>`. Asset-gated tests skip cleanly when it
  is unset.

## Reference media

The hashes of the reference copy and the provenance of every external
artifact are in `docs/PROVENANCE.md`. Nothing is downloaded by the build.

## Installing the game

The reference ISO is an installer, not an installed game. Install it with
the official installer, on Windows or under Wine (`wine setup.exe` in a
32-bit prefix), and use the installed directory. The repository carries no
tool to extract the installer media ([`FORMATS/dat.md`](FORMATS/dat.md)).
