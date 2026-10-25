# tools/bo2fmt

Python reference implementation of the Blood Omen 2 containers and asset
codecs. It is the place where a format is proven before it is ported to
C++ in `src/engine`.

Planned commands:

```sh
python3 tools/bo2fmt/bo2fmt.py list <container>
python3 tools/bo2fmt/bo2fmt.py unpack <container> --out <dir>
python3 tools/bo2fmt/bo2fmt.py repack <dir> --out <container>
python3 tools/bo2fmt/bo2fmt.py info <file>
```

## Rules

- Validate against synthetic fixtures first; the tests must not need the
  game.
- Real assets are read from `external/` and never copied into the repo.
- A format is only "decoded" when a re-packed file is accepted by the
  original game.
- No original strings or asset content in source, fixtures or docs.

## Status

Implemented:

| Module | Target | State |
|---|---|---|
| `sfxi.py` | Installer SFXI container | Verified against the reference installer |
| `big.py` | `.big` (`goefile`) content container | Spec-level, synthetic tests |
| `rkv.py` | `pc.rkv` audio archive | Spec-level, synthetic tests |

The numbered `data/*.dat` chunks are a custom encrypted installer
archive; the extractor is not ported. Use an installed tree for real
assets.
