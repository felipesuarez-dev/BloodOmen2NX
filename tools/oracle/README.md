# Oracle tools

`patch_bo2_oracle.py` builds a derived copy of `bo2.exe` for the Wine oracle:
it guards the null 2D display object, replaces 3DNow `femms` with `emms`, and
(while diagnosing) skips the device-init `int3`. It never modifies the input
executable.

The current blocker is documented in `docs/ORACLE_COMPARE.md`: Wine reaches
the video configuration dialog, then display/device initialization fails.
