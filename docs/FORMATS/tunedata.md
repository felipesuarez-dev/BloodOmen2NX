# `tunedata`

Status: **partially verified**. The container layout and the Kain movement
fields below are read by `src/game/kain_tuning.cpp` and checked against the
real file. The rest of the 18,172-byte `kain` entry is not decoded.

## Where it lives

`tunedata` is a `.big` entry type (see [`big.md`](big.md)). The one that
drives Kain's movement is the entry named `kain` in
`resources/levels/kain/kain.big`. The variants `kainc.big`, `kaind.big`,
`kaine.big` and `kainf.big` carry identical movement fields. Smaller
`tunedata` entries (`kainactor`, 296 bytes, and others) are separate tables.

## Container layout

The extracted entry is a `u32` size field followed by the data blob `B`.
All numbers are little-endian. A field at **B offset** `k` is at file
offset `4 + k`.

| Part | Offset | Size | Notes |
|---|---|---|---|
| Size field | 0x0000 | 4 | `0x2EC5` (11,973) in `kain` |
| Blob `B` | 0x0004 | 11,973 | Covered by the size field |
| Tail | 0x2EC9 | 6,195 | Not decoded |

The entry is 18,172 bytes. `parse_kain_tuning` accepts an entry when the
size field reaches past the last movement field (B+0xB18) and the data is at
least `4 + size` bytes long; bytes after that are ignored.

## `kain` movement fields

All values are `f32`. B offsets are in hex; the value column is what the
shipped `kain` entry holds. Units are game units (about 1 cm) and seconds.
The field names in parentheses are those of the C struct that kain.big ships
as `src/etemplates/kaintune.h` (entry 0505, extracted as
`0505_src_etemplates_kaintune.h`); the struct reproduces the 11,973-byte blob
layout. The behaviour of each field is cited from the decompile (FUN_).

| B offset | Field | Value | Consumers |
|---|---|---|---|
| 0x00C | Stick deadzone (`airParams.controlThresh`) | 0.1 | FUN_00425520, FUN_0041ecf0 (copy step not traced) |
| 0x9DC | Air turn rate (`jumpParams.jumpTurnRate`) | 45 deg/s | FUN_00441f20: yaw change per unit stick in a forward or back jump. `glideTurnRate` (B+0x9E0, 45) and `glideMaxDist` (B+0x9E4, 320) are read there too and not ported |
| 0x9FC | `jumpParams.normalJumpParams`, 9 floats | see below | FUN_00451d00 builds the jump from it |
| 0xA20 | `jumpParams.forwardJumpParams`, 6 floats | see below | FUN_00428fe0 passes it to FUN_00452030 (result at A+0x47c, copied to behavior+0x7c by FUN_00428f80) |
| 0xA38 | `jumpParams.sideJumpParams`, 6 floats | see below | same, result at A+0x4dc (behavior+0xdc) |
| 0xA50 | `jumpParams.sideVertJumpParams`, 9 floats | see below | FUN_00451d00, result at A+0x440; FUN_004436e0 copies it over the current jump model for a sideways jump |
| 0xA74 | `jumpParams.backwardJumpParams`, 6 floats | see below | same, result at A+0x4ac (behavior+0xac) |
| 0xAC8 | Turn rate, Ready (`moveParams.standTurnRate`) | 2.5 rad/s | copied to A+0x18 by FUN_00428fe0; FUN_00445e70 passes it to FUN_0041ecf0 |
| 0xACC | Turn rate, Run (`moveParams.runTurnRate`) | 2.5 rad/s | copied to A+0x1C; FUN_00445930 passes it to FUN_0041ecf0 |
| 0xAD0 | Turn rate, WalkBack (`moveParams.backTurnRate`) | 2.5 rad/s | copied to A+0x20; FUN_00446270 passes it to FUN_0041ecf0 |
| 0xB14 | Run clip rate (`moveParams.moveSpeedMult`) | 1.15 | copied to A+0x40 by FUN_00428fe0; FUN_00445ba0 uses it as the playback rate of the run clip at full stick. The port multiplies the `kain/run` root speed (200 u/s) by it to get 230 u/s (`docs/FORMATS/animreel.md`) |

The chain is B -> A -> consumer. FUN_00428fe0 copies fields of the tunedata
into the runtime tune object A (`behavior+0x164`, for example B+0xAC8 is
`param_3[0x2b2]` and goes to A+0x18) and also keeps a pointer to the tunedata
at `behavior+0x168`. The consumers read A, not B. The port reads B directly
and skips A. The state handlers are matched to Ready, Run and WalkBack by
their A offsets in order; the state names come from the field order, not from
a traced state machine.

### `normalJump` block (B+0x9FC)

| B offset | Field | Value | Meaning |
|---|---|---|---|
| 0x9FC | `g_up` (`jAscentGravity`) | -2500 | Gravity while vz >= 0 (FUN_00442ae0) |
| 0xA00 | `g_down` (`jDescentGravity`) | -700 | Gravity once vz < 0 |
| 0xA04 | `g_glide` (`jFloatGravity`) | -128 | Gravity while gliding (not used yet) |
| 0xA08 | `termFall` (`jNormalMaxV`) | 512 | Fall speed cap (FUN_00441f20) |
| 0xA0C | `termGlide` (`jFloatMaxV`) | 160 | Glide speed cap (not used yet) |
| 0xA10 | `h1` (`jMinHeight`) | 64 | Apex of a tapped jump |
| 0xA14 | `h2` (`jMaxHeight`) | 128 | Apex target of a held jump |
| 0xA18 | `tMin` (`jMinTime`) | 0.066 | Start of the hold boost window, seconds after takeoff |
| 0xA1C | `tMax` (`jMaxTime`) | 0.3 | End of the hold boost window |

FUN_00451d00 turns the block into the jump model. The port computes the same
values in `derive_jump` instead of storing them:

| Derived | Value | Formula |
|---|---|---|
| Launch vz | 565.685 | `sqrt(2 * h1 * abs(g_up))` |
| Boost acceleration | 1998.94 | Iterative solve for the window that lifts the apex to `h2` |
| Tap airtime | 0.6539 s | `sqrt(2 * h1 / abs(g_up)) + sqrt(2 * h1 / abs(g_down))` |

`re/decomp/kain_movement/jump_model.py` is a local hand port of the same
function used to cross-check these numbers.

### `sideVertJump` block (B+0xA50)

Same nine fields as `normalJump`, values -3000, -500, -96, 512, 160, 64, 64,
0.05, 0.06. The heights are equal, so there is no hold boost, and the launch
speed is `sqrt(2 * 64 * 3000)` = 619.68. FUN_004436e0 copies this model over
the current jump for a sideways jump. The descent still uses the normal
`g_down` (FUN_00442ae0 reads A+0x408).

### Air steering blocks (B+0xA20, 0xA38, 0xA74)

Six floats each, named `MinDist`, `MaxDist`, `MinTime`, `MaxTime`, `Kd` and
`MaxLateralVel` in kaintune.h. FUN_00452030 derives the air control of one
jump direction from the block and the `normalJump` airtimes (0.6539 s for the
tap apex, 1.0181 s for the held apex):

| Derived | Formula | Forward (0xA20) | Back (0xA74) | Side (0xA38) |
|---|---|---|---|---|
| Fields (shipped) | `MinDist`, `MaxDist`, `MinTime`, `MaxTime`, `Kd`, `MaxLateralVel` | 96, 320, 0.03, 0.07, 96, 0 | 64, 192, 0.066, 0.25, 96, 0 | 72, 192, 0.066, 0.25, 96, 256 |
| Launch speed | `MinDist / min_airtime`, cut to `MaxDist / max_airtime` when that is smaller | 146.81 | 97.88 | 110.11 |
| Acceleration | `(MaxDist - max_airtime * launch) / ((ramp / 2 + max_airtime - MaxTime) * ramp)`, `ramp = MaxTime - MinTime`; zero when `abs(ramp) < 1e-5` | 4403.56 | 583.54 | 504.83 |
| Peak speed | `ramp * acceleration + launch` | 322.96 | 205.25 | 203.00 |
| Damping | `Kd * 0.01`, per tick | 0.96 | 0.96 | 0.96 |

`re/decomp/kain_movement/jump_model.py` (`variant`) cross-checks these
numbers. FUN_00452030 also stores a ninth value, a glide speed, that the port
does not use yet. The use of these numbers is in
[`GAMEPLAY.md`](../GAMEPLAY.md#air-control).

## Open questions

- What the 6,195-byte tail holds.
- Which state uses each of the three turn rates. All three are 2.5, so the
  port maps them by name (Ready idle, Run forward, WalkBack backward).
- Whether the size field is the blob length or a count of the fields the
  engine fixes up at load time.
