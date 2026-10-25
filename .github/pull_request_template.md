## Summary

What changes and why (one short paragraph).

## Evidence

- Original functions: `FUN_XXXXXXXX`
- Local decompile: `re/decomp/<addr>_<name>.c.txt` (not committed)

## Tests run

- [ ] `./scripts/ci.sh`
- [ ] `PP_ASSETS_DIR=... ctest --preset linux-debug` (if asset-gated tests are affected)

## Checklist

- [ ] One logical change; Conventional Commit in English.
- [ ] One author; no co-author, signed-off or generated-by trailers.
- [ ] No game assets, original binaries or copyrighted strings committed.
- [ ] `re/FUNCTION_MAP.csv` updated for every translated function.
- [ ] `clang-format` applied to every touched C/C++ file; no warnings.
- [ ] README and the technical docs updated in the same change.
