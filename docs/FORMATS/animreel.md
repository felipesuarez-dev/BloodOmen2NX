# `animreel`: animation clips

An `animreel` BIG entry holds one animation clip: a set of per-node channels
sampled once per frame. The kain container (`resources/levels/kain/kain.big`)
has 134 of them, named `kain/run`, `kain/walk`, `kain/walk_backwards` and so
on. All 134 parse with the layout below and consume their blob exactly.

Status: **decoded** for inner version 6 (every Kain clip). Reader:
`tools/bo2fmt/animreel.py`; C++ port: `src/engine/anim_reel.cpp`.

Sources are `bo2.exe` functions. Facts not read from the code are marked
*inferred*.

## Layout

All fields are little-endian.

| Offset | Type | Field | Source |
|---|---|---|---|
| 0x00 | char[8] | `animreel` tag | FUN_0066aa20 |
| 0x08 | u32 | payload size, header included (7248 for `kain/run`) | |
| 0x0C | u32 | chunk version, 4 in every Kain clip | FUN_0066aa20 accepts 1, 3, 4, 0x10001, 0x20001; version 4 calls FUN_006689a0 with -1, which reads the inner version |
| 0x10 | u32 | inner version, 6 | FUN_006689a0 |
| 0x14 | u32 | reel+0x08, unknown (0 in Kain clips) | FUN_00668700 |
| 0x18 | u32 | event count `N` | FUN_00668700 |
| 0x1C | `N` x 8 | events: u32 symbol id, f32 frame | |
| | f32 | frames per second (reel+0x0C), 30 | |
| | u32 | frame count (reel+0x10) | |
| | u32 | descriptor count `D` (reel+0x40) | |
| | u32 | blob size (reel+0x48) | |
| | bytes | sample blob | |
| | `D` x 8 | descriptors: u16 node, u8 type, u8 component, u32 value | |

The payload ends exactly after the descriptors. Only inner version 6 is
implemented; other versions are rejected.

## Channels

A descriptor value decides how the channel is stored (FUN_0063c480 compiled
path, FUN_0066a6d0, FUN_0066a680):

- Bit 0 set: a constant. The float is the value's bit pattern as is. The low
  mantissa bit is the tag and is not cleared. The channel takes no blob bytes.
- Bit 0 clear: a codec id. The channel's data is read from the blob
  sequentially in descriptor order. Constants consume nothing.

| Codec | Class | Constructor | Evaluator | Blob data | Size |
|---|---|---|---|---|---|
| 0 | AnimChanUC | FUN_0067ad20 | FUN_0067aaf0 | `frames` x f32 | `frames * 4` |
| 8 | AnimChanLSC | FUN_0068b1f0 | FUN_0068aed0 | f32 offset, f32 scale, f32 start, u16 `K`, `K` x u16 segments | `align4(K * 2 + 2) + 12` |
| 12 | AnimChanLC | FUN_0068aca0 | FUN_0068aa90 | f32 offset, f32 scale, `frames` x u8 | `align4(frames + 8)` |
| 16 | AnimChanStream | FUN_006712c0 | not decoded | 4 bytes, opaque | 4 |

Any other even value is invalid. The blob must be consumed exactly; this
holds for every Kain clip.

Evaluation. The time is a frame index; between two frames every codec is
linear, so the port decodes one float per frame and `AnimChannel::sample`
interpolates the two neighbours (FUN_0067aaf0 for UC, FUN_0068aa90 for LC; the
LSC segments are linear over whole frames, so the rule is exact there too).
The port clamps the frame to the clip. At whole frames:

- UC: the sample for that frame.
- LC: `byte * scale + offset`.
- LSC: segment `s` covers `len = s >> 11` frames and adds
  `delta = (s & 0x7FF) * scale + offset` linearly over them. The value at a
  frame is `start`, plus the deltas of every full segment before the frame,
  plus `frames_into_segment * delta / len`. Past the last segment the port
  clamps to it and keeps extrapolating (a port choice): in FUN_0068aed0 the
  loop advances to index K and reads the u16 one past the segment array
  (padding or the next channel's data) before its `K <= index` check. A
  zero-length segment divides by zero in the original; the port returns the
  running value. No Kain clip reaches either path.

Limits: the frame count must be 1..65535, because the codec constructors
(FUN_0067ad20, FUN_0068aca0, FUN_0068b1f0) take it as a ushort and the reel
stores a short (the reel+0x12 copy in FUN_0063c480); every Kain clip has
1..241 frames. The port also caps the decoded samples of one reel at 16M
floats so small LSC channels cannot amplify a file; that cap is port-only.

## Duration

`(frames - 1) * (1 / 30)`. FUN_0068ada0 and FUN_0068a9b0 use the double
constant 1/30 at 0x872498, not the fps field.

## Descriptor keys

A descriptor is addressed by the key `node << 16 | type << 8 | component`.
The compiled reel keeps the descriptors in file order with the three fields
swapped into that key (FUN_0063c480), and FUN_0063ce20 bisects them, then
walks forward from the last hit.

The clip sampler binds nine channels per bone (FUN_00638920), in this order:

| Type | Components | Meaning | Value without a channel |
|---|---|---|---|
| 0 | 0, 1, 2 | Rotation about x, y, z, in radians | 0 |
| 1 | 0, 1, 2 | Translation x, y, z | The bone pivot (FUN_00637900, FUN_005e26c0) |
| 2 | 0, 1, 2 | Scale x, y, z | 1 |

`node` is the bone id (`boneIdx` of the `modlskel` record), not the bone's
position in the array: the runtime bone record keeps `index << 16 | id` as its
key (FUN_005e27e0). In `kain/run` the 102 plain nodes are 102 of the model's
104 bone ids. A clip may name a bone the model does not have (nodes 95 and 96
in some Kain clips); nothing looks it up, so it is ignored. How the values
become bone matrices is in [`models.md`](models.md).

Nodes with the high bits set are auxiliary records, not bones:

| Node | Types | Reader |
|---|---|---|
| `0x1000 \| n` | 7 and 8, components 0..2 | FUN_00639230 |
| `0x2000 \| n` | 10, 12 and 18, components 0..2 | FUN_006393c0 |
| `0x3000 \| n` | 14, 15, 16 and 17 | FUN_006395f0 |
| plain | 20, components 0, 1, 2, 4, 5, 6 | FUN_006397a0 |

Their meaning is open. Over the 134 Kain clips the bone keys are sorted and
unique, so the port's first-match scan (`find_channel`) returns what the
bisection returns. Twelve clips (`kain/walk`, `kain/walk_backwards`, the
`claw_block_*` and `combat_jump_*` clips) repeat a key, always on a `0x1000`
node. No bone key uses the stream codec.

## Root motion

Node 0, type 1, components 0, 1, 2 are the local x, y, z translation of the
root. Missing components are 0. Local x is forward. The skeleton does not
apply the root's channels: the bone of index 0 gets an identity matrix
([`models.md`](models.md)), so this translation only moves the entity.
FUN_0041f7d0 takes the
root delta from the animation system (FUN_0041fb00), rotates it by the
instance rotation and adds it to the position.

Per cycle displacement is `value(last frame) - value(first frame)` and the
root speed is the displacement over the duration.

| Clip | Frames | Duration | Root x displacement | Root speed |
|---|---:|---:|---:|---:|
| `kain/run` | 25 | 0.8 s | 160 | 200.0 u/s |
| `kain/walk` | 47 | 1.5333 s | 86.4010 | 56.3485 u/s |
| `kain/walk_backwards` | 48 | 1.5667 s | -97.9167 | -62.5 u/s |

The root is matched as node == 0 exactly. FUN_0063c480 tests bit 0x1000 of
the descriptor's u16 node; the 2916 Kain channels with node >= 0x1000 are all
auxiliary records of types 7 and 8 (above), so a flagged node is never the
root.

How the game turns these into ground speed is in
[`../GAMEPLAY.md`](../GAMEPLAY.md): run speed is the root speed times the run
clip rate (FUN_00445ba0), walk-backwards is the root speed times the clip
node speed from the `animatio` graph ([`animatio.md`](animatio.md): the node
is `cl_back`, speed 1.7; the run state overwrites the node speed instead of
multiplying it).

## Clips inside a cell group

A cell group stores the clips of its animated models in its `cellanim`
section, without the chunk header: each one starts at the inner version
(offset 0x10 of the layout above). `FUN_006689a0` reads both. See
[`models.md`](models.md#cellanim); the port reads one with
`parse_anim_reel_body`.

## Open questions

- The event symbol ids (footsteps in Kain's clips: four events at frames 5
  and 17 of `kain/run`).
- The u32 at reel+0x08.
- What the auxiliary records carry: the flagged nodes (types 7, 8, 10, 12,
  14..18) and types 19 and 20 on the root (components 0, 1, 2, 4, 5, 6).
  Across the Kain clips the types in use are 0, 1, 2, 7, 8, 19 and 20.
- What AnimChanStream carries.
- Inner versions other than 6 and chunk versions other than 4.
- The UC evaluator skips the interpolation when its sampler cache carries a
  flag (FUN_0067aaf0, cache `+0x1c == 1`); the port always interpolates.
- The `animatio` graph that references the reels is decoded; see
  [`animatio.md`](animatio.md). How the engine blends two clips on the
  skeleton (the sampler with vtable 0x86f91c) is not ported.
