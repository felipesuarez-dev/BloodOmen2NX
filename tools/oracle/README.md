# Oracle tools

## Wine oracle

The pristine `bo2.exe` runs under Wine with wined3d on Xvfb; no patch is
needed. Setup, route grammar, parity numbers and limits are in
`docs/ORACLE_WINE.md`.

| File | Purpose |
|---|---|
| `scripts/oracle/wine_setup.sh` | Build `external/oracle_install/`, create the prefix, import the registry values, seed saves |
| `wine_capture.py` | One scripted capture: Xvfb, game, route, inputs, screenshots, PPM frames, video |
| `routes/sl01_basic.txt` | Load PARTIDA 2 and walk, run, jump and attack (Windows phase names) |

```sh
scripts/oracle/wine_setup.sh external/captures/capture_20261028_0150
python3 tools/oracle/wine_capture.py tools/oracle/routes/sl01_basic.txt
python3 tools/oracle/validate_capture.py external/captures/wine_YYYYMMDD_HHMM
```

A Wine capture has `"source": "wine"` in `game_manifest.json`. It has no
`camera_d3d8.jsonl` or `state.csv`; `validate_capture.py` does not require
them for that source and `align.py` reports inputs and phases by time offset.

## Native Windows oracle (2026-10-27)

A native Windows harness complements the Wine oracle.
It runs `bo2.exe` on the Windows host (no Wine) in an 800x600 window and
serves captures through an SSH reverse tunnel.

`oracle_client.py` talks to the server. `validate_capture.py` checks the
required artifacts, the `bo2.exe` hash and `SHA256SUMS` (Windows and Wine
captures). `align.py` maps the
input events to the captured frames and summarizes the present rate.

`capture_20261028_0150` is the first complete capture (`sl01` spawn,
walk, run, jump; `validate_capture.py` passes). It has no camera pose or
player state because `VIEW` is the identity in gameplay. See `docs/ORACLE_WINDOWS.md` for the capture
facts and `docs/ORACLE_WINDOWS_NATIVE.md` for the harness.

Quick check from this host:

```sh
python3 tools/oracle/oracle_client.py health
python3 tools/oracle/oracle_client.py list
curl -s http://127.0.0.1:8765/health
```
