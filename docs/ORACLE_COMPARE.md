# Oracle comparison

The oracle is the original PC build running under Wine. It is used to prove
behaviour, not to run the game for fun.

## Method

1. Capture a reference moment from the original: screenshot, state dump,
   audio sample or save file.
2. Reproduce the same moment in the port with scripted input and a fixed
   tick.
3. Compare. For visuals, put both frames side by side; for state, compare
   canonical hashes; for saves, compare the byte layout.

## Status

**The pristine `bo2.exe` runs under Wine, reaches the main menu and
gameplay, and scripted captures work.** The setup, the capture tool, the
route grammar and the measured parity are in
[`ORACLE_WINE.md`](ORACLE_WINE.md). The spawn frame differs from the
Windows frame by a mean absolute error of 4.9/255 (RMSE 8.6); the main
menu by 0.57/255.

The Wine diagnosis of 2026-10-25 and 2026-10-26 was wrong and is
withdrawn:

| Earlier claim | What is true |
|---|---|
| The video dialog's resolution list is empty under Wine | The list is filled. `ModeID` in `HKCU\Software\Crystal Dynamics\Blood Omen 2\1.00.000` pointed out of range, so nothing was selected |
| Accepting the dialog crashes at `0x56E2C9` (null `param_1[2]` in `FUN_0056E2B0`) and the device init fails in `FUN_0055F480`; a patched exe is needed | The pristine exe does not crash once the registry and the install layout are set. The cause of the old crash was not isolated |
| 3DNow `femms` raises `EXCEPTION_ILLEGAL_INSTRUCTION`, so it must be replaced | The exception at `0x6D327B` is the game's own 3DNow CPU probe, and the game handles it |
| d8vk or a virtual desktop is needed | The builtin wined3d is used (`d3d8=b`); no virtual desktop |

Those runs lacked `InstallPath` and `MoviePath` (`FUN_00578b80` reads them
from `HKLM`, and without them the game opens `\data\game.erg` at the drive
root and loads nothing), used a layout that did not mirror the Windows
install, and a `ModeID` that selected no mode. The failures that followed
were read as Wine or D3D8 limits, and each workaround treated a symptom.
`tools/oracle/patch_bo2_oracle.py` was removed.

## Frame comparator

`tools/compare_frames.py` takes two binary PPM frames, writes a
side-by-side image, an amplified difference image and a JSON report, and
reports whole-frame, N x N tile and named-region metrics. The tile and
region reports isolate the world, UI and lighting layers for review.

```sh
python3 tools/compare_frames.py port.ppm oracle.ppm --out /tmp/gate \
  --tiles 4 --region ui:0,320,640,160 --region world:0,0,640,320 --diff-gain 4
```

It writes `/tmp/gate-side-by-side.ppm`, `/tmp/gate-diff.ppm` and
`/tmp/gate-metrics.json`. The script is tested with synthetic frames and has
been run on the Wine and Windows spawn frames (`ORACLE_WINE.md`).

## Port divergences

| Area | Divergence | Evidence | Next action |
|---|---|---|---|
| `sl01` spawn frame | Magenta tint over the whole view | A `pyramid/purple` box (flat `255,0,255,128`) encloses the camera; replacing the texture with green turns the tint green (`docs/FORMATS/textures.md`) | Decompile how `bo2.exe` excludes `pyramid/*` helper volumes from rendering (BO2NX-68) |
| `sl01` materials | `--material diffuse` and `layers` render near-black | Port render at the spawn | Decompile the material/light-map combination for level models (BO2NX-68) |

## Rules

- A screenshot comparison is a gate, not a demo. Do not show a level until
  the frame recognizably matches the original.
- Keep oracle captures under `external/captures/` (gitignored). Only derived
  facts (hashes, dimensions, counts) are committed.
- Record every divergence with a `FUN_` reference and a next action.

## Native Windows oracle (2026-10-27)

A native Windows harness is available as a complement to the Wine oracle.
It runs `bo2.exe` on the Windows host (no Wine) in an 800x600 window and
serves captures through an SSH reverse tunnel.

`capture_20261028_0150` is complete: it loads the `sl01` checkpoint
save, records walk, run and jump with video, `state.csv` and PPM key frames,
and passes `validate_capture.py`. `VIEW` is the identity in gameplay, so it
has no camera pose or player state. See
`docs/ORACLE_WINDOWS_NATIVE.md` for the harness,
`docs/ORACLE_WINDOWS.md` for the capture facts, and
`tools/oracle/oracle_client.py`, `tools/oracle/validate_capture.py` and
`tools/oracle/align.py` for the tooling.

Quick check from this host:

```sh
python3 tools/oracle/oracle_client.py health
python3 tools/oracle/oracle_client.py list
```
