# Wine oracle

The pristine `bo2.exe` runs under Wine on Ubuntu with no patch. A script
prepares the prefix, and a second script replays a route of keyboard and
mouse steps and records screenshots, PPM frames, video and an input log in
the layout of the Windows captures. It also samples Kain's position,
velocity, health and lore from the game's memory into `state.csv`. It needs
no GPU and no display.

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
| `game_manifest.json` | `source: "wine"`, exe hash, Wine version, renderer, video mode, route name, `state` summary |
| `state.csv` | Kain's state sampled every 20 ms; see [Game state](#game-state). Absent with `--no-state`, if the memory cannot be read, or if Kain never appears. If sampling stops mid-run, the rows taken so far are kept |
| `instances.json` | Only with `--dump-instances PHASE`: every game instance with its draw fields; see [Instance dump](#instance-dump) |
| `inputs.jsonl` | Same schema as Windows: `epoch_ms`, `mono_ms`, `event`, `key`, `duration_ms`, `name` |
| `screenshots/`, `frames_ppm/` | One PNG and one 800x600 PPM per `shot` step |
| `video.mkv`, `video_start.json` | x11grab 800x600, 30 fps; start time is the ffmpeg spawn time (about 0.3 s uncertainty) |
| `saves/` | Copy of the prefix saves taken after the run |
| `route.txt`, `notes.md`, `SHA256SUMS`, `logs/` | Route copy, generated notes, checksums, Wine and ffmpeg logs |

`validate_capture.py` treats `"source": "wine"` as a capture that does not
need `camera_d3d8.jsonl` or `state.csv`, and skips the camera coverage
warnings. A `state.csv` that is present is checked like the Windows one; its
empty `cam_*` columns are not an error. Windows captures are validated as
before.

## Route grammar

One step per line. `#` starts a comment. Times are milliseconds.

| Step | Effect |
|---|---|
| `wait <ms>` | Sleep. Fails early if the game exits |
| `key <keysym>` | Tap an xdotool keysym (80 ms down), e.g. `key Return` |
| `hold <keysym>[+<keysym>...] <ms>` | Hold keys; `shift+w` presses in order and releases in reverse. A key written `name@delay` goes down that many ms after the first one (delays must not decrease and must fit in `<ms>`), so `w+x@800 2300` runs for 0.8 s and then jumps with W still down |
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

`tools/oracle/routes/sl01_basic.txt` loads the second save slot (`Save 01`, the
slums/sl01 checkpoint), then walks, runs, jumps and attacks, with the phase
names of the Windows capture. Three more routes start from the same spawn and
exist to measure one system each:

| Route | Measures | Used in |
|---|---|---|
| `sl01_air.txt` | Jump direction: forward, back, sideways, and a direction pressed in the air | `docs/GAMEPLAY.md`, Air control |
| `sl01_air_turn.txt` | The turn in the air | same |
| `sl01_camera.txt` | The follow camera: turns in place, a standing jump and a turn while walking | `docs/GAMEPLAY.md`, Camera |

The state of a run depends on where Kain ends the earlier phases, so each of
these routes keeps its phases short and turns around between forward runs.

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

## Game state

`wine_capture.py` reads Kain's state and the gameplay camera from the game's
memory every 20 ms and writes them as `state.csv` with the Windows header
plus four columns. `--no-state` disables sampling.

### How it works

- `tools/oracle/memstate.py` reads `/proc/<pid>/mem`. Addresses are the
  Windows virtual addresses of the decompilation; they are the same inside
  the Wine process.
- The pid is the one `wine_capture.py` launched: `wine` runs `bo2.exe` in
  that process. Yama `ptrace_scope` 1 lets a process read the memory of its
  descendants, so no privileges are needed. If the memory cannot be read
  (`ptrace_scope` 2 or 3 refuses the open with `EACCES`), the tool logs one
  warning, stops sampling and finishes the capture without `state.csv`. It
  also writes none if Kain never appears.
- A background thread looks for Kain from the start of the route, every
  0.5 s, until he exists. Then it samples every 20 ms.
- Kain is found by walking the instance list. An instance is Kain when its
  behavior object has the RTTI vtable of `kain::KainTopLevelBehavior`.
- Freed heap objects stay mapped and keep reading fine, so a reload could
  leave frozen rows. Every sample checks that `u32(behavior)` is still the
  Kain vtable and that `u32(instance+0x124)` is still that behavior. If
  either fails, or a pointer stops reading, the thread drops Kain and
  searches again.
- The camera is searched once Kain exists, at most every 2 s, because the
  search reads the writable heap (see "Camera" below). Each search logs its
  duration in ms (57 ms in `wine_20261029_0110`). Every sample checks that the camera still has the follow
  camera vtable and that `u32(follow+4)` is Kain's instance; otherwise it is
  dropped and searched again.
- Sampling stops for good, and keeps the rows already taken, when the game
  exits (a read fails and `/proc/<pid>` is gone) or an unexpected exception
  is raised. It logs one warning, and `game_manifest.json` records the
  reason in `state.stopped`.

### Offsets

Each value comes from the decompilation. "HUD" is the Kain debug HUD
(0x42a200; Ghidra splits its tail as `FUN_0042a3e0`).

| Field | Read | Source |
|---|---|---|
| Instance list | `u32(0xA836A8)` is the head node of a circular list; node `[next, prev, instance]` | `FUN_004a5290` |
| Behavior | `u32(instance+0x124)`; Kain when `u32(behavior) == 0x841D84` | `FUN_004d9850` (`GetPlayerHitPoints`), RTTI |
| `player_x/y/z` | f32 x3 at `instance+0xB4` | HUD |
| `player_yaw_deg` | f32 at `instance+0xC8` (radians), in degrees. Only the yaw is read | The rotation triple at `instance+0xC0` is applied as X, Y, Z rotations by `FUN_005c1790` (called from `FUN_007535d0` with `inst+0xb4`, `inst+0xc0`); the HUD reads `+0xC8` as yaw (its debug arc drawing uses `instance+200`) |
| `vel_x/y/z` | f32 x3 at `behavior+0x19C`, world frame | HUD |
| `on_platform` | u32 at `behavior+0x4E0` | HUD |
| `anim` | u32 at `behavior+0x128+0x458` (KainState+0x458), the animation index | FUN_00442800 and FUN_004436e0 store it right before the animation play call; the jump runs 76, 78, 79, 86 and the run 1 |
| Stats | `u32(behavior+0x128+0x594)` | The HUD passes ECX=`behavior+0x128` to `FUN_00425fb0` |
| `h` | `u32(stats+0x11C)` | `FUN_00425fb0` is the getter returning `u32(u32(this+0x594)+0x11C)` |
| `health` | f32 at `h+0xC` | Read in `FUN_004d9850` (`GetPlayerHitPoints`) |
| `health_max` | f32 at `h+0x14` | Read at 0x42a696 in the HUD |
| `lore` | f32 at `l+4`, with `l = u32(stats+0x124)` | `FUN_00425fc0`, plus the HUD "Cur Lore" print |
| GameCamera | 0x180-byte object with a second vtable `0x83F770` at `+0x18`; found by that vtable in the heap | `FUN_00409b50` (constructor), RTTI |
| Follow camera | `follow = u32(GameCamera+0x68)`, vtable `0x83F5E4` (`FollowingCreatureCameraImpl`, 0x114 bytes) | `FUN_00406de0`, RTTI |
| Follow target | `u32(follow+4)` is the followed creature's instance; it held Kain's instance address | Observed live, not read in the decompilation. Used only to pick and validate the camera, never emitted |
| `cam_x/y/z` | f32 x16 camera-to-world matrix at `follow+0xC`; the eye is elements 12-14 (`follow+0x3C`), valid when the type u32 at `follow+0x4C` is 4 or 8 | `FUN_004088c0` reads the translation and the type |
| `cam_yaw_deg`, `cam_pitch_deg` | row 0 of that matrix as the forward vector: yaw `atan2(y, x)`, pitch `asin(z)` | Checked live only: row 0 matches Kain's heading when the camera has caught up |

### Columns

| Filled | Empty (not sampled, never invented) |
|---|---|
| `frame` (sample index), `epoch_ms`, `mono_ms` (sample time), `phase` (route phase), `cam_*`, `player_*`, `vel_*`, `health`, `player_yaw_deg`, `health_max`, `lore`, `on_platform`, `anim` | `fps_inst`, `grounded`, `mana`, `weapon`, `tick`, `view_*`, `proj_*` |

Floats have six decimals. `player_yaw_deg` is the raw Euler value, not
unwrapped. `game_manifest.json` gets a `state` entry: rows, target and mean
period, the Kain instance and behavior addresses, the follow camera
address, the number of rows with a camera pose, `offsets` (a pointer to the
table above) and, if sampling stopped early, `stopped` with the reason.

### Camera

`VIEW` is the identity in gameplay and `CamView` (vtable `0x87805C`) is
inactive, so the pose comes from the follow camera that `GameCamera` owns.
`FUN_004088c0`, its update, reads `follow+0xC` as a camera-to-world matrix.
The sampler takes the eye from the translation and uses row 0 as the
forward vector; that row was checked live only.

Checked in `wine_20261029_0052`: at `after_w` the camera sits on Kain's
heading (side offset 0.0), 190.9 behind and 96.1 above him with a pitch of
-6.0 deg. At the end of `after_a` and `after_d` its yaw follows his (157.7
vs 157.9 deg, -9.6 vs -9.9). In the `run` phase Kain was blocked (his speed
fell to about 40 u/s) and the camera pulled in to as little as 8.9 behind,
consistent with the collision pull-in in `FUN_004079c0`.

### Measured (`wine_20261029_0043`, `sl01_basic`)

| Check | Result |
|---|---|
| Rows, rate | 2991 rows in 59.8 s; 50.0 Hz mean, 20 ms median, 26 ms max interval |
| Kain found | 0.8 s after the load was requested (the last `Return` of the route) |
| Spawn | (-8.803, 57.129, 208.000), the expected (-8.80, 57.13, 208.0) |
| Health, lore | health 30 of 30 (18.3 at the first sample, full after 0.24 s); lore 0 |
| `on_platform` | 0 in every row |
| `after_w` (`w`, 2 s) | horizontal speed rises 0, 100, 230 u/s and holds 230 |
| `run` (`shift+w`, 2 s) | peak horizontal speed 200 u/s; it falls to about 45 u/s for 1 s, so Kain was probably blocked, and then reaches 200 again |
| Turn rate (`after_a`, `after_d`, key held 1 s each) | Least-squares slope of `player_yaw_deg` over `mono_ms`: `a` +158.1 deg/s (50 rows over 0.98 s, r 0.9999), `d` -160.1 deg/s (51 rows over 1.00 s, r -0.9999); no horizontal velocity |
| Jump | first airborne sample vz 554 u/s, peak 75.4 above the ground (z 208.0 to 283.4) about 0.24 s after the first airborne sample, airborne 0.66 s, landing vz -308 |
| Gravity from vz(t) | ascent -2156 u/s2 (12 samples, r -0.994), descent -706 u/s2 (22 samples, r -0.999) |

The turn fits use the rows of `state.csv` between the key's `down` and `up`
events in `inputs.jsonl`, from the first to the last sample where the yaw
changes. Yaw does not wrap in these rows.

Reading the jump: vz does not follow one line. The ascent decelerates three
times faster than the descent, so the jump is not a single constant gravity.
The first airborne sample is at z 215.3, not at the takeoff, so 554 u/s is a
lower bound on the takeoff impulse (the ascent fit extrapolates to 537 u/s at
that sample). The 200 u/s peak of `run` is below the 230 u/s that plain `w`
reaches; the run phase was probably blocked, so it is not a clean top speed.

### Instance dump

`--dump-instances PHASE` reads every instance of the list at `0x00A836A8`
once, when the route enters `PHASE`, and writes `instances.json` next to
`state.csv` (`read_instances` in `tools/oracle/memstate.py`). Each row has
the instance address, its position (`+0xB4`), the entity flags (`+0xF0`;
`0x200` is the template's `helperobj`, `FUN_0070d0d5`), the hidden byte
(`+0x139`, `FUN_0070dfa0`), the render LOD and LOD table of its
`goe::AnimModelInst` (`+0x9C`, `+0xA0`; `FUN_005dcbf0`) and its behavior's
vtable. Match the rows to the region entities by position to learn how the
original treats each entity type; `docs/FORMATS/models.md` ("Helper
objects") uses it at the `sl01` spawn. It needs the state sampler, so it
cannot be combined with `--no-state`.

## Input bindings

The keys of the oracle run are not the game's defaults. The defaults are in
`kain.imp` (keyboard: attack C, combat Z, evade X, jump S, action Space); the
prefix holds a user rebinding in
`drive_c/users/<user>/Documents/Blood Omen 2/settings.cfg` (784 bytes, written
by the Windows install and seeded by `wine_setup.sh`). Layout, read from the
file and cross-checked with the oracle:

| Part | Content |
|---|---|
| Header | 16 bytes: `60 2b 1d 6f a0 d5 cf 11 bf c7 44 45 53 54 00 00` |
| Records | From offset 0x10, 12 bytes each: `u32 index, u32 key1, u32 key2` (0 = unbound) |
| Key code | `0x400 + DirectInput scancode` for the keyboard (`0x42d` is X, `0x438` Left Alt); `0x100 + button` for the mouse (`0x100` left, `0x101` right) |

The index follows the rows of the options menu (list at 0x8FE514, FUN_0055a370).
Only the first row is verified: the oracle jumps with the right mouse button
and with X. The effect column is what the `anim` column of `state.csv` showed at
the `sl01` spawn (`external/captures/wine_probe_attack_1` to `_4`, taps and
holds, each key alone and in pairs):

| Index | Row | Keys in the prefix | Effect at the spawn |
|---|---|---|---|
| 0 | jump | right mouse, X | Jump: animations 76, 78, 79, 86 |
| 1 | bloodsuck | Left Alt | None |
| 2 | dark gift | Z | None |
| 3 | action | left mouse, Space | None |
| 4 | dark gift menu | Left Ctrl | None |
| 5 | autoface | Left Shift | Animation 13 for about 0.2 s, then idle |
| 6 | block | C | None |
| 7 to 10 | forward, backward, left, right | W, S, A, D | Walk and turn |
| 11 | look around | Right Ctrl | None |
| 12 | (pause) | Esc | Not probed |

The engine's virtual buttons are a different table (0x8F43C8: jump 0x5C, attack
0x5D, evade 0x5E, combat 0x5F, action 0x60); the map from these rows to those
ids is not traced. The melee attack needs combat mode: the combo trigger in
`kain.react` requires an attack press while combat mode is on, and combat
mode is held on the `combat` button. At the spawn no key or pair of keys starts one:
Space, the left mouse button or Z held together with Alt, C, Ctrl or Shift
leave `anim` at 0. So the attack is still unconfirmed; the Shift, Alt, Space
and left mouse inputs of `sl01_basic` are the candidates, and the missing piece
is the state that enables combat (an equipped weapon or a target is the first
thing to check).

Shift is bound to the autoface row, not to a run, so the 200 u/s of `shift+w`
measured in `sl01_basic` is probably Kain with that action active (not
verified).

## Limits

- No D3D hook: no `camera_d3d8.jsonl`. The camera pose comes from memory;
  the Windows capture has none, because `VIEW` is the identity in gameplay.
- `state.csv` rows are stamped when the sampler reads, not at a game tick.
  The game updates at its own rate, so consecutive rows can repeat or skip a
  tick (the jump's vz steps are uneven).
- Sampling needs `ptrace_scope` 0 or 1.
- llvmpipe is a CPU rasterizer. Frame timing does not match a GPU, so
  compare frame content, not timing.
- The game's present rate under Wine is unknown. On Windows it was
  uncapped (median 250 fps).
- Inputs are stamped when `xdotool` returns, not when the game reads them.
- The level load time varies (20-40 s); the route uses a fixed 45 s wait.
- Only the spawn frame is reproducible. After the first move the game state depends on
  frame timing: two `sl01_basic` runs gave different jump frames (position and camera).
- The attack input is unconfirmed on both platforms; see [Input bindings](#input-bindings).

## Next step

Use `state.csv` to derive the movement and camera constants for the port
(`--play`): walk and turn rates, the jump curve, and the follow distance,
height and pitch, each confirmed in the decompilation before it is used.
Add the logic tick when it is located.
