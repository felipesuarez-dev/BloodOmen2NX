# Installer chunks (`data/*.dat`)

Status: **not read by the port**. The numbered `data/*.dat` files of the
installation media are the packed form of the game data: the official
installer (`setup.exe`, then `install2.dat`) expands them into the installed
tree. The port only reads an installed copy of the game
([`../GAME_DATA.md`](../GAME_DATA.md)); it never opens these chunks.

## Role

The installer script (`install2.dat` -> SFXI -> `script.txt`, see
[`sfxi.md`](sfxi.md)) maps 198 chunks to target paths and copies 29 other
files unchanged:

| Install type | Files | Targets |
|---|---:|---|
| `decompress` | 198 | 179 `.big`, 18 `.san`, 1 `pc.rkv` |
| `copy` | 29 | `bo2.exe`, `binkw32.dll`, `game.erg`, `list.txt`, UI raw images, fonts, Bink movies |

Each chunk is a single-file archive protected by the installer's own
encryption. `pc.rkv` is the audio archive; the level, model, texture and
entity data lives in the `.big` files.

## History

During M0 and M1 the project decoded these chunks with its own tool and
rebuilt the installed tree from the reference media (198/198 chunks, 227
files, 1,968,053,271 bytes; the manifest hash is in
[`../PROVENANCE.md`](../PROVENANCE.md)). That tool and the description of
the installer's encryption were removed from the repository on 2026-11-15:
the encryption is part of the installer, not of the game, and the port does
not need it. Use the official installer, on Windows or under Wine, to get
the installed tree.
