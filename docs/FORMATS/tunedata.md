# `tunedata`

Status: **partially verified**. The container layout, the Kain movement
fields and the health tables below are read by `src/game/kain_tuning.cpp` and
checked against the real file. Most of the blob of the 18,172-byte `kain`
entry is not decoded.

## Where it lives

`tunedata` is a `.big` entry type (see [`big.md`](big.md)). The one that
drives Kain's movement is the entry named `kain` in
`resources/levels/kain/kain.big`. The variants `kainc.big`, `kaind.big`,
`kaine.big` and `kainf.big` carry identical movement fields. Smaller
`tunedata` entries (`kainactor`, 296 bytes, and others) are separate tables.

## Container layout

FUN_007706c0 reads an entry (FUN_007705d0 is the writer): a size, the data
blob `B`, a list of resource names and a list of pointer fixups. All numbers
are little-endian. Every `u32` after the blob is read at the next 4-byte
boundary (FUN_006a09a0); the padding bytes are `0xFF` in the shipped file.
A field at **B offset** `k` is at file offset `4 + k`.

| Part | Offset in `kain` | Notes |
|---|---|---|
| Size | 0x0000 | `u32`, `0x2EC5` (11,973): the length of the blob |
| Blob `B` | 0x0004 | The tuning structure and the arrays and strings its pointers reach |
| Name count | 0x2ECC | `u32`, 111 |
| Names | 0x2ED0 | Each one a `u32` and a zero-terminated string (FUN_00770660, FUN_006a0a40): the resources the structure names (`nexus_stone`, `bloodsplash/bloodspurt`, ...). The `u32` takes the values 1 to 4; its meaning is not decoded |
| Fixup count | 0x3F38 | `u32`, 248 |
| Fixups | 0x3F3C | Each one `{ u32 where, u32 target }` (FUN_00770690); the list ends the entry at 0x46FC |

A pointer field of the blob holds a stale address in the file. After the
read, FUN_00780580 walks the fixups and stores `B + target` at `B + where`
when both offsets are inside the blob. So a pointer at B offset `where` leads
to B offset `target`: the fixup `0x8B0 -> 0x22A4` is the lore level table.

The field names of the structure are in the game data: `kain.big` carries the
header `src/etemplates/kaintune.h` (a `strmfile` entry) with every structure
and a description of each field.

`parse_kain_tuning` accepts an entry when the size field reaches past the
last movement field (B+0xB18) and the data is at least `4 + size` bytes long.
An entry that ends with the blob has no fixups and its tables stay empty; a
list or a table that runs past its data is an error.

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

## `healthParams` (B+0x898)

`KainHealthParams` in `kaintune.h`. The port reads the fields below; the six
floats before them (`healthIncreaseRate` 50, `loreIncreaseRate` 20,
`thirstDrainRate` 3, `thirstMinPercent` 50, `thirstDrainTime` 60,
`thirstDrainAmount` 20) are not used yet.

| B offset | Field | Value | Consumers |
|---|---|---|---|
| 0x8B0 | `lorePerLevelArray` | pointer, fixed up to B+0x22A4 | FUN_00420ef0, FUN_00420700, FUN_004208a0, FUN_00421020 |
| 0x8B4 | `numLoreLevels` | 26 | FUN_00420ff0 |
| 0x8B8 | `defaultHealth` | a `KainDebugHealth` with no name, `loreLevel` 15 | FUN_004256c0 |
| 0x8DC | `debugHealth` | pointer, fixed up to B+0x2374 | FUN_004256c0 |
| 0x8E0 | `numDebugHealths` | 17 | FUN_004256c0 |

A lore level record (`KainLoreLevels`, 8 bytes) is `{ f32 maxLore, f32
maxHitPoints }`. The lore level is the index of the record. The table starts
`(6, 30)`, `(30, 60)`, `(120, 90)`, `(240, 120)` and ends `(950, 500)` from
level 20 on; the hit points grow by 30 per level up to 480 at level 15.

- FUN_00420ef0(level, lore) enters a level: the maximum lore is `maxLore` of
  the record and the lore is clamped to it. It ends with FUN_00420700, which
  sets the health and the maximum health to `maxHitPoints` of the record.
- FUN_004208a0 returns `maxHitPoints` of the last record (500) and
  FUN_00421020 its `maxLore` (950). The HUD divides the current maximum by
  them to size the vials.

A start record (`KainDebugHealth`, 0x24 bytes) is `{ char* name, i32
loreLevel, i32 x 7 }`; the seven flags say which dark gifts Kain owns (charm,
fury, rupture, speed, stealth, superJump, telekinesis). When Kain is created
without a saved state, FUN_004256c0 takes the record whose name equals the
level name it is given, or `defaultHealth` when none does, and enters its
lore level with 0 lore. The records: `slums` 0, `smugglersden/regionfiles` 3,
`lowercity` 5, `uppercitynew` 6, `keep` 7, `industrial quarter` 8, `canyons`
9, `eternal prison` 10, `device` 11, `wharves/regionfiles` 12, `hyldencity`
13, the five boss arenas (4, 6, 9, 10, 13) and `johnsworld` 0. So Kain enters
the slums with 30 hit points and room for 6 lore.

The port: `KainTuning::lore_levels`, `start_lore_levels`,
`default_lore_level`; `set_lore_level`, `start_lore_level`, `top_max_health`
and `top_max_lore` in `src/game/kain_vitals.cpp`. Not ported: the gift flags,
the growth of the shown values toward their targets (FUN_004210b0) and the
level-up (FUN_00420ab0).

## Open questions

- Which string the game passes to FUN_004256c0 as the level name (the caller,
  FUN_0042af90, takes it from the level record it is created with).
- What the `u32` in front of each resource name means.
- Which state uses each of the three turn rates. All three are 2.5, so the
  port maps them by name (Ready idle, Run forward, WalkBack backward).
