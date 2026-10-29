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
| `sl01` spawn frame | Helper boxes drawn; a `pyramid/purple` box (flat `255,0,255,128`) enclosed the camera and tinted the view magenta | The original skips entities whose template has `flag helperobj`: `FUN_00785c8d` parses it, `FUN_0070d0d5` copies it to entity flag `0x200`, `FUN_0070f100` returns without drawing (`docs/FORMATS/models.md`). The port applies the flag; the magenta box is gone | Trace why `detector`, `planhelper*`, `conversation` and `bo2cinematic` models, which carry no `helperobj`, are not drawn either (the green `bo2cinematic001` diamond remains at the oracle pose) |
| `sl01` materials | `--material diffuse` and `layers` render near-black (mean RGB 0.016 against 0.108 in the Wine frame) | The layer colours are exporter fallbacks, not albedo: 1,015 of 1,075 used materials have a grey `c1` of 0.098 or 0.118, characters have all-zero layers, and the device runs with `D3DRS_LIGHTING = FALSE` (`docs/FORMATS/models.md`) | Decode the baked lighting: the group-to-model binding of the `.slm` vertex colours and light maps, fog and the per-level ambient values |
| `sl01` spawn frame | Whole-frame error 35.5/255 against the Wine frame, luma correlation 0.13 | Gate below | Lighting (above), the Kain model, the HUD and the types that hide through other paths |

## `sl01` spawn gate (BO2NX-68)

**Status: not met.** The helper-object rule removes the pyramid boxes, but
the frame is still far from the Wine frame; the lighting is not decoded.

Reference: `wine_20261029_0110`, `screenshots/01_spawn` (`frames_ppm/01_spawn.ppm`,
800x600). Camera: the `state.csv` row sampled at the screenshot
(`cam` = -199.751, 57.129, 304.069; yaw 0 looks along +X; pitch -6 degrees;
Kain at -8.803, 57.129, 208.0, so the camera is 190.9 behind and 96.1 above
him). The vertical field of view is not sampled; the port default of 55
degrees reproduces Kain's screen position (feet at y = 515 of 600 gives
55.6) and the layout of the level geometry.

```sh
bo2nx --level --big external/installed/media/gogames/bo2/resources/levels/slums/sl01.big \
  --material off --width 800 --height 600 \
  --eye "-199.751,57.129,304.069" --target "-198.7565,57.129,303.9645" --out port.ppm
python3 tools/compare_frames.py port.ppm wine_20261029_0110/frames_ppm/01_spawn.ppm \
  --out /tmp/gate --tiles 4 --region ui:0,0,290,175 --region kain:350,290,110,240
```

Definitions, all on 8-bit RGB frames of 800x600:

- **World** is every pixel except the HUD box (`x 0..289, y 0..174`) and a
  box around Kain (`x 350..459, y 290..529`), which the port does not draw:
  402,850 pixels.
- **World MAE** and **RMSE** are the mean absolute and root-mean-square
  difference between the port and Wine frames over the three channels of
  the world pixels, in 8-bit units.
- **Mean RGB** is the mean of the three channels over the world pixels,
  divided by 255. It is not photometric luminance.
- **Correlation** is Pearson's r over the world pixels of the Rec. 601 luma
  (`0.299 R + 0.587 G + 0.114 B`) of the port and Wine frames.
- **Gain fit** scales the port frame by `k = sum(Lp * Lw) / sum(Lp^2)`
  (luma of port and Wine) and reports the world MAE of `clamp(k * port)`
  against Wine.

These were computed with a throwaway numpy script; the whole-frame, tile and
box numbers below come from `tools/compare_frames.py`.

| Port frame | World MAE | World RMSE | Mean RGB (Wine 0.108) |
|---|---:|---:|---:|
| `develop` (25e537a), `--material off` | 35.40 | 40.06 | 0.241 |
| helper rule, `--material off` | 35.06 | 39.30 | 0.240 |
| `develop`, `--material off --lightmap` | 54.90 | 60.41 | 0.314 |
| helper rule, `--material off --lightmap` | 54.79 | 60.06 | 0.314 |
| light maps bound by instance name, `--material off --lightmap` | 55.10 | n/a | 0.322 |
| `develop`, `--material diffuse` | 23.47 | 26.27 | 0.016 |
| helper rule, `--material diffuse` | 23.39 | 26.19 | 0.016 |
| helper rule, `--material layers` | 23.38 | 26.18 | 0.016 |

Whole frame (`compare_frames.py`): MAE 35.50, RMSE 41.15 before and
MAE 35.55, RMSE 40.26 after. Tile MAE, 4x4 (rows top to bottom), before:

```
34.1 37.1 29.7 36.4
27.6 31.4 29.0 34.1
28.1 29.9 42.3 36.7
45.4 44.2 41.9 40.0
```

after:

```
33.8 37.3 29.9 33.4
28.9 31.6 29.0 34.3
27.2 31.9 38.6 36.5
45.9 47.5 42.9 40.0
```

How to read it:

- The helper rule changes the oracle-pose error by 0.34, because the boxes
  cover little of that view: the `kainemitter` and `hidden_checkpoint`
  columns beside Kain, a blue `regionkeeper` funnel and a purple `sfxobj`
  cube. The tile that held the columns improves from 42.3 to 38.6. The
  Kain region got worse (34.4 to 40.2): the lit floor now visible behind
  the removed column is brighter than the Wine frame, as is the whole port
  frame.
- With the camera inside the `kainemitter` box
  (`--eye -17.3,45.6,222.2 --target -7.27,45.6,219`, `--material off`,
  800x600, `sl01`) the tint is gone: whole-frame mean RGB 0.469, 0.100,
  0.457 (R/G 4.67) becomes 0.254, 0.255, 0.225 (R/G 1.00). The Wine frame
  has 0.118, 0.106, 0.104 (R/G 1.12) from another pose, so this only shows
  the tint, not parity.
- `--material diffuse` scores a lower MAE only because it is almost black;
  its correlation with the Wine frame is 0.015 (`off`: 0.13). Do not read
  it as closer.
- A single gain of 0.41 on the `off` frame brings the world MAE to 8.5 (8.4
  measured on luma only), which says the missing piece is the level of
  illumination and not geometry, but the correlation stays at 0.13: the lit
  and shadowed areas of the original are not reproduced.
- Binding the light maps by instance name, as the original does
  (`litedata.md`), raises the luma correlation of the `--lightmap` frame
  from 0.10 to 0.15, the best of the frames above, while its MAE stays
  about 55. The light-map shading of the host is still not the original
  equation. Stage 1 runs `COLOROP MODULATE` with the light map
  (`0x565bf3`). The `.slm` palette holds only the high byte
  (`0xLL000000`), so the texture format the original builds from it
  decides the colour, and that format is not traced. The `.slm` vertex
  colours and the fog are not applied either.
- Still different from the original: lighting, fog, the green
  `bo2cinematic001` diamond (no `helperobj` flag, see `models.md`), Kain
  and the HUD.

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
