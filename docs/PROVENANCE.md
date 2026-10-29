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

Materialized with `tools/bo2fmt/installer.py` from the reference ISO, the
SFXI resources of `install2.dat` and the 198 `data/*.dat` chunks. The
local copy is `external/installed/` (gitignored).

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

## Chain of custody

1. Keep the reference ISO and the extracted files under `external/`.
2. Record hashes in this file before first use.
3. Run `scripts/check_no_assets.sh` before every commit.
4. Never commit game data, original code, decompiled dumps or verbatim
   game text.
