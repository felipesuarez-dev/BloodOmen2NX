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
