# Wine oracle

The pristine `bo2.exe` runs under Wine on Ubuntu with no patch. A script
prepares the prefix, and a second script replays a route of keyboard and
mouse steps and records screenshots, PPM frames, video and an input log in
the layout of the Windows captures. It needs no GPU and no display.

Reference build: `bo2.exe` SHA-256
`91758398a17de9c9f974a8a102762a534fb523d40fa6868a5e5a4f0483b2753c`, the same
file as the Windows capture.

## Quick path

1. Install the packages listed in [`SETUP.md`](SETUP.md).
2. Prepare the prefix and seed the saves from the Windows capture:

   ```sh
   scripts/oracle/wine_setup.sh external/captures/capture_20261028_0150
   ```

3. Capture the `sl01` route (about 2 minutes):

   ```sh
   python3 tools/oracle/wine_capture.py tools/oracle/routes/sl01_basic.txt
   ```

4. Check the result:

   ```sh
   CAP=external/captures/wine_YYYYMMDD_HHMM
   python3 tools/oracle/validate_capture.py "$CAP"
   python3 tools/oracle/align.py "$CAP"
   python3 tools/compare_frames.py "$CAP/frames_ppm/01_spawn.ppm" \
     external/captures/capture_20261028_0150/frames_ppm/01_spawn.ppm --out /tmp/spawn
   ```

`WINEPREFIX` overrides the prefix (default `~/.local/share/bo2-wine`) in both
scripts. `WINEDEBUG=+seh` shows the game's `OutputDebugString` log in
`logs/wine.log`.

## What a run produces

`external/captures/wine_YYYYMMDD_HHMM/`, which is gitignored.

| File | Content |
|---|---|
| `game_manifest.json` | `source: "wine"`, exe hash, Wine version, renderer, video mode, route name |
| `inputs.jsonl` | Same schema as Windows: `epoch_ms`, `mono_ms`, `event`, `key`, `duration_ms`, `name` |
| `screenshots/`, `frames_ppm/` | One PNG and one 800x600 PPM per `shot` step |
| `video.mkv`, `video_start.json` | x11grab 800x600, 30 fps; start time is the ffmpeg spawn time (about 0.3 s uncertainty) |
| `saves/` | Copy of the prefix saves taken after the run |
| `route.txt`, `notes.md`, `SHA256SUMS`, `logs/` | Route copy, generated notes, checksums, Wine and ffmpeg logs |

`validate_capture.py` treats `"source": "wine"` as a capture without
`camera_d3d8.jsonl` and `state.csv`, and skips the camera coverage warnings.
Windows captures are validated as before.

## Route grammar

One step per line. `#` starts a comment. Times are milliseconds.

| Step | Effect |
|---|---|
| `wait <ms>` | Sleep. Fails early if the game exits |
| `key <keysym>` | Tap an xdotool keysym (80 ms down), e.g. `key Return` |
| `hold <keysym>[+<keysym>...] <ms>` | Hold keys; `shift+w` presses in order and releases in reverse |
| `click <button>` | Tap a mouse button (1 left, 2 middle, 3 right) |
| `mousehold <button> <ms>` | Hold a mouse button |
| `shot <name>` | Save `screenshots/<name>.png` and `frames_ppm/<name>.ppm` |
| `phase <name>` | Log a `phase` event |

The route starts at the main menu. `wine_capture.py` waits for it by taking
a screenshot every 2 s. The menu is ready after three consecutive
comparisons with no change, while the left and right edges of the frame are
textured (the carved menu border). A black screen or a dialog on black, such
as the startup warning, is static too but fails the edge check. After 180 s
the run stops and saves `logs/boot_timeout.png`. `--boot-wait <seconds>`
replaces the check with a fixed wait.

The tool also refuses to start if the prefix's `bo2.exe` is not the
reference build, and it exits non-zero without writing the manifest if
ffmpeg dies before the end (a truncated video). `Ctrl+C`, `SIGTERM` and
`SIGHUP` all stop Xvfb, Wine and ffmpeg.

`tools/oracle/routes/sl01_basic.txt` loads PARTIDA 2 (`Save 01`, the
slums/sl01 checkpoint), then walks, runs, jumps and attacks, with the phase
names of the Windows capture.

## Why the setup is needed

The earlier "Wine is blocked" diagnosis was wrong. The game was starting
with inputs it cannot work with, and each workaround (an exe patch, d8vk, a
virtual desktop) treated a symptom. These are the actual causes:

| Cause | Symptom | Fix in `wine_setup.sh` |
|---|---|---|
| No base directory. `FUN_00578b80` reads `InstallPath` and `MoviePath` from `HKLM\Software\Crystal Dynamics\Blood Omen 2` | The game opens `\data\game.erg` at the drive root, loads no BIG and exits silently | `InstallPath=C:\BO2Oracle`, `MoviePath=C:\BO2Oracle\data` |
| `ModeID` in `HKCU\...\1.00.000` out of range | Nothing was selected in the video dialog, which was read as an empty list; the list was filled | `ModeID=2` with 800x600, `ScreenDepth=22` |
| `GraphicsDevice` differs from wined3d's device GUID `{aeb2cdd4-6e41-43ea-941c-8361cc760781}` | "Tu configuración gráfica ha cambiado" and the config dialog | `GraphicsDevice` set to that GUID as `REG_BINARY` |
| Layout differs from the Windows install | Missing data files | `external/oracle_install/`: symlinks, a flat `data/`, linked as `C:\BO2Oracle` |

Other settings: `Windowed=0`, `RefreshRate=0`, `SkipMovies=1`,
`ProgramsWarnings=0`. `ScreenDepth` is a `D3DFORMAT`, not a bit count:
`22` is `D3DFMT_X8R8G8B8`. The 16-bit mode (`ScreenDepth=23`, `ModeID=3`)
also works but is farther from the Windows frame.

Two more facts that are easy to misread:

- The `EXCEPTION_ILLEGAL_INSTRUCTION` at `0x6D327B` is the game's own 3DNow
  CPU probe. The game handles it. No patch is needed, and
  `patch_bo2_oracle.py` was deleted.
- `d3d8` must be the builtin wined3d (`WINEDLLOVERRIDES=d3d8=b`). A d8vk
  `d3d8.dll` in `system32` of an older prefix must not be used.

## Measured parity

| Check | Result |
|---|---|
| Fresh prefix (`WINEPREFIX=<new dir>`), setup then capture | Main menu without the config dialog; spawn frame equals the main prefix (mean 0.009/255, RMSE 0.29) |
| Main menu vs Windows `00_main_menu_reference` | mean abs error 0.57/255, RMSE 6.5 |
| Spawn vs Windows `01_spawn` | mean abs error 4.88/255, RMSE 8.55, max 184 |
| Menu detected | about 10 s after launch |
| `sl01_basic` route | about 80 s of input time, 9.5 MB video |

The spawn difference is spread over almost every pixel at a low level. It
is not yet attributed (GPU rasterization versus llvmpipe, or the animation
phase at capture time).

What the route shows: spawn, walking and running toward the shed, and a
clear jump (Kain in the air above the shed in `04a_during_jump`). The left
mouse attack shows no animation, which matches the Windows capture, where
the attack was also not confirmed.

## Limits

- No D3D hook: no camera pose and no player state. The Windows capture
  also has no camera pose, because `VIEW` is the identity in gameplay.
- llvmpipe is a CPU rasterizer. Frame timing does not match a GPU, so
  compare frame content, not timing.
- The game's present rate under Wine is unknown. On Windows it was
  uncapped (median 250 fps).
- Inputs are stamped when `xdotool` returns, not when the game reads them.
- The level load time varies (20-40 s); the route uses a fixed 45 s wait.
- Only the spawn frame is reproducible. After the first move the game state depends on
  frame timing: two `sl01_basic` runs gave different jump frames (position and camera).
- The attack input is unconfirmed on both platforms.

## Next step

Read the player and camera state from `/proc/<pid>/mem` of the Wine process
and write it as `state.csv`. Use only offsets verified in the decompilation,
as the Windows harness does, and add the columns `validate_capture.py`
already expects. The reasoning for each offset belongs in
[`REVERSING.md`](REVERSING.md).
