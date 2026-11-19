# tools/bo2fmt

Python reference implementation of the Blood Omen 2 containers and asset
codecs. It is the place where a format is proven before it is ported to
C++ in `src/engine`.

Umbrella commands:

```sh
python3 tools/bo2fmt/bo2fmt.py list <container>
python3 tools/bo2fmt/bo2fmt.py unpack <container> --out <dir>
python3 tools/bo2fmt/bo2fmt.py repack <dir> --out <container>
python3 tools/bo2fmt/bo2fmt.py info <file>
python3 tools/bo2fmt/bo2fmt.py verify <container.big>
```

Texture inspection (needs Pillow, `python3-pil`; write under `external/`):

```sh
python3 tools/bo2fmt/textures.py list <level.big> [--match text]
python3 tools/bo2fmt/textures.py export <level.big> --out external/extracted/<level> [--match text]
```

Animation clips (`animreel`, see `docs/FORMATS/animreel.md`):

```sh
python3 tools/bo2fmt/animreel.py info <extracted animreel entry>
python3 tools/bo2fmt/animreel.py root <container.big> [--match text]
```

String tables (`strtable`, see `docs/FORMATS/strtable.md`). The entry is picked by
locale like the engine does (Spanish by default, the `default` copy as fallback):

```sh
python3 tools/bo2fmt/strtable.py dump <container.big> --entry ui/UI [--locale spanish] [--raw]
```

The output is `key<TAB>text`, in UTF-8. `--raw` prints the stored text, where
upper-case letters stand for accented glyphs; without it the Spanish accent
mapping (inferred, see the format doc) is applied. The output contains the
original game text: keep it out of the repository.

Animation graphs (`animatio`, see `docs/FORMATS/animatio.md`):

```sh
python3 tools/bo2fmt/animatio.py nodes --big <container.big> --entry <name> [--match text]
python3 tools/bo2fmt/animatio.py states --big <container.big> --entry <name> [--match text]
python3 tools/bo2fmt/animatio.py nodes <extracted animatio entry>
```

Script packages (`bpackage`, see `docs/FORMATS/bpackage.md`):

```sh
python3 tools/bo2fmt/bpackage.py list <container.big>
python3 tools/bo2fmt/bpackage.py classes <container.big> <package>
python3 tools/bo2fmt/bpackage.py region-classes <container.big> <region>
python3 tools/bo2fmt/bpackage.py disasm <container.big> <package> <class> [method]
python3 tools/bo2fmt/bpackage.py stats <directory>
python3 tools/bo2fmt/bpackage.py natives <container.big> [--package native]
```

`list` prints one line per package (name, version, imports, classes, methods,
bodies, native bodies and code words) and one line per region with its
`packages` section, prefixed `region`; `classes` prints one line per class of
a package (name, kind, parent, data size, slots, states, fields, methods and
bodies) and `region-classes` does the same for the package of a region.
`disasm` prints the instructions of the bodies of a class, with the operands
taken from the reference list; `stats` counts the instructions of every
container under a directory, in total and per code. `natives` prints the
native interface of a package, one line per native body, with its state when
it is not `default` and `[thread]` when the method runs as a thread. Its
output is game data: do not store it in the repository.

`unpack` writes `manifest.json` with the BIG entry metadata (type, name,
language, symbols and padding). `repack` rebuilds the container from that
manifest and is byte-identical when the extracted data is unchanged.
`verify` performs the full unpack/repack cycle and fails unless the bytes
match. RKV containers can be listed and unpacked; they are not repacked.

## Installer media

The repository has no tool to extract the installer: install the game with
its official installer (on Windows or under Wine) and point the tools at
the installed tree. `sfxi.py` still lists the SFXI resource container of
`install2.dat` (`docs/FORMATS/sfxi.md`).

## Rules

- Validate against synthetic fixtures first; the tests must not need the
  game.
- Real assets are read from `external/` and never copied into the repo.
- A format is only "decoded" when it is validated against the real build.
- No original strings or asset content in source, fixtures or docs.

## Status

| Module | Target | State |
|---|---|---|
| `sfxi.py` | Installer SFXI container | Verified against the reference installer |
| `big.py` | `.big` (`goefile`) content container, locale-aware lookup (`find_localized`) | Verified: 179 files, 246,332 entries |
| `rkv.py` | `pc.rkv` audio archive | Verified: 2,666 Ogg streams |
| `bo2fmt.py` | Umbrella list/unpack/repack CLI | BIG and RKV list/unpack; BIG repack verified byte-identical on the 179 containers with `scripts/verify_data.sh` |
| `audio.py` | `sound` metadata and `strmfile` extraction | Verified on the installed tree; field semantics open |
| `model.py` | model, skeleton and raw animation extraction | Verified: 5,168 models, 1,586 skinned |
| `region.py` | entity table extraction | Verified: 15,632 entities, 533 models, 0 errors |
| `cellgroup.py` | `cellgrup` reader: `models`, `attributes` (models with their chunks, marker, priority, materials and textures; the attributes of each model) | Verified: the 42 models and markers of `ui/hud`; 664 model blocks of `slums/sl01` |
| `textures.py` | `texture` entries to PNG (Pillow; DXT2 and DXT4 are read as DXT3 and DXT5) with `index.json` | Verified on `slums/sl01`: 239/239 and `code.big`: 66/66 |
| `strtable.py` | `strtable` reader: `dump <container.big> --entry <name> [--locale spanish] [--raw]` | Verified: 92/92 tables parse, 1 irregular record (Italian `ui/UI`); full accent table from the font art |
| `font.py` | `texfont` reader: `dump <container.big> [--entry main/main]` | Verified: `main/main` of `code.big`, 87 glyphs |
| `animreel.py` | `animreel` clip reader: `info <entry>`, `root <container.big> [--match text]` | Verified: 134/134 Kain clips parse; root speed of `kain/run` is 200 u/s |
| `animatio.py` | `animatio` graph reader: `nodes`, `states` (`--big <container.big> --entry <name>` or an extracted entry) | Verified: all 4,081 `animatio` entries of the installed tree parse to the last byte (2,156 graphs, 1,925 alias lists) |
| `bpackage.py` | `bpackage` script package reader: `list`, `classes`, `region-classes`, `disasm`, `stats`, `natives` | Verified: 480/480 packages and 165/165 region packages parse to the last byte |
| `compare_frames.py` | Frame side-by-side, diff and metrics | Synthetic tests; used against the Windows and Wine oracle frames |

The numbered `data/*.dat` pipeline is documented in
`docs/FORMATS/dat.md`. The content and audio containers are documented in
`docs/FORMATS/big.md` and `docs/FORMATS/rkv.md`.
