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

## Rules

- A screenshot comparison is a gate, not a demo. Do not show a level until
  the frame recognizably matches the original.
- Keep oracle captures under `external/captures/` (gitignored). Only derived
  facts (hashes, dimensions, counts) are committed.
- Record every divergence with a `FUN_` reference and a next action.
