# Native Windows oracle capture

This report records the derived facts of the first native Windows capture
that reached gameplay. The capture itself lives under `external/captures/`
and is gitignored; no screenshots, video, saves or game assets are committed
here.

## Capture

| Field | Value |
|---|---|
| Capture | `capture_20261028_0047` |
| Host | Windows 11, AMD RX Vega 11, native `bo2.exe`, no Wine |
| Video | 800x600, 60 fps, `video.mkv` (84,572,914 bytes) |
| Gameplay detected | 63.6 s after the new-game input |
| First level | `slums/sl01` from the save header |
| Screenshots | 11 PNG at 800x600, including `contact_sheet.png` |
| Save | `saves/Save 00/game.bo2`, 460 bytes, `slums/sl01.hidden_checkpoint001` |
| Inputs | 75 events |
| Camera events | 262,838 lines; 26,301 presents; 236,525 transforms |
| Present rate | 178.47 fps mean, 200 fps median, p95 10 ms |
| Camera coverage | 59.949 s, from 1793159271702 to 1793159331651 |
| `state.csv` | missing |
| `frames_ppm/` | missing |
| `game_manifest.json` | missing |
| `notes.md` | missing |
| `SHA256SUMS` | missing |

The capture is partial: it proves the game reaches the first level, but it
does not yet provide camera or player state for the gameplay segment.

## Screenshots

The contact sheet and the key frames were reviewed locally:

| Frame | Observation |
|---|---|
| `00_main_menu.png` | Main menu, Spanish, version 1.02 |
| `01_spawn.png` | Kain at the first-level spawn, third-person camera, health/mana HUD and tutorial |
| `02_after_w.png` | Kain moved forward; the camera followed |
| `03_after_run.png` | Checkpoint area, two distant actors, tutorial for the checkpoint |
| `04_after_jump.png` | Similar to `03`; the post-action frame does not show the jump |
| `05_after_attack.png` | Nearly identical to `04` (MAE 0.45); the attack is not visible |
| `06_after_camera.png` | Nearly identical to `05` (MAE 0.21); the camera rotation is not visible |
| `07_pause_menu.png` | Pause menu |
| `08_save_menu.png` | Save menu |
| `09_save_attempt.png` | Save confirmation state |

Consecutive mean absolute error, 0-255 RGB:

| Pair | MAE |
|---|---:|
| `01` vs `02` | 11.39 |
| `02` vs `03` | 15.02 |
| `03` vs `04` | 13.89 |
| `04` vs `05` | 0.45 |
| `05` vs `06` | 0.21 |
| `06` vs `07` | 17.49 |
| `07` vs `08` | 1.01 |
| `08` vs `09` | 1.01 |

The input log contains `W`, `S`, `A`, `D`, `Shift+W`, right mouse, left
mouse, mouse movement, `Escape` and `Enter`. The save file and the pause and
save menus confirm that the game received the keyboard input. The jump,
attack and camera steps are not confirmed by the post-action frames; they
need state or a frame taken during the action.

## Camera coverage gap

The camera hook stops before gameplay:

- Last camera present: `1793159331651`.
- `spawn` phase: `1793159343263`, 11.612 s after the last present.
- `01_spawn.png`: 16.773 s after the last present.
- Gameplay segment (`spawn` to `capture_end`): 24.819 s.
- Last captured input: 36.4 s after the last present.
- `align.py`: 8 of 75 inputs are inside the camera coverage; 67 are outside.

The two D3D8 devices are created at `1793159271694` and `1793159273397`,
both early in the run. The `present` and `transform` events do not carry the
device pointer, so the hook's device attribution is unknown. The hook must
survive the level load or device reset and cover the gameplay segment.

## Runtime configuration

The registry export is UTF-16 and records:

| Key | Value |
|---|---|
| `Windowed` | 0 |
| `ScreenWidth` | 800 |
| `ScreenHeight` | 600 |
| `ScreenDepth` | `0x17` (23) |
| `VSync` | 1 |
| `RefreshRate` | `0x3b` (59) |
| `SkipMovies` | 1 |
| `DisableVShader` | 1 |
| `MouseSensitivity` | 10 |

The measured present rate is not vsync-limited. The game displays version
1.02, but the capture has no `game_manifest.json`, so the `bo2.exe` hash is
not verified from the capture. The reference hash is
`91758398a17de9c9f974a8a102762a534fb523d40fa6868a5e5a4f0483b2753c`.

## Artifact hashes

These are the SHA-256 values of the local downloaded copies. Without a
`SHA256SUMS` file from Windows they are not authenticated against the server
copy.

| SHA-256 | Bytes | Path |
|---|---|---:|---|
| `4a1d11b097f2e92c023668e6e05a84c8e45648b6f8f00b1a88ee6936d0d8d7a7` | 1,488 | `run_summary.json` |
| `4757c0ae7e1644610738e6f702bde77567a50be298e861192a29640f49cdb8fb` | 7,383 | `inputs.jsonl` |
| `4839d68f93ea6f6e7729f25a26f627efb173c65079d455a396e8428861240cdc` | 45,754,095 | `camera_d3d8.jsonl` |
| `f7494d21f4b9fa74ae6082159ca69f00325cd882c81b610d9c6517e6fa552f97` | 1,708 | `game_config/game.erg` |
| `aa04b3008dcf1432fd0b27d77340bbb3e420ee47e53f072280f9b21bc1091eaf` | 3,212 | `game_config/registry.reg` |
| `5785190de02e2e985f10b07dff7357ec2645ae23da0351150cb58137e364aa61` | 460 | `saves/Save 00/game.bo2` |
| `12296a9f63e2d5a0a6bc2df6aea036ec0a580281d2daa4ad967331c46d8c91cc` | 784 | `saves/settings.cfg` |
| `1f0c136ff0ee37c96a91b57175d1466ef7df989092318454927c4f0a103a1991` | 1,156,589 | `screenshots/contact_sheet.png` |

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
validate_capture.py --partial:
  ok=false
  missing: game_manifest.json, notes.md, SHA256SUMS
  warning: inputs continue 36.4 s after the last camera present
  warning: frames_ppm/ has no valid PPM frames

align.py:
  26,301 present events over 59.949 s
  8 inputs aligned, 67 outside the camera coverage
  state: missing
```

Python tests: 66. CTest: 15/15. ASan: 15/15. Asset policy: clean.

## Next capture

1. Make the camera hook survive the level load or device reset and emit
   `present` and `transform` for the gameplay segment, with the device
   pointer and frame number.
2. Emit `state.csv` from the `VIEW` and `PROJECTION` matrices for the
   gameplay segment; add player fields only when the offsets are verified.
3. Add `game_manifest.json`, `notes.md`, `SHA256SUMS` and `frames_ppm/` at
   the key frames.
4. Verify the `bo2.exe` hash.
5. Confirm the jump, attack and camera steps with state or a frame taken
   during the action.
6. Keep the client and the video region at 800x600.
7. Start from a manual save at the first level or wait for the cutscene to
   finish, then capture the route from spawn.
