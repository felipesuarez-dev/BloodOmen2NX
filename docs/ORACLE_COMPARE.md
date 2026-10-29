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

**Reaches the video configuration dialog; crash after accepting.**
On 2026-10-25 the Spanish GoldMaster launches under Wine 9.0 with a
virtual desktop:

```sh
wine explorer /desktop=bo2,800x600 bo2.exe
```

The initial warning appears; after accepting it the video configuration
dialog appears (device `NVIDIA GeForce GTX 470`, 800x600, 16-bit,
60 Hz). Accepting that dialog reproduces the fault at `0x56e2c9` in
`FUN_0056e2b0` with a null `param_1[2]` (read of `0x1c`). Without the
virtual desktop the fault happens before the configuration dialog.
Disabling `PlayMovie`, `LogoMovie*` and `IntroMovie*` in a private copy
of `game.erg` does not change it. The D3D8 device is created and vertex
buffers are locked, so the crash is in the 2D/UI scaling path rather than
in device creation.

The game stores its video settings under
`HKCU\Software\Crystal Dynamics\Blood Omen 2\1.00.000`
(`ScreenWidth`, `ScreenHeight`, `ScreenDepth`, `Windowed`, `SkipMovies`,
`ProgramsWarnings`, `DisableVShader`, `DisableModulate2x`, ...). Setting
`ScreenDepth = 32`, `Windowed = 1`, `SkipMovies = 1` and
`ProgramsWarnings = 0` replaces the initial crash with the message
"Tu configuración gráfica ha cambiado" followed by the video
configuration dialog. The dialog's resolution list is empty under Wine,
so a mode cannot be selected; setting `Windowed = 0` and the original
`ScreenDepth = 23` showed one 16-bit mode and reproduced the crash after
accepting it.

The next step is to inspect the class behind the `0x86db78` vtable and
the display/UI initialization that fills `param_1[2]`, and to make Wine
enumerate a valid video mode (or run the game windowed with a mode that
does not need the 2D display object).

The install tree was mounted as `C:\gogames\bo2` in a private prefix,
with `bo2.exe`, `data/` and the `code.big`/`pc.rkv`/`resources` files at
the root. The oracle remains optional and time-boxed; see
`docs/RUNNING.md`.

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
`/tmp/gate-metrics.json`. The script is tested with synthetic frames; the
oracle frame is still blocked by the Wine crash described above.

## Rules

- A screenshot comparison is a gate, not a demo. Do not show a level until
  the frame recognizably matches the original.
- Keep oracle captures under `external/captures/` (gitignored). Only derived
  facts (hashes, dimensions, counts) are committed.
- Record every divergence with a `FUN_` reference and a next action.

## Wine unlock attempt (2026-10-26)

A patched copy (`tools/oracle/patch_bo2_oracle.py`, output kept under
`external/`) now gets past the original `0x56E2C9` crash:

- the 2D scale hook returns when the display object is null;
- all 152 `femms` (3DNow) instructions are replaced by `emms`, because Wine
  raises `EXCEPTION_ILLEGAL_INSTRUCTION` on `femms`;
- the device-init `int3` at `0x568763` is skipped for diagnosis.

The game reaches the warning and video-configuration dialogs. Accepting the
configuration then fails in `FUN_0055F480` (device/display initialization)
and the subsequent path hits the skipped assert / a null device. The
resolution list is empty under Wine; forcing mode index 0 does not fix
device creation. `d8vk-v1.0` was installed into the private prefix and
initializes Vulkan/llvmpipe, but the same device-init failure remains.

The working layout is `external/oracle_game/` (game files at the root,
`bo2.exe` patched copy); launch with `wine ./bo2.exe` from that directory so
argv[0] carries separators. Next steps: inspect `FUN_0055F480` return point
under `winedbg`/gdb, or provide a 32-bit D3D8 mode that Wine accepts
(possibly a newer Wine or a native d3d8 runtime).

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
