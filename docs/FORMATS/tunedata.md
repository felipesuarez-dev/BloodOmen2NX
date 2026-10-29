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

| B offset | Field | Value | Consumers |
|---|---|---|---|
| 0x00C | Stick deadzone | 0.1 | FUN_00425520, FUN_0041ecf0 (copy step not traced) |
| 0x9FC | `normalJump` block, 9 floats | see below | FUN_00451d00 builds the jump from it |
| 0xAC8 | Turn rate, Ready | 2.5 rad/s | copied to A+0x18 by FUN_00428fe0; FUN_00445e70 passes it to FUN_0041ecf0 |
| 0xACC | Turn rate, Run | 2.5 rad/s | copied to A+0x1C; FUN_00445930 passes it to FUN_0041ecf0 |
| 0xAD0 | Turn rate, WalkBack | 2.5 rad/s | copied to A+0x20; FUN_00446270 passes it to FUN_0041ecf0 |
| 0xB14 | Run clip rate | 1.15 | copied to A+0x40 by FUN_00428fe0; FUN_00445ba0 uses it as the playback rate of the run clip at full stick. The port multiplies the `kain/run` root speed (200 u/s) by it to get 230 u/s (`docs/FORMATS/animreel.md`) |

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
| 0x9FC | `g_up` | -2500 | Gravity while vz >= 0 (FUN_00442ae0) |
| 0xA00 | `g_down` | -700 | Gravity once vz < 0 |
| 0xA04 | `g_glide` | -128 | Gravity while gliding (not used yet) |
| 0xA08 | `termFall` | 512 | Fall speed cap (FUN_00441f20) |
| 0xA0C | `termGlide` | 160 | Glide speed cap (not used yet) |
| 0xA10 | `h1` | 64 | Apex of a tapped jump |
| 0xA14 | `h2` | 128 | Apex target of a held jump |
| 0xA18 | `tMin` | 0.066 | Start of the hold boost window, seconds after takeoff |
| 0xA1C | `tMax` | 0.3 | End of the hold boost window |

FUN_00451d00 turns the block into the jump model. The port computes the same
values in `derive_jump` instead of storing them:

| Derived | Value | Formula |
|---|---|---|
| Launch vz | 565.685 | `sqrt(2 * h1 * abs(g_up))` |
| Boost acceleration | 1998.94 | Iterative solve for the window that lifts the apex to `h2` |
| Tap airtime | 0.6539 s | `sqrt(2 * h1 / abs(g_up)) + sqrt(2 * h1 / abs(g_down))` |

`re/decomp/kain_movement/jump_model.py` is a local hand port of the same
function used to cross-check these numbers.

## Observed, not consumed yet

| B offset | Shape | Notes |
|---|---|---|
| 0xA20, 0xA38, 0xA74 | 6 floats each | FUN_00452030 builds a jump variant from each. The derived launch speeds are 146.8, 110.1 and 97.9. They are likely the air-control launch speeds; not verified and not used by the port |
| 0xA50 | 9 floats, same shape as `normalJump` | Parses cleanly as a second jump block (-3000, -500, -96, 512, 160, 64, 64, 0.05, 0.06). Its consumer is not identified |

## Open questions

- What the 6,195-byte tail holds.
- Which state uses each of the three turn rates. All three are 2.5, so the
  port maps them by name (Ready idle, Run forward, WalkBack backward).
- Whether the size field is the blob length or a count of the fields the
  engine fixes up at load time.
