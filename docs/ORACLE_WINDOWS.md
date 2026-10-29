# Native Windows oracle capture

This report records the derived facts of the native Windows captures
that reached gameplay. The capture itself lives under `external/captures/`
and is gitignored; no screenshots, video, saves or game assets are committed
here.

## Capture `capture_20261028_0150` (complete)

`validate_capture.py` accepts this capture in final mode (`ok: True`). It
is the reference capture for the first level.

| Field | Value |
|---|---|
| Host | Windows 11 25H2, AMD RX Vega 11, native `bo2.exe`, no Wine |
| `bo2.exe` SHA-256 | `91758398a17de9c9f974a8a102762a534fb523d40fa6868a5e5a4f0483b2753c` (matches the reference; patch 1.02 not applied) |
| Start | Frida spawn, then load of the manual save `slums/sl01.hidden_checkpoint001` (`saves/Save 01/game.bo2`) |
| Video | 800x600, `video.mkv` (32,155,856 bytes), ffmpeg gdigrab |
| Inputs | 72 events; 53 aligned to a present, 19 outside the camera coverage |
| Camera events | 23,994 presents over 70.5 s; 228,947 transforms |
| Present rate | 214.86 fps mean, 333.33 fps median, p95 10 ms (uncapped) |
| `state.csv` | 10,233 rows, frames 13,762 to 23,994 |
| `frames_ppm/` | 7 frames: spawn, after W, during and after jump, after attack, camera |
| Screenshots | 20 PNG, including `contact_sheet.png` |
| Saves | `Save 00` to `Save 05` (460 bytes each) and `settings.cfg` |
| `SHA256SUMS` | 157 entries, verified |

Phase rows in `state.csv`:

| Phase | Rows | First frame |
|---|---:|---:|
| `spawn_idle` | 4,673 | 13,762 |
| `after_w` | 395 | 18,435 |
| `after_s` | 390 | 18,830 |
| `after_a` | 393 | 19,220 |
| `after_d` | 689 | 19,613 |
| `run` | 1,723 | 20,302 |
| `jump` | 1,583 | 22,025 |
| `attack` | 387 | 23,608 |

### What the capture proves

| Action | Status | Evidence |
|---|---|---|
| Load from save to the `sl01` spawn | Confirmed | `01_spawn` frame |
| Walk W/S/A/D, run with `Shift+W` | Confirmed | Video and screenshots |
| Jump (right mouse) | Confirmed | Video: Kain leaves the ground and lands; `04a_during_jump` is taken from the video at the input timestamp |
| Attack (left mouse) | Not confirmed | The DirectInput hook sees the click, but the video shows no attack animation |
| Camera rotation (`Right Ctrl+Left`) | Not confirmed | Opens the Dark Gift overlay instead |
| Save menu | Confirmed | `08_save_menu`, `09_save_attempt` |

### Limits

- `SetTransform(D3DTS_VIEW)` is the identity in all 10,233 gameplay rows.
  The engine does not pass the camera through `VIEW`, so `cam_*` columns
  are empty. `PROJECTION` is valid. The camera pose has to come from engine
  memory, not from D3D8 transforms.
- Player position, velocity, health, mana, weapon, animation and the logic
  tick are not captured. No memory offsets are assumed.
- The present rate is uncapped (about 250 fps in gameplay according to the
  capture notes). It is not the logic tick.
- The input log starts before the first present and continues 5.4 s after
  the last one (the save phase).
- The exported registry has `Windowed = 0`, `ScreenDepth = 0x17`,
  `VSync = 1`, `SkipMovies = 1`, `DisableVShader = 1` and
  `ProgramsWarnings = 0`. The capture notes record `Windowed = 1` and an
  800x600 client; the D3D8 device reports `Windowed = 0`.

## Earlier partial capture `capture_20261028_0047`

The first capture that reached gameplay. It started a new game, reached
`slums/sl01` after 63.6 s and saved a checkpoint. Its camera hook stopped
11.6 s before the spawn, and it has no manifest, notes, hashes,
`state.csv` or `frames_ppm/`. It is superseded by `_0150`.

## Port comparison attempt

The port loads `slums/sl01` with `--level`:

| Metric | Value |
|---|---:|
| Models | 608 (56 skipped) |
| Triangles | 49,624 |
| Textures | 213 / 225 |
| Light maps | 94 / 225 |
| Instances | 650 (599 placed, 51 skipped) |
| Lights | 103 |
| Markers | 48 |
| Region entities | 104 |

The spawn data from `region`:

| Entity | Position |
|---|---|
| `player` (`kainemitter`) | `[-7.27, 45.60, 218.19]` |
| `hidden_checkpoint001` | `[-8.80, 57.13, 208.00]` |

The first render attempts around the spawn are magenta or very dark and do
not show Kain. The oracle frame shows textured stone and the character. The
port's `--level` path does not bind the same material/texture set for this
level, and the gameplay camera pose is not available from the capture. The
spawn frame is therefore not comparable yet; tracked as BO2NX-68.

No camera or player state exists for the gameplay segment. The `VIEW` matrix
is the missing input for camera comparison; the save file is the only
gameplay-state artifact.

## Validation

```text
validate_capture.py external/captures/capture_20261028_0150:
  ok: True, mode final, res 800x600
  counts: inputs=72 state=10233 camera=252979 present=23994 ppm=7 png=20 saves=7
  warning: inputs start before the first camera present
  warning: inputs continue 5.4 s after the last camera present

align.py:
  23,994 present events over 70.544 s
  53 inputs aligned, 19 outside the camera coverage
  state: 10,233 rows, frames 13,762..23,994
```

## Next capture

1. Read the camera pose and the player state from engine memory (verified
   offsets only); `VIEW` is the identity.
2. Confirm the attack binding and the camera-rotation binding of this
   configuration before scripting them.
3. Record a logic-tick counter, not only the present rate.
