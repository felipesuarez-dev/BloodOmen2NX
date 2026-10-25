# tools/ghidra

Headless Ghidra helpers for `bo2.exe`. The project lives in
`external/ghidra` (gitignored) and is analysed as `x86:LE:32:default`.

Planned helpers, ported from the sibling decompilation project:

| Script | Purpose |
|---|---|
| `decompile.py` | Decompile functions by address (`--callees`, `--callers`) |
| `xrefs.py` | List references to an address |
| `make_functions.py` | Recover functions missed by auto-analysis |
| `export_db.py` | Export functions, calls and strings |
| `ExportFunctions.py` | Ghidra-side export |
| `ExportStrings.py` | Ghidra-side string export |

## Setup

```sh
export GHIDRA_INSTALL_DIR=~/tools/ghidra
python3 -m venv ~/.venvs/bo2
~/.venvs/bo2/bin/pip install --no-index \
  --find-links "$GHIDRA_INSTALL_DIR/Ghidra/Features/PyGhidra/pypkg/dist" pyghidra
```

## Policy

Decompiled output is evidence, not source. Save useful output under
`re/decomp/` (gitignored). Never paste decompiled functions, original
strings or game text into the repository.

## Status

The scripts land with M0, after Ghidra is installed and `bo2.exe` is
imported for the first time.
