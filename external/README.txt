external/ - your own game files live here (gitignored)

Nothing in this directory is tracked by git except this README.

Expected content
----------------
Target build: Blood Omen 2, PC, version 1.02, Spanish GoldMaster.
The game is a 32-bit Windows release (Direct3D 8, DirectInput, Bink).

A materialized installation is expected to contain at least:

    data/game.erg
    data/*.dat            (asset containers)
    *.dat                 (Bink videos: awakens, intro, credits, ...)
    bo2.exe

Reference material
------------------
The installer ISO, the extracted installation and the Ghidra project may
also live here. They are never committed. See docs/PROVENANCE.md for the
hashes of the reference copy.

The host build reads game data with a --data-dir flag. The Switch build
reads it from the SD card path documented in docs/GAME_DATA.md.
