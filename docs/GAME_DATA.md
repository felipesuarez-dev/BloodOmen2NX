# Game data

The port does not ship game data. You need your own copy of *Blood Omen 2*
for PC, version 1.02 Spanish GoldMaster.

## What the port needs

The final asset requirements will be confirmed in M1. The first expected
layout is:

```
<data-dir>/
  bo2.exe                 (reference only; never executed by the port)
  data/
    game.erg
    <asset containers>
  <video>.dat             (Bink videos)
```

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

The reference ISO is an installer, not an installed game. M0 extracts
`setup.exe` and `install2.dat` statically instead of running the Windows
installer. The resulting layout is recorded in `docs/PROVENANCE.md` and in
`docs/FORMATS/`.
