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

`unpack` writes `manifest.json` with the BIG entry metadata (type, name,
language, symbols and padding). `repack` rebuilds the container from that
manifest and is byte-identical when the extracted data is unchanged.
`verify` performs the full unpack/repack cycle and fails unless the bytes
match. RKV containers can be listed and unpacked; they are not repacked.

## Installer extraction

The 198 numbered installer chunks are decoded. Materialize the reference
tree from the ISO, the SFXI resources and the chunks:

```sh
python3 tools/bo2fmt/installer.py \
  --script external/installer/sfxi/script.txt \
  --installer external/iso/install2.dat \
  --data external/iso/data \
  --sfxi external/installer/sfxi \
  --iso external/iso \
  --out external/installed \
  --unrar /path/to/unrar
```

The output path can also be the first positional argument. `--dry-run`
prints the plan, `--only <name>` extracts one entry, and
`--skip-existing` resumes.

Requirements:

- `unrar` (the pipeline rebuilds a standard RAR with the password flag
  cleared and streams `unrar p` into the target).
- A C compiler for `bo2cipher.c`. Without one, `--cipher auto` falls back
  to the slow Python reference; `--cipher native` requires the helper.
- The user's own `install2.dat`: the S-box and password are read from it
  at the offsets documented in `docs/FORMATS/dat.md`. They are not stored
  in the repository.

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
| `installer.py` | Installer `data/*.dat` chunks | Verified: 198/198 chunks, 227-file tree |
| `bo2cipher.c` | Block cipher accelerator | Verified against the Python reference |
| `big.py` | `.big` (`goefile`) content container, locale-aware lookup (`find_localized`) | Verified: 179 files, 246,332 entries |
| `rkv.py` | `pc.rkv` audio archive | Verified: 2,666 Ogg streams |
| `bo2fmt.py` | Umbrella list/unpack/repack CLI | BIG and RKV list/unpack; BIG repack verified byte-identical |
| `audio.py` | `sound` metadata and `strmfile` extraction | Verified on the installed tree; field semantics open |
| `model.py` | model, skeleton and raw animation extraction | Verified: 5,168 models, 1,586 skinned |
| `region.py` | entity table extraction | Verified: 15,632 entities, 533 models, 0 errors |
| `textures.py` | `texture` entries to PNG (Pillow) with `index.json` | Verified on `slums/sl01`: 239/239 |
| `strtable.py` | `strtable` reader: `dump <container.big> --entry <name> [--locale spanish] [--raw]` | Verified: 92/92 tables parse, 1 irregular record (Italian `ui/UI`); accent mapping inferred |
| `animreel.py` | `animreel` clip reader: `info <entry>`, `root <container.big> [--match text]` | Verified: 134/134 Kain clips parse; root speed of `kain/run` is 200 u/s |
| `compare_frames.py` | Frame side-by-side, diff and metrics | Synthetic tests; used against the Windows and Wine oracle frames |

The numbered `data/*.dat` pipeline is documented in
`docs/FORMATS/dat.md`. The content and audio containers are documented in
`docs/FORMATS/big.md` and `docs/FORMATS/rkv.md`.
