# tools/gen

Derived data and generators.

| File | Purpose |
|---|---|
| `progress.py` | Rewrite the README status block from `re/FUNCTION_MAP.csv` and `progress_data.py` |
| `progress_data.py` | Level list and platform feature states |

```sh
python3 tools/gen/progress.py
python3 tools/gen/progress.py --check
```

`--check` exits non-zero when the README block is stale. CI runs it.

## Function map rules

Statuses and the evidence each one needs are written at the top of
`progress_data.py`; `tests/python/test_progress.py` enforces the mechanical
parts (known status, port file named in `notes`, function cited in the repo).

### Library code

`external` rows are not game code. Two kinds exist:

| Kind | Rows | Evidence |
|---|---:|---|
| Compiler unwind funclets (`Unwind@<address>`) and the CRT/MFC/import symbols Ghidra named | 17416 | Ghidra name; seeded by `seed_function_map.py` |
| Statically linked library code in `0x007a30cc`-`0x007f3c4f` (subsystem `lib`) | 1186 | The range holds 322 string references (237 distinct strings) and every one belongs to a library: MSVC CRT and undecorator (`typeid`, `__MSVCRT_HEAP_SELECT`, `` `template-parameter ``), D3DX8 (`D3DXERR_*`, the shader assembler), zlib and libpng. The code before it (`0x007a2643`, a sound script error) and after it (`0x007f3de5`, the Spanish error dialog) is game code. The first named CRT function is `__global_unwind2` at `0x007a30cc`; the last named import is `DirectInputCreateEx` at `0x007f3c50` |

To re-check: list the string cross-references with `ListStrings.java`
(`re/decomp/bo2_strings/strings.tsv`, local) and confirm that none inside the
range is a game string. Library code outside that range (STL or MFC inlined
into game objects, other CRT pieces) is still counted as `todo`.
