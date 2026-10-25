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

- Host build: `--data-dir <dir>` (planned; the current smoke binary does not
  read assets).
- Tests: `BO2NX_ASSETS_DIR=<dir>`. Asset-gated tests skip cleanly when it
  is unset.

## Reference media

The hashes of the reference copy and the provenance of every external
artifact are in `docs/PROVENANCE.md`. Nothing is downloaded by the build.

## Installing from the ISO

The reference ISO is an installer, not an installed game. The chunks are
decoded; `tools/bo2fmt/installer.py` materializes the tree from the ISO,
the SFXI resources and the 198 `data/*.dat` files. See
`docs/FORMATS/dat.md` for the pipeline and the `--data-dir` layout.

A Windows or GOG/Steam installation remains a valid oracle, but it is no
longer required to obtain the tree.
