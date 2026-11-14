# Gameplay translation

Inventory of the systems to recover. Status is `todo` until a function is
translated and verified.

| System | Status | Notes |
|---|---|---|
| Boot and front-end | partial | Language: the port starts in the `spanish` locale like the Spanish build (FUN_004aae40 forces it, FUN_006c8fb0 filters BIG entries) and reads the localized string tables, see [`FORMATS/big.md`](FORMATS/big.md#language) and [`FORMATS/strtable.md`](FORMATS/strtable.md). Logos, menus and options todo |
| Level loading and streaming | todo | Container readers, level table |
| Camera | partial | Follow pose, yaw catch-up, jump height lag and range smoothing ported; combat and scripted cameras todo. See [Camera](#camera) |
| Player movement | partial | Ground turn, ground speeds, partial-stick blend, jump, air control and fall cap; glide, climb and swim todo. See [Kain movement](#kain-movement) |
| Combat | todo | Melee, block, grab, finishers |
| Dark gifts | todo | Unlock and use |
| Weapons and inventory | todo | Pick up, equip, drop |
| Enemies and AI | todo | Behaviours, patrol, combat |
| Scripting and triggers | todo | Level events and dialogue |
| HUD and menus | todo | Health, rage, prompts |
| Saves and checkpoints | todo | Format and flow |
| Audio events | todo | Dialogue, effects, music |

## Rules

- Translate the original, do not redesign it.
- Every row points to at least one `FUN_` address once work starts.
- Behaviour claims need a local decompile and a test or oracle comparison.
- Keep this table current; it is the map of what the port actually does.

## Kain movement

Kain turns in place, walks and runs along his yaw, jumps with two-phase
gravity and a hold boost, steers in the air, and a follow camera sits behind
him and lags a jump.
The code is `src/game/kain_tuning.cpp`, `src/game/player.cpp` and
`src/game/camera.cpp`; `bo2nx --play` runs it. Units are game units (about
1 cm) and seconds, Z up, forward = (cos yaw, sin yaw, 0).

The constants come from three places. Read the source column before trusting
a number:

- **B+offset**: a field of the `kain` tunedata, see
  [`FORMATS/tunedata.md`](FORMATS/tunedata.md). `B -> A` means FUN_00428fe0
  copies it into the runtime tune object A (`behavior+0x164`) and the
  consumer reads A.
- **FUN_**: the value is derived by that function's math.
- **animreel**: the root motion of a clip in kain.big, decoded in
  [`FORMATS/animreel.md`](FORMATS/animreel.md).
- **animatio**: the `kain` animation graph (states, sequences, clip nodes),
  decoded in [`FORMATS/animatio.md`](FORMATS/animatio.md).
- **measured, oracle**: read from the Wine capture
  `external/captures/wine_20261029_0052/state.csv`. These stand in for
  data that is not decoded yet.

### Ground

| Value | Number | Source |
|---|---|---|
| Stick deadzone | 0.1 | B+0x00C, read directly from the tunedata blob (KainState+0x40) by FUN_00425520 and FUN_0041ecf0 |
| Turn rate (Ready, Run, WalkBack) | 2.5 rad/s each | B+0xAC8 -> A+0x18, B+0xACC -> A+0x1C, B+0xAD0 -> A+0x20 (FUN_00428fe0); passed to FUN_0041ecf0 |
| Yaw rate | `rate * min(abs(ax) / (1 - deadzone), 4)`; A/D give 2.7778 rad/s | FUN_0041ecf0; measured, oracle: +158.1 and -160.1 deg/s |
| Turn direction | `ax > 0` lowers the yaw, `ax < 0` raises it, so A is a negative `ax` and D a positive one. The port's `move_x` has the same sign: D is +1, A is -1 | FUN_0041ecf0; measured, oracle: A raises the yaw |
| Turning | In place, no strafe | measured, oracle |
| Run clip rate | 1.15, the playback rate of the run clip at full stick | B+0xB14 -> A+0x40 (FUN_00428fe0); read by FUN_00445ba0 (slot 0 gets rate `(0.5 s + 0.5) * A+0x40` at stick `s`, so `A+0x40` at `s = 1`) |
| Run root speed | 200 u/s | `kain/run` animreel, node 0 translation x: 0 to 160 over 24 frames (0.8 s) |
| W, first 0.2 s | 100 u/s. From rest the graph plays `st_rdy2run`, a held frame behind a 0.2 s blend whose transform is (20, 0, 0); while a clip blends in, the root gets the part of the blend's transform that matches the blend time played. When the blend ends Kain is 20.1 u ahead: the original counts 0.001 s more of the blend | FUN_005ceef0, blend 4 of the `kain` graph ([`FORMATS/animatio.md`](FORMATS/animatio.md), Root motion); oracle: x is the rest position plus 20.100 when the blend ends (`wine_probe_20261111`, frame 2253) |
| W, after it | 230 u/s = run root speed 200 * run clip rate 1.15. The tick where the run clip ends drops what is left of it, and the first tick of every restart plays at the node speed 1.5 (300 u/s): the run state writes the rate before the advance that restarts the clip | FUN_00445ba0, FUN_00445930, FUN_00643f80; oracle: world \|vel_xy\| reads 300 at about +191, +892 and +1572 ms after W in `external/captures/wine_20261029_0110/state.csv`, spaced by the run cycle 0.8 / 1.15 = 0.696 s, and 132.98 at the end of a cycle in `wine_probe_20261111` |
| Shift+W | 200 u/s (= 200 * 1.0, unexplained), no start ramp. Unverified: Kain was probably blocked for part of that phase | measured, oracle (`docs/ORACLE_WINE.md`) |
| Walk root speed | 56.3485 u/s forward | `kain/walk` animreel, node 0 translation x: 0 to 86.4010 over 47 frames (1.5333 s) |
| Walk-back root speed | 62.5 u/s backward | `kain/walk_backwards` animreel, node 0 translation x: -2.0833 to -100 over 47 frames (1.5667 s) |
| Partial stick | At stick magnitude s (clamped to 1): run rate `(0.5 s + 0.5) * 1.15`, walk rate `(s + 1) * 1.15`; speed = `run_rate * s * 200 + walk_rate * (1 - s) * 56.3485`. At s = 0.5 it is 134.85 u/s; at s = 1, 230 | FUN_00445ba0; with the animation the port sums the root motion of layers 0 and 1 by these weights, which gives the same speed. The root output of the cross blend entity is not decompiled |
| S | 106.25 u/s = walk-back root speed 62.5 * clip node speed 1.7, yaw unchanged. `sq_back` starts with blend 6, 0.1 s without a transform, so Kain stands still for 0.1 s first (FUN_005ceef0) | The 1.7 is the `speed` of the only clip node on `kain/walk_backwards` in the `kain` animatio graph (docs/FORMATS/animatio.md; read by `apply_kain_clip_speeds`). Unlike the run, nothing overrides it: FUN_00723620 rewrites the rate of the run layers only. The oracle confirms 62.5 * 1.7 = 106.25 u/s. The clip starts with a measured 100 ms windup: the oracle's x stays constant for frames 2381-2385 of `sl01_basic` after the S press, and the port models it with `kBackStartTime` |
| Release | 50 u/s for 0.2 s, then still: `st_ready` from `st_run` goes through `st_run2rdy`, whose 0.2 s blend carries the transform (10, 0, 0) | FUN_005ceef0, blend 0 of the `kain` graph; oracle: 50.0 u/s for ten samples after W is released (`wine_camera_1`, frames 3461 to 3470) |

### Air

| Value | Number | Source |
|---|---|---|
| Integrator | `pos += (v + a*dt/2)*dt; v += a*dt` | FUN_00432030 |
| Gravity | -2500 while vz >= 0, -700 once vz < 0 | B+0x9FC, B+0xA00; FUN_00442ae0 |
| Fall speed cap | 512 | B+0xA08; FUN_00441f20 |
| Jump trigger | On the press edge; the button must be released before the next jump | Assumption. The original trigger path (the caller of FUN_004436e0) is not traced |
| Launch vz | 565.685 = `sqrt(2 * h1 * abs(g_up))` | B+0xA10; FUN_004436e0, FUN_00451d00 |
| Tap jump | Apex 64, airtime 0.6539 s (0.2263 up, 0.4276 down) | B+0xA10; FUN_00451d00 |
| Hold boost | +1998.94 u/s2 from 0.066 s to 0.3 s after takeoff while jump stays down; lifts the apex toward 128 | B+0xA14, B+0xA18, B+0xA1C; derived by `derive_jump` from FUN_00451d00 |
| Boost on release | The tick's boost is applied first; the release is seen afterwards and ends the boost | FUN_00443a50 |

Releasing the jump button inside the boost window ends the boost, so the apex
lands between 64 and 128: a 5-tick hold at 60 Hz peaks at 74.7.

### Air control

A jump has a direction. The stick picks it at takeoff and each air tick moves
the horizontal velocity toward what the stick asks for. The port applies it to
jumps only (`PlayerController::steer_in_air`); walking off a ledge keeps the
ground speed, see Pending. "Block" below is a row of `docs/FORMATS/tunedata.md`
("Air steering blocks"); its field names are those of `kaintune.h`.

| Rule | Number | Source |
|---|---|---|
| Direction class | Below the deadzone: none. Otherwise `ay` (negative for W) and `ax` (positive for D) give forward or back inside the cone `ax` within `+-2 abs(ay)`, left for a larger negative `ax`, right for a larger positive one | FUN_00425580 (stored at KainState+0x540 every tick) |
| Launch speed | `abs(stick) *` the block's launch speed along the heading: forward 146.81, back -97.88 u/s. Left and right jumps launch sideways at 110.11 u/s (A to Kain's left, D to his right) | FUN_004436e0; blocks B+0xA20, B+0xA74, B+0xA38 through FUN_00452030 |
| Sideways vertical model | The `sideVertJump` block replaces the jump: launch 619.68, `g_up` -3000, apex 64, no boost. The descent uses the normal `g_down` | FUN_004436e0 copies A+0x440; FUN_00442ae0 reads A+0x408 |
| Top speed | Forward jump 322.96 u/s forward; back jump 205.25 backward; sideways jump 203.00 to the side and 256 forward or back. The other axes of a forward or back jump are 0 | FUN_00441dc0 (picks the numbers), FUN_00452030 (peak and `MaxLateralVel`) |
| Target speed | The stick rescaled from the deadzone to 1, times the top speed of its axis | FUN_00442240 |
| Acceleration | Toward the target at 4403.56 u/s2 when speeding up forward, 583.54 when slowing or going back, 504.83 sideways | FUN_004423b0; B+0xA20, B+0xA74, B+0xA38 |
| No stick | The velocity keeps 96 percent per tick, then loses `dt *` the slowing acceleration, and stops at zero. The 0.96 is per tick, so it depends on the tick rate as in the original | FUN_004423b0 |
| Turning | A forward or back jump turns `jumpTurnRate * ax` deg/s (45); D lowers the yaw. The velocity is rotated to the heading's frame, changed, and rotated back with the new yaw, so it follows Kain. A tick that turns by 1 degree or more leaves the velocity alone. A standing or sideways jump does not turn | FUN_00441f20, FUN_00694970; B+0x9DC `jumpParams.jumpTurnRate` |
| First direction in the air | A jump with no direction takes the first direction pressed in the air and keeps it until landing | FUN_00441f20 (`this+0x3c == 0`), FUN_00441dc0 |
| Landing | The jump velocity ends; the ground speed returns | port; the original zeroes the velocity in the ground state (FUN_00432030) |
| Wall | The port stops the jump velocity when the move is blocked | port; the original zeroes the forward or back velocity when blocked (KainState+0x450 and 0x454) |

The flags `0x800000` (run state) and `0x400000` (stick scale 0.25), the platform
velocity added at takeoff and the glide branch are not ported.

Measured in the Wine oracle (`tools/oracle/routes/sl01_air.txt`, capture
`external/captures/wine_air_1`, and `sl01_air_turn.txt`, capture
`external/captures/wine_air_turn_1`; the game ticks at about 50 Hz, one
`state.csv` row per tick):

| Check | Derived | Oracle |
|---|---|---|
| Forward jump speed (W and X), 4 jumps | 322.96 | 323.0 in every row after the ramp |
| Back jump speed (S and X), 2 jumps | 205.25 | 205.2 |
| Sideways jump speed (D and X) | 203.00 | 203.0 |
| Back and side ramps | 583.5 and 504.8 u/s2 | about 556 and 489 (steps of 20 ms rows, 3 to 5 percent low) |
| Air turn rate, forward jump with D, with A, back jump with D | 45 deg/s | -44.3, +44.1 and -45.5 deg/s (least squares over 40 to 50 rows, r above 0.998); the velocity direction equals the yaw to 0.1 deg |
| Standing jump, then D in the air | no turn, 203.0 | 0.0 deg/s and 203.0 |
| Standing jump, then W in the air | 322.96 | 323.0, within 2 rows of the first speed |
| W released in the air | 323 to 0 in 19 ticks, 48.5 u | 18 rows, 44.2 u |
| Forward jump, X at 300 ms, D at 500 ms, held to 1700 ms | 1.00 s, step (302.6, -84.3), yaw -36 deg | 1.02 s, step (289.3, -84.8), yaw -36.6 deg at the landing; the last 0.1 s shows the speed dropping from 309 to 186, so Kain touched geometry |

The takeoff tick is not visible: the first airborne row already shows 320.3 u/s
(forward) or 127.4 u/s (sideways), because the first tick after the press is
about 35 ms long under llvmpipe (the vertical speed of the same row, 474.8 or
518.1, fits a 34 ms step of `g_up`). The sideways jump rose 69.0 above its
takeoff in the oracle against the model's 64.0; not explained.

### Camera

`FollowCamera` (`src/game/camera.cpp`) ports `FollowingCreatureCameraImpl`
(vtable 0x83F5E4). Its per-frame update is FUN_00408b10, which calls the yaw
step FUN_00407ec0, the height step FUN_00407c70, the pitch, range and look
steps FUN_00407fb0, FUN_00408300 and FUN_00408480, builds the pose, and ends
in FUN_004088c0 (range smoothing and the collision pull-in FUN_004079c0).
The constants are fields of the live `CameraSetup` (`follow+8`) that the oracle
read from the game's memory at the `sl01` spawn
(`external/captures/wine_camera_1`, a one-off dump of `follow` and its setup
every tick). The code that builds that setup was not traced.

| Value | Number | Source |
|---|---|---|
| Range, pivot to eye | 192 | setup+0x18; follow+0x50 (FUN_004074c0 copies it) |
| Eye elevation above the pivot | 0.10472 rad (6 deg) | setup+0x1c; FUN_00408b10 offsets the eye by `(-cos p, 0, sin p) * range` in the yaw frame |
| Pivot height above Kain | 76 | setup+0x0c; FUN_00407c70 sets the pivot to Kain's z plus it |
| Pose | Eye 190.94 behind and 96.07 above Kain, looking along the yaw pitched down 6 deg | the three rows above; measured, oracle: 190.9, 96.1 and -6.0 deg |
| Yaw catch-up | A cubic Hermite blend from the camera yaw and yaw speed to Kain's yaw at rest, advanced each frame by `min(dt, duration)`; the duration is the largest root of the four quadratics that bound the end accelerations by the limit | FUN_00407ec0 with FUN_00713360, FUN_00713450 (duration), FUN_00713810 and FUN_00713870 (position and velocity); the angle is taken closest to Kain's (FUN_00694400) |
| Yaw acceleration limit | 150 rad/s2 from setup+0x30, times 5 when `input device type == 2` (DAT_0083f718); the port uses 750, see below | FUN_00407ec0 |
| Height lag after a jump | At takeoff the camera is given a setup with delay 0.1 s and blend time 0.6 s. The pivot holds its resting height for about the delay, then blends from that height to Kain's z plus 76 with the same Hermite blend (end speed zero, start speed carried between frames), never below Kain's feet. The blend progress is the time since takeoff over 0.6 s, so it ends 0.6 s after takeoff; then the pivot is rigid again | FUN_00407c70 and FUN_004074c0; the setup was read live (setup+0x10 = 0.1, setup+0x14 = 0.6). Measured in the live follow object: +0xD4 equals +0xCC during the blend and the delay field reads 0 from about 0.08 s, so the 0.1 s hold is an approximation (FUN_00407c70 sets +0xD4 = +0xCC - delay, which the data does not show) |
| Range smoothing | Pull-in instant; growth with the Hermite blend at 2000 u/s2, one third of the limit for the 0.5 s after a pull-in, at least 10 from the pivot | FUN_004088c0 and FUN_004079c0; `wine_camera_1` grows from 19.76 to 192 in 0.99 s; the port reaches 192 in 1.00 s in `tests/unit/test_camera.cpp` |

Checked against the capture (`FollowCamera` replayed over its `state.csv`
rows, `docs/ORACLE_WINE.md`, route `sl01_camera.txt`):

| Check | Oracle | Port |
|---|---|---|
| Camera yaw behind Kain on a 160 deg/s turn | 0.2 to 0.3 deg | at most 1.6 deg (first tick of a turn), 0.5 deg after a short tap |
| Same turn with the limit at 150 (no keyboard factor) | 0.2 to 0.3 deg | up to 5.1 deg: too slow, so the factor of 5 is kept |
| Eye height in a standing jump (rest 304.07, apex of Kain 281) | peak 356.19 | 356.32; within 1.1 over the jump except the first row (takeoff seen one row late) |
| Pivot blend over 26 rows of the oracle's blend state (blend weight 0.07 to 1 over 0.5 s) | the pivot heights of the dump | the same Hermite blend: 22 rows within 0.4 u, the other 4 differ by 1.2 to 4.7 where the Kain z of that row is a tick off |
| Range recovery after a collision pull-in | 19.76 to 192 in about 0.99 s (`wine_camera_1`) | 1.00 s with the 0.5 s one-third limit and the Hermite blend |

The range blend of FUN_004088c0 is ported. The eye is pulled in at once when
the level collision mesh is in the way (FUN_004079c0, a ray from the pivot);
the range then grows back through the same acceleration-limited Hermite
blend, at 2000 u/s2 (30000 when blocked), one third of the limit during the
0.5 s after a pull-in, and never closer than 10 to the pivot. The oracle
`wine_camera_1` grows from 19.76 to 192 in 0.99 s; the port reaches 192 in
1.00 s in `tests/unit/test_camera.cpp`. The setup blend of FUN_00408300
(range, pitch and tilt changes by script), the second-target pitch of
FUN_00407fb0 and the stick look of FUN_00408480 are not ported and are not
needed for the keyboard on this level.

The `after_a` phase of the first oracle capture (117 back, 88 high) is that
collision pull-in against a wall, not a yaw lag, as the earlier notes read it.

### Port-only behaviour

These have no original source. They are development aids or placeholders.

| Item | Note |
|---|---|
| Mouse and right-stick look | Adds to yaw and to the camera pitch offset. Not part of Kain's controls |
| Eye height 1.7 | Provisional for the camera eye and the ground raycast. The original's r=1 ground/step probe is not ported: its hit is a 3D correction inside the slide state (`FUN_00430820`), and the vertical part alone drops the body into small pits |
| Attack range 2.2, radius 1.0, cooldown 0.45 s | Provisional, same reason |
| Move sub-steps | A tick of travel is up to 230 / 60 = 3.8 units, ten times the body radius, so a move is walked in steps of one radius to keep thin walls solid |
| Forward and back | On the ground the root motion of the animation moves Kain (`KainAnimation::ground_advance`): the start of the run, the 0.1 s without movement before the walk back and the slide after W is released are blends of the graph. Without an animation the controller keeps the steady clip speeds. The platform layer also zeroes sticks below 0.25 before the deadzone applies |
| Step cap | A step is cut to 0.1 s (the cap the SDL loop already uses) and a move to 64 sub-steps, so a stalled frame cannot tunnel through a wall or the floor |
| Deadzone edge | Turning starts at exactly the deadzone (FUN_0041ecf0 compares `deadzone <= ax`); the forward/back gate is not traced and the port uses past-the-deadzone |
| Run button | `run` is Shift and gives 200 u/s, which is slower than plain W (230). That is what the oracle measured (unverified, see above); the name is kept from the input layer. In the oracle prefix Shift is the `autoface` row, not a run (`docs/ORACLE_WINE.md`, Input bindings), so this speed is probably that action |

### Pending

| Item | State |
|---|---|
| Root motion | Decoded for the `kain/run` and `kain/walk_backwards` clips and used for the W and S speeds above; the walk-back clip node speed is read from the `kain` animatio graph. Clip blending, events and the landing clips are not ported |
| Shift+W | The 200 u/s of Shift+W is a measured constant with no decompiled source: Shift is combat mode (BO2NX-83). The start of the run at partial stick has no oracle capture; the port sums the root motion of the run and walk layers by the weights of the cross blend |
| Air control extras | The run flag (a running jump with an off-axis stick counts as forward), the 0.25 stick scale, the platform velocity at takeoff and the blocked forward and back flags are not ported. The sideways rise (69.0 in the oracle against 64.0) is unexplained |
| Air step rate | The original applies the air damping (0.96 per call) and the 1 degree turn rule once per frame, and so does the port; the port steps once per rendered frame, so the air feel follows the frame rate until the fixed logic tick lands (BO2NX-37) |
| Landing | On the ground a landing clip moves Kain by the root of its reel, like any other clip; that is not compared with the oracle beyond the jump phases |
| Glide | Not implemented. `g_glide` (-128) and `termGlide` (160) are parsed only |
| Ledge fall | Not implemented. Walking off an edge only applies gravity and keeps the ground speed of the held input; the original fall state (FUN_00442800, FUN_00442ae0) has the same air control as a jump but its takeoff velocity was not traced |
| Wall slide and stop | Ported: the body is Kain's chest sphere (r 19.8 at `z+51.98`), and `CollisionWorld::slide_body` (FUN_005ccb60) sweeps it, stops at the earliest contact and slides the rest of the tick. On `sl01` the W run slides at 162 u/s on model 57's upper chamfer and stops at x = 326.38, y = 44.18 (oracle 326.35 / 44.14). The post-stop drift down the wall (oracle y 44.14 -> 42.84) is not modeled |
| Release slide | Ported with the root motion: the blend of `st_run2rdy` moves Kain 10 u in 0.2 s |
| Camera setups | The range, pitch, pivot height and yaw limit are those of one live setup; the code or data that builds the setups (including the jump setup with delay 0.1 and blend 0.6) was not traced. The range blend itself is ported |

## Animation state

Every movement state of Kain stores the index of an `animatio` graph state at
`KainState+0x458` and starts it on layer 0 of the animation instance
(`StartState`, vtable slot `+0x14`). The Wine oracle samples that field as
`anim`. The graph and its state machine are in
[`FORMATS/animatio.md`](FORMATS/animatio.md); the port is
`src/game/kain_animation.cpp` over `src/engine/anim_machine.cpp`.

| Index | State | Who starts it |
|---:|---|---|
| 0 | `st_ready` | the ready state (stored at 0x445e8f) |
| 1 | `st_run` | the run state enter, FUN_004458e0 |
| 59 | `st_back` | the walk-back state (stored at 0x446240) |
| 76 | `st_startjump` | the jump state enter (FUN_004436e0; FUN_00443d60 for the back and side jumps), which also sets a 0.2 s timer |
| 78 | `st_fallingup` | the falling state while the vertical velocity is not negative (FUN_00442800, FUN_00442ae0) |
| 79 | `st_fallingdn` | the falling state once it is negative |
| 86 | `st_normal_landing` | the landing state |
| 87 | `st_run_landing` | the landing state |
| 13, 14 | `st_ready_cm`, `st_walk_cm` | combat mode: this is what Shift and Shift+W start |

Read from the oracle, not from the code: a landing with forward held is
`st_run_landing` and any other is `st_normal_landing`; the state lasts until
its sequence ends (0.5333 s and 0.311 s in the graph, 25 to 27 and 13 to 14
samples of 20 ms in the captures); turning in place keeps `st_ready`.

The run state writes the run clip rate every frame (FUN_00445ba0), so
`cl_run` plays at 1.15 instead of its node speed 1.5.

The jump state leaves `st_startjump` from its update (FUN_00443a50), which
hands over to the falling state when the 0.2 s timer has run out, when the
vertical velocity is no longer positive, or when the hold boost is over: its
start time has passed and the jump button is up, or its end time has passed
too. A tap therefore leaves at the boost start and a held jump at the timer,
which is what the captures show (3 to 5 samples for a tap, about 10 held).

Gate: `bo2nx --replay` writes the port's index as the `anim` column and
`tools/oracle/compare_state.py` counts the rows where it equals the oracle's.

| Capture | Rows that match | Where it differs |
|---|---:|---|
| `wine_probe_20261111` | 2,999 / 3,109 (96.5%) | `run` 31/139: Shift is combat mode, which the port does not model; `jump` 160/161 |
| `wine_camera_1` | 3,608 / 3,611 (99.9%) | `jump` 152/155 |
| `wine_air_1`, first five jump phases | 832 / 981 (84.8%) | the landing choice and the jump physics (the port's position leaves the route in these phases); from `air_w` on it is far off, so the later phases say nothing about the animation |

W is 137/137, S 50/51 and A/D 51/51 in `wine_probe_20261111`.

Open:

- Combat mode, the attacks and the state the landing state picks, which is
  read from the captures here.
- In FUN_00443a50 the timer does not end the state while `KainState+0x45c`
  is 2, and the boost exit waits for a flag (`+0x5c` of the state) that the
  movement step clears; the port has neither.
- What is drawn: `KainActor` (`src/game/kain_actor.cpp`) skins the model with
  the reel of the layer 0 clip at the machine's reel time, and while a clip
  blends in it goes from the pose drawn last to the clip's first frame
  (`FORMATS/models.md`, Pose blend). In the run the pose is the cross blend
  of the run and the walk clips by the stick magnitude
  (`FORMATS/animatio.md`, Kain's run state); with a keyboard the stick is
  full and the walk has no weight. The transition filters, the lean of layer
  3 and the layer masks are not ported, and no capture has a partial stick to
  compare the mix against.

## Movement collision (BO2NX-72, decoded live)

The original has no body cast against the level. A live gdb trace of the W
run (`wine_trace_20261111`) shows three queries per tick:

| Query | Shape | Origin | Extent |
|---|---|---|---|
| Ground/step probe | sphere r=1 | feet, `z+16` | down to `z-24` |
| Run probe | sphere r=8 | chest, `z+48` | 32 u forward |
| Camera pull-in | sphere r=10 | `z+76` | backwards |

The run probe (`FUN_0042ee60`) does **not** block the move: on a terrain hit
it compares the hit normal with the tunedata threshold (A+0x614, 0.9) and sets
`KainState+0x588` bit `0x100`. `FUN_0041f460` calls `FUN_0041ee60` for two
per-direction records (`+0x545`/`+0x558`) reading the blocker pointers at
`+0x450`/`+0x454`, but a live trace (`wine_blocker_20261112`) shows both
pointers stay 0 and the record flag stays 0 during the whole W run: the
response is a no-op, and the pointers are never written. The second record
consumer, the wall projection (`FUN_0041f460` at `0x41f54b`, `FUN_0041f7d0`
at `0x41f8f8`, guarded by the state mode `+0x45C == 1` and the normal at
`+0x478`), is also inactive: a live trace (`wine_wall2_20261112`) reads mode 0
and normal (0,0,1) for the whole run.

A hardware watchpoint on the position (`wine_poswatch2_20261111`) gives the
writers per tick:

| Writer | Fields | Role |
|---|---|---|
| `FUN_0041f460` (`0x41f640`) | x, y, z | the state's target position (root delta, unblocked) |
| `FUN_0070eac0` (`0x70ec39`) | x, z, y | the "set position" message; runs after and wins |
| `FUN_00432030` (`0x43227a`) | z | integrator |
| `FUN_00430820` (`0x430b4a`) | z | ground snap |

During the W run the target is the position plus the animation root delta
(+2.15 in x per tick at 230 u/s). At the slide the "set position" message
(`FUN_0070eac0`, message type `DAT_009105cc`; it copies the payload's
`+0x30..+0x38` into the instance `+0xB4..+0xBC`) corrects the target by
(-1.05, -1.05) per tick: the 2.15 u move projected on a plane with normal
(0.707, 0.707, 0), i.e. 230 cos 45 = 162 u/s along (1, -1), the gate value.
The stop at x = 326.35 is the same message path clamping the position while
the target keeps advancing. The behaviour's own position (`behavior+0x190`)
follows the same path, and the velocity at `behavior+0x19C` is a finite
difference of that position (`FUN_00431df0`), so it adds no information.

The run probe (`FUN_0042ee60`) only raises the wall flag
(`PlayerController::wall_hug`); it never clamps the move. Its shape cast is the
engine `swept_sphere` (`FUN_005be9a0`). The run probe uses the general shape
cast `FUN_0062d8f0` and its leaf `FUN_0062dfc0`, not `FUN_0062ec30` (a live
trace shows the latter only sees the camera cast); its leaf test is per object
(query flag 2 against the class byte at `+0x38`, bit 0).

The body collision is the collision manager, not a movement probe. A live
dump of the mover proxy (`wine_shape2_20261113`, object `a`) shows its
container is Kain's `collmodc`: three solid spheres at `z 51.98 / 49.61 /
23.60`, radii `19.8 / 19.297 / 15.0`, and the world AABB at proxy `+0x58` is
only the broad-phase bound. The manager's query (`FUN_005ccb60` passes
`{2,1,0}` to `FUN_005cc130`; `FUN_0062ef20` tests `sphere.flags & 1`) only
passes the **chest sphere**: the other two records have flags 0. The resolver
sweeps that sphere to the earliest contact, resolves it and slides the rest
of the tick. `CollisionWorld::slide_body` ports that loop (swept sphere,
normal projection, up to 8 passes, a 0.01 gap, and a depenetration pass for
the next triangle of a curved bevel). The replay rows are 20 ms while the
original integrates at about 100 Hz, so the move is split into 2.3 u
substeps; without that the body cuts the corner of the bevel.

Two filters were needed to match the oracle's contacts:

- The packed `collmodc` triangle record keeps the third vertex index in 9
  bits: its top two bits live in the low bits of the attribute byte
  (`FUN_0062dfc0`: `v0 = (record >> 18) & 0x1ff`, `v1 = (record >> 9) &
  0x1ff`, `v2 = record & 0x1ff`). The port kept only 6 bits, which invented
  triangles. The parser now restores the high bits.
- The packed record's 2-bit category (`attribute >> 5`) marks a face-only
  triangle as edges only when it is 2 (`FUN_005ddc90` skips the face test).
  The ramp at (274, 59, 254) in `sl01` is such a triangle; the port now skips
  its face in the sphere and ray queries.

The cell broad phase was also wrong: a cell's `terrains` list references the
`cellmdls` models it draws, so it is a hint, not an owner. Model 649 instance
635 (the W stop) is listed by cell 5 but sits inside cell 0, so the body
ignored it and stopped 1.8 u late on another wall. `instance_cells` now
prefers a cell whose bounds contain the instance and that lists its model,
then the bounds alone, then the list. The portal **symbols** still link the
neighbour graph (`sl01` cell01 symbol 925, portal 930 <-> 931 = cell02).

On `sl01_basic` the W run follows the oracle's path: the slide starts at
the chamfer with 162 u/s at -45 degrees, and the stop is x = 326.38,
y = 44.18 (oracle x = 326.35, y = 44.14). `after_w` goes from mean 13.31 /
max 29.18 to mean 0.398 / max 2.497 with the start of the run fitted to the
capture (100 u/s for 0.18 s). With the ground movement taken from the root
motion of the graph (BO2NX-82) the distance is the oracle's, but the replay
moves one 20 ms capture row per tick and its inputs are stamped up to a row
late, so the switch to 230 u/s lands about a row later: `after_w` is **mean
1.829 / max 5.699** and `after_s` **mean 0.940 / max 1.648**. That is the
baseline now. The oracle's post-stop drift down the wall (y 44.14 ->
42.84) and the S/A/D phases that follow it are still open (BO2NX-37). Entity
collision is ruled out: `staticcollision` / `collisionresponse_stop` are
parsed and `sl01` has 0 static collision entities.
