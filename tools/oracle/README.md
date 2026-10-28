# Oracle tools

`patch_bo2_oracle.py` builds a derived copy of `bo2.exe` for the Wine oracle:
it guards the null 2D display object, replaces 3DNow `femms` with `emms`, and
(while diagnosing) skips the device-init `int3`. It never modifies the input
executable.

The current blocker is documented in `docs/ORACLE_COMPARE.md`: Wine reaches
the video configuration dialog, then display/device initialization fails.

## Native Windows oracle (2026-10-27)

A native Windows harness is available as a complement to the Wine oracle.
It runs `bo2.exe` on the Windows host (no Wine) in an 800x600 window and
serves captures through an SSH reverse tunnel.

`oracle_client.py` talks to the server. `validate_capture.py` checks the
required artifacts, the `bo2.exe` hash and `SHA256SUMS`. `align.py` maps the
input events to the captured frames and summarizes the present rate.

The first three captures are partial: the runner stops at the intro cutscene
with `RuntimeError: gameplay not detected`, and there is no gameplay state
yet. The final run will start from a manual save at the first level. See
`docs/ORACLE_WINDOWS_NATIVE.md`.

Quick check from this host:

```sh
python3 tools/oracle/oracle_client.py health
python3 tools/oracle/oracle_client.py list
curl -s http://127.0.0.1:8765/health
```
