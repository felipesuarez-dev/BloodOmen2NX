# `animatio`: animation graphs

An `animatio` BIG entry is a state machine over animation clips. A graph holds
**clip nodes** (a reel plus in/out/speed), **sequences** (lists of clip nodes),
**states** (transitions whose actions play sequences or start other states) and
the tables they point at. Kain's graph is the `kain` entry of
`resources/levels/kain/kain.big`.

Status: **decoded** (container, clip nodes, states, transitions and actions).
Verified on all 4,081 `animatio` entries of the installed game: every one
parses and consumes its payload exactly (2,156 graphs with 86,920 clip nodes,
1,925 version 1 aliases). Reader: `tools/bo2fmt/animatio.py`.

Sources are `bo2.exe` functions; dumps are in `re/decomp/animatio_graph/`
(local, not committed). Facts not read from the code are marked *inferred*.

## Quick path

```sh
python3 tools/bo2fmt/animatio.py nodes --big <kain.big> --entry kain [--match walk]
python3 tools/bo2fmt/animatio.py states --big <kain.big> --entry kain --match st_run
python3 tools/bo2fmt/animatio.py nodes <extracted animatio entry>
```

`nodes` prints each clip node as `index name in out speed reel`. With `--big`
the reel name is resolved through the entry's symbol list; without it the
output shows `symbol N`.

Examples from Kain's graph (`speed` is the node field, not a measured rate):

| Node | Reel | in | out | speed |
|---|---|---:|---:|---:|
| `cl_run` | `kain/run` | 0 | 0.8 | 1.5 |
| `cl_walk` | `kain/walk` | 0 | 1.5333 | 1.5 |
| `cl_back` | `kain/walk_backwards` | 0 | 1.5667 | 1.7 |

## Container

The loader is `AnimationLoader` (vtable 0x8723c8, FUN_0066a850), which calls
FUN_005e9ae0. Everything is a stream of primitives (FUN_006a09a0, FUN_006a09e0,
FUN_006a0920, FUN_006a0960):

| Primitive | Size | Alignment |
|---|---:|---|
| int, float | 4 | 4 (pad before the value) |
| u16 | 2 | 2 |
| byte | 1 | none |

FUN_005e9ae0 reads two ints. The first must be 0. The second is the version:

- **0**: a graph, read by FUN_005e8ee0 into a 0x108-byte object (FUN_005e9d20).
- **1**: an alias list. An int count, then that many references
  (FUN_005e9840). The entry only names other resources (for example Kain's
  `nexus_stone`, 16 bytes). 1,925 entries.

A reference is two u16: an index into the entry's BIG symbol list for the name
and one for the type. The symbol list maps to the container `names` table;
FUN_006b48e0 does the index lookup.

### Version 0 stream

After the two ints FUN_005e8ee0 reads, in this order. "Object" is the offset
of the vector in the 0x108-byte graph object (a vector is `size, capacity,
ptr`, so the pointer sits 8 bytes after it); the fixup FUN_00642ea0 uses the
same offsets.

| # | Object | Content | Record | Reader |
|---:|---|---|---|---|
| 1 | +0x00 | int (flags, 0 in every Kain graph) | 4 | FUN_005e8ee0 |
| 2 | +0x04 | nine 32-bit values (opaque; *inferred* hashes stored as floats) | 36 | FUN_005e8ee0 |
| 3 | +0x28 | **states** | 36 | FUN_005eab00, FUN_005ea770, FUN_005e8e00 |
| 4 | +0x34 | **bindings**: condition operand pairs | 8 | FUN_005eaea0 |
| 5 | +0x40 | **sequences** | 12 | FUN_005eb000 |
| 6 | +0x4c | **chain**: clip/blend pairs | 8 | FUN_005eb180 |
| 7 | +0x58 | **clip nodes** | 36 | FUN_005eb2e0 |
| 8 | +0xc0 | second reel table | tag, 2 refs | FUN_005e9180 |
| 9 | +0x64 | **reels**: references | 4 | FUN_005eb5d0 |
| 10 | +0x74 | masks | 8 | FUN_005eb700 |
| 11 | +0x80 | byte table | 1 | FUN_005e8ee0 |
| 12 | +0x8c | layer types | 12 in memory, 2 ints in the stream | FUN_005eb900 |
| 13 | +0x98 | symbol to state map | u16 + int | FUN_005e9460 |
| 14 | +0xa8 | int table | 4 | FUN_005e8ee0 |
| 15 | +0xb4 | **name pool** (NUL-terminated strings) | 1 | FUN_005e8ee0 |
| 16 | +0xcc | objects (polymorphic) | class id + data | FUN_005e8ee0, FUN_0060f780 |
| 17 | +0xd8 | blends | 16 | FUN_005eba40 |
| 18 | +0xe4 | transforms | 36 | FUN_005e8ab0 |
| 19 | +0xf0 | event lists (a list of lists) | 16 per event | FUN_005ebbf0 |
| 20 | +0xfc | variants | 8 | FUN_005ebed0, FUN_005e8590 |
| 21 | +0x70 | final int | 4 | FUN_005e8ee0 |

Every section starts with an int count. Elements are listed in stream order,
which is not the in-memory order.

### Tagged references

Inside the records, a link to another table is a tagged int: `(index << 1) | 1`,
or 0 for none. After loading, FUN_00642ea0 turns each tag into a pointer
(`base + (tag >> 1) * stride`). Names are tags too, with a byte offset into the
name pool. Plain ints (state, sequence and clip indexes, layer numbers) are not
tagged.

## Clip node (36 bytes)

FUN_005eb2e0 reads three floats and six ints. FUN_005e1c20 prints the first
three and the reel name. FUN_005e1b60 relocates the others.

| Offset | Type | Field | Points into |
|---:|---|---|---|
| 0x00 | f32 | `in` | |
| 0x04 | f32 | `out` | |
| 0x08 | f32 | `speed` | |
| 0x0c | tag | reel | reels (+0x64); an index >= 0x4000 selects the second table (+0xc8) |
| 0x10 | tag | events | event lists (+0xf8, stride 12) |
| 0x14 | tag | transform | transforms (+0xec, stride 36) |
| 0x18 | tag | blend | blends (+0xe0, stride 16) |
| 0x1c | tag | mask | masks (+0x7c, stride 8) |
| 0x20 | tag | name | name pool (+0xbc) |

New elements start as in = -1, out = -1, speed = 1 (FUN_005eb2e0) and the
stream then overwrites all three. A node with `in == out` plays a single
frame (a held pose; *inferred* from `in == out` and the 0.0 duration).

When a node starts, FUN_005e1d80 copies its first nine dwords to the state
machine at +0x08, so the machine's rate (+0x10) starts as the node `speed`.
FUN_00723620 writes that same field (see the run state below).

## Sequences and chain

A sequence (12 bytes, FUN_00643d60) is `head` (tag into the chain), `events`
(tag into the event lists) and `name` (tag). The chain (8 bytes: `clip` int and
`blend` tag) is an array; a sequence is the run of links from `head` up to the
first link whose clip is -1. FUN_00643f10 starts the first link: the blend
comes from the action or, when it is 0, from the link.

## States, transitions and actions

A state is 36 bytes (FUN_005eab00; fixup FUN_00642a80):

| Offset | Content |
|---:|---|
| 0x00 | vector of transitions (0x14 bytes each) |
| 0x0c | tag into the variants |
| 0x10 | factory id (FUN_0060f760), -1 when none |
| 0x14 | child state index, -1 when none |
| 0x18 | tag into the blends |
| 0x1c | tag into the objects |
| 0x20 | tag, state name (`st_*`) |

A transition is `variant` (tag), `condition` (id) and a vector of actions
(FUN_005ea770). An action is four ints plus a fifth when the kind is 1 to 6
(FUN_005e8e00): `kind, target, blend, param, arg`.

FUN_00642c00 starts a state. It evaluates the transitions in order and runs
the actions of the first one whose condition is true. The loop has no bound,
so the last transition must be unconditional (id 0): that is what the states
listed below show. Actions (FUN_00642c00, operands fixed by FUN_006429b0):

| Kind | Effect | `target` | `arg` |
|---:|---|---|---|
| 1 | play sequence (FUN_00643f10) | sequence | |
| 2 | continue with another state on this layer | state | |
| 3 | start a state on another layer (FUN_005e3b80) | state | layer |
| 4 | play a sequence on another layer (FUN_005e3e50) | sequence | layer |
| 5 | create the layer entity from a factory | | tag into the layer types |
| 6 | call a function (FUN_0060f730) with the `param` variant | | function id |

Conditions are ids resolved by FUN_00613220 (`GameAnimationManager` tables):

| Id | Function | Meaning |
|---:|---|---|
| 0 | FUN_006135d0 | always true |
| 1 | FUN_006135e0 | the state being left (machine+0x9c) is state N |
| 2 | FUN_00613620 | the current clip node is clip N |
| 3 | FUN_00613640 | the current sequence is sequence N |
| 4 | FUN_00613670 | random threshold |
| 5 | FUN_006136b0 | layer A is in state B |
| 6 | FUN_00613700 | the current state name equals a string |
| 7 | FUN_00613770 | the clip reached its end |
| 0x400 | FUN_006134e0 | NOT of one binding |
| 0x401 | FUN_00613510 | OR of two bindings |
| 0x402 | FUN_00613570 | AND of two bindings |

A combinator's operands are the `value` of its variant and of the next
variant; each is a tag into the bindings (`value` tag, `condition` id), so
conditions nest. Ids 4 to 8 and the game-specific ids above 8 are only
partly read (name and shape of the callee, not every operand).

The machine lives in the layer slot of the animation instance
(`AnimInst`, vtable 0x86ec4c): `+0x14` is `StartState(state, instance,
layer)` (FUN_005e3bf0), and layer `i` is the state machine at `+0x20 + i * 4`
(FUN_005e3aa0). The state names map to indexes through the symbol table
(section 13): in Kain's graph all 188 symbols match `st_<symbol>`.

## Kain's run state

`FUN_004458e0` (run state enter) calls `StartState(1, ...)`; state 1 is
`st_run`. Decoded with `animatio.py states` (the `prev==` terms are
condition 1):

| State | Layer | What it does |
|---|---:|---|
| `st_run` | 0 | If the previous state is none of `st_run`, `st_rdy2run`, `st_run_landing`, `st_slide_run_landing`, `st_float_run_landing`: go to `st_rdy2run`. Otherwise play `sq_run` (clip node `cl_run`, reel `kain/run`) and start `st_walkblend` on layer 1 |
| `st_rdy2run` | 0 | If the previous state is `st_rdy2run`: go to `st_run`. Otherwise: set the root layer to 2 (callback FUN_00613480), play `sq_rdy2run` (`cl_rdy2run`, frame 0 of the run reel, `in == out`, 0.2 s blend), start `st_walkblend` on layer 1, `st_moveblend` on layer 2 and `st_movelean` on layer 3 |
| `st_walkblend` | 1 | Plays `sq_walk` (`cl_walk`, reel `kain/walk`) or, when its OR condition is false, `sq_rdy2walk` (`cl_rdy2walk`, frame 0 of the walk reel, 0.2 s blend) |
| `st_moveblend` | 2 | One kind 5 action: creates the layer entity from layer type 0 (type id 5) |

Layer type 5 resolves to the global at 0xa6abc4, whose value is the vtable of
`CrossBlendAFSFactory` (RTTI at vtable 0x86ea98). FUN_005d4880 requires that
class on layer 2 before it writes the blend weights, which is what FUN_00445ba0
does with `s` and `1 - s` (inputs 0 and 1: layers 0 and 1). The variant of the
kind 5 action is a float (0.01), copied into the first weight when the entity is
created (FUN_005d4670).

What this says about the rates FUN_00445ba0 writes:

| Fact | Evidence |
|---|---|
| Layer 0 plays the run clip, layer 1 the walk clip | st_run, st_walkblend above |
| The per-frame rate replaces the clip node speed, it does not multiply it | FUN_005e1d80 copies node speed to machine+0x10; FUN_00723620 overwrites that field |
| So W is 200 u/s * 1.15 (the 1.5 of `cl_run` is not applied) | the oracle measures 230; walk-back has no override and keeps its node speed 1.7 (62.5 * 1.7 = 106.25) |
| Root motion is read from one layer, `AnimInst+0x130` (*inferred*: the nine floats are the root delta) | FUN_005e3ea0 copies the nine-float output of layer `param_1[0x4c]` for FUN_0041fb00; FUN_00613480 sets the index |
| `st_rdy2run` sets that layer to 2 | its action 6 with callback id 3, value 2 |

## Class tables

The ids in the stream (conditions, callbacks, factories, object classes) go
through `AnimFuncLookup` (vtable 0x86f494); the game derives it as
`GameAnimationManager` (vtable 0x867cf8), whose tables extend the engine ones
(ids below the engine count use the engine table).

| Table | Function | Content |
|---|---|---|
| callbacks (action kind 6) | FUN_00516c50 | engine ids 0 to 4, then game ids |
| layer types | FUN_00516c80 | engine ids 0 to 8 are factory classes |
| event functions (event lists) | FUN_00516cb0 | game ids only; the engine table returns 0 |
| condition and factory ids | FUN_00516ce0 | engine 0 to 8, 0x400 to 0x402, then game ids |
| object classes | FUN_00516d40 | 0 = null, 1 = `ClipAndBoneID` (FUN_004a5ce0), 2 = `ClipAndBoneID2` (FUN_004a5fe0) |

Object class 1 reads an int and a clip tag; class 2 reads two ints and a clip
tag. Callback 2 (FUN_00613450) stores a value in the instance; callback 3
(FUN_00613480) sets the root layer.

## Port

`tools/bo2fmt/animatio.py` reads the whole stream and keeps unknown fields raw.
Port-only limits: an element count that cannot fit in the remaining bytes is
rejected, and counts above 4M are rejected.

## Open questions

- The W start phase: oracle ground speed is 100 u/s (200 * 0.5) for the first
  0.18 s and then 230. The graph gives the sequence (`st_run` to `st_rdy2run`
  to `st_run`, with a 0.2 s blend into a held frame of the run reel), but
  nothing in it explains the factor 0.5. Next step: decompile the machine
  advance (FUN_00643030), the blend start (FUN_005cecf0, FUN_0063bb40) and the
  cross blend entity (vtable 0x86eaa4) to see how root motion is weighted, or
  sample the layer rates (`machine+0x10`) in the oracle.
- Shift+W (200 u/s, unverified). FUN_00445ba0 gives 230 at full stick; the
  value 200 needs a smaller stick magnitude `s` and the cross blend weights.
  Where the input layer sets `s` for Shift is not decoded.
- Kain's reels 109 to 115 (used by the wall-hug clips) point at symbol 0
  (`kain/walk`), which looks like a placeholder.
- The nine header values, the int table (+0xa8), the byte table (+0x80) and
  the masks (+0x74): layouts known, meaning not (*inferred*: masks select
  bones).
- Condition ids above 7 and the operands of ids 4 and 6.
- The event lists: `(time, kind, function id, tag)`. Kain's `cl_run` has 14
  events, seven at 0.0667 s and seven at 0.5 s, while the `kain/run` reel
  carries its own events at frames 5 and 17; how the two relate is not
  decoded.
- Version 1 aliases: which resource the game loads when it follows one.
