# Gameplay translation

Inventory of the systems to recover. Status is `todo` until a function is
translated and verified.

| System | Status | Notes |
|---|---|---|
| Boot and front-end | todo | Logos, menus, options, language |
| Level loading and streaming | todo | Container readers, level table |
| Camera | partial | Follow pose measured in the oracle; combat and scripted cameras todo. See [Kain movement](#kain-movement) |
| Player movement | partial | Ground turn, ground speeds, jump, air control and fall cap; glide, climb and swim todo. See [Kain movement](#kain-movement) |
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
gravity and a hold boost, steers in the air, and a rigid follow camera sits
behind him.
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
| W, first 0.18 s | 100 u/s (= 200 * 0.5; the start phase is unexplained) | measured, oracle |
| W, after 0.18 s | 230 u/s = run root speed 200 * run clip rate 1.15 | FUN_00445ba0; oracle-confirmed: 230 measured. Periodic peaks in `external/captures/wine_20261029_0110/state.csv` (W down at mono_ms 41388469): world \|vel_xy\| reads 300 at about +191, +892 and +1572 ms, spaced by the run cycle 0.8 / 1.15 = 0.696 s. That slot 0 is the run clip is inferred from this match |
| Shift+W | 200 u/s (= 200 * 1.0, unexplained), no start ramp. Unverified: Kain was probably blocked for part of that phase | measured, oracle (`docs/ORACLE_WINE.md`) |
| Walk-back root speed | 62.5 u/s backward | `kain/walk_backwards` animreel, node 0 translation x: -2.0833 to -100 over 47 frames (1.5667 s) |
| S | 106.25 u/s = walk-back root speed 62.5 * clip node speed 1.7, yaw unchanged | The 1.7 is the `speed` of the walk_backwards clip node in the `animatio` graph. The graph is not decoded, so the node is located by its fields (layout from the debug printer FUN_005e1c20, "animation=%s in=%f out=%f speed=%f" at +0, +4, +8, reel reference at +0xC, resolved by the fixup FUN_005e1b60): in the `kain` animatio entry the node at offset 0x5504 reads in 0.0, out 1.5667, speed 1.7, reel 3 (= index 1 << 1 \| 1), and 1.5667 is the walk_backwards duration (47 / 30). The neighbours read out 1.5333, speed 1.5, index 0 (walk) and out 0.8, speed 1.5, index 2 (run), matching the entry's dependency order walk, walk_backwards, run. The oracle confirms it: 62.5 * 1.7 = 106.25 u/s. The port keeps 1.7 as a constant |
| Release | Stops at once | measured, oracle shows a short slide that is not modeled |

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

| Value | Number | Source |
|---|---|---|
| Distance behind Kain, along his yaw (caught up) | 190.9 | measured, oracle |
| Height above Kain (caught up) | 96.1 | measured, oracle |
| Pitch | -6 deg (looking slightly down) | measured, oracle |
| Scripted range | 192 (CameraSetup, follow+0x50) | FUN_004074c0 |

The port camera is rigid: it always sits at the caught-up offsets above. The
original lags Kain's yaw; at the end of the `after_a` phase of the oracle it
was 117 back and 88 high. The eye is pulled in along the line back from Kain
when the level collision mesh is in the way.

### Port-only behaviour

These have no original source. They are development aids or placeholders.

| Item | Note |
|---|---|
| Mouse and right-stick look | Adds to yaw and to the camera pitch offset. Not part of Kain's controls |
| Body radius 0.4, eye height 1.7, body clearance 0.05 | Provisional. They predate the Kain tuning, are in meter-like units and are about 100 times too small for game units |
| Attack range 2.2, radius 1.0, cooldown 0.45 s | Provisional, same reason |
| Move sub-steps | A tick of travel is up to 230 / 60 = 3.8 units, ten times the body radius, so a move is walked in steps of one radius to keep thin walls solid |
| Forward and back | Only on or off past the deadzone; analog speed scaling is not modeled. The platform layer also zeroes sticks below 0.25 before the deadzone applies |
| Step cap | A step is cut to 0.1 s (the cap the SDL loop already uses) and a move to 64 sub-steps, so a stalled frame cannot tunnel through a wall or the floor |
| Deadzone edge | Turning starts at exactly the deadzone (FUN_0041ecf0 compares `deadzone <= ax`); the forward/back gate is not traced and the port uses past-the-deadzone |
| Run button | `run` is Shift and gives 200 u/s, which is slower than plain W (230). That is what the oracle measured (unverified, see above); the name is kept from the input layer. In the oracle prefix Shift is the `autoface` row, not a run (`docs/ORACLE_WINE.md`, Input bindings), so this speed is probably that action |

### Pending

| Item | State |
|---|---|
| Root motion | Decoded for the `kain/run` and `kain/walk_backwards` clips and used for the W and S speeds above. The `animatio` graph (clip nodes, blending) is not decoded, so the walk-back clip node speed 1.7 is a constant |
| Partial stick | At stick `s < 1` FUN_00445ba0 blends slot 0 (weight `s`) with slot 1 (weight `1 - s`, rate `(s + 1) * A+0x40`). Slot 1's clip is not identified, so analog speed scaling is not ported |
| Start phase, Shift+W | The 100 u/s start phase (0.18 s) and the 200 u/s of Shift+W are measured constants with no decompiled source yet |
| Air control extras | The run flag (a running jump with an off-axis stick counts as forward), the 0.25 stick scale, the platform velocity at takeoff and the blocked forward and back flags are not ported. The sideways rise (69.0 in the oracle against 64.0) is unexplained |
| Glide | Not implemented. `g_glide` (-128) and `termGlide` (160) are parsed only |
| Ledge fall | Not implemented. Walking off an edge only applies gravity and keeps the ground speed of the held input; the original fall state (FUN_00442800, FUN_00442ae0) has the same air control as a jump but its takeoff velocity was not traced |
| Collision size | Body radius and eye height are provisional, see above |
| Release slide | The short slide after releasing W is not modeled |
| Camera lag | Not modeled: the port camera is rigid. Needs the catch-up of FollowingCreatureCameraImpl (update FUN_004088c0, setup acceleration limits) |
