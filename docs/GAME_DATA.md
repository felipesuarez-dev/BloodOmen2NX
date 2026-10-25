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
  data/
    game.erg              text configuration
    pc.rkv                audio archive (Ogg Vorbis)
    Code.big, PreCode.big code, font and shared resources
    resources/levels/...  level .big files
    kain*.big             Kain models and textures per chapter
    *.dat                 Bink videos
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

- Host build: `--data-dir <dir>` (planned; the current smoke binary does not
  read assets).
- Tests: `BO2NX_ASSETS_DIR=<dir>`. Asset-gated tests skip cleanly when it
  is unset.

## Reference media

The hashes of the reference copy and the provenance of every external
artifact are in `docs/PROVENANCE.md`. Nothing is downloaded by the build.

## Installing from the ISO

The reference ISO is an installer, not an installed game. The installer
uses a custom encrypted archive; the fast path is to install the game on
Windows (or use a GOG/Steam copy) and copy the installed tree to
`external/installed/`. The ISO remains the provenance reference. See
`docs/FORMATS/dat.md`.
