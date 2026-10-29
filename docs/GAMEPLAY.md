# Gameplay translation

Inventory of the systems to recover. Status is `todo` until a function is
translated and verified.

| System | Status | Notes |
|---|---|---|
| Boot and front-end | todo | Logos, menus, options, language |
| Level loading and streaming | todo | Container readers, level table |
| Camera | partial | Follow pose measured in the oracle; combat and scripted cameras todo. See [Kain movement](#kain-movement) |
| Player movement | partial | Ground turn, ground speeds, jump and fall cap; air control, glide, climb and swim todo. See [Kain movement](#kain-movement) |
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
gravity and a hold boost, and a rigid follow camera sits behind him.
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
| Run button | `run` is Shift and gives 200 u/s, which is slower than plain W (230). That is what the oracle measured (unverified, see above); the name is kept from the input layer |

### Pending

| Item | State |
|---|---|
| Root motion | Decoded for the `kain/run` and `kain/walk_backwards` clips and used for the W and S speeds above. The `animatio` graph (clip nodes, blending) is not decoded, so the walk-back clip node speed 1.7 is a constant |
| Partial stick | At stick `s < 1` FUN_00445ba0 blends slot 0 (weight `s`) with slot 1 (weight `1 - s`, rate `(s + 1) * A+0x40`). Slot 1's clip is not identified, so analog speed scaling is not ported |
| Start phase, Shift+W | The 100 u/s start phase (0.18 s) and the 200 u/s of Shift+W are measured constants with no decompiled source yet |
| Air control | Not implemented: in the air Kain keeps the ground speed of the held input. The launch speeds 146.8, 97.9 and 110.1 and the air acceleration are likely from FUN_00452030 and FUN_004423b0, not verified |
| Glide | Not implemented. `g_glide` (-128) and `termGlide` (160) are parsed only |
| Ledge fall | Not implemented. Walking off an edge only applies gravity |
| Collision size | Body radius and eye height are provisional, see above |
| Release slide | The short slide after releasing W is not modeled |
| Camera lag | Not modeled: the port camera is rigid. Needs the catch-up of FollowingCreatureCameraImpl (update FUN_004088c0, setup acceleration limits) |
