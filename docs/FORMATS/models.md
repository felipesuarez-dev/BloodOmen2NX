# Models and animation

Status: **static cell geometry decoded and rendered; materials and
animation open**.

The static shell of a level lives in the `cellgrup` entry of its `.big`.
The shared `model` entries (characters, props, particles) use the same
`rmd` chunk format. The geometry is in world space: the renderer can draw
a whole level without per-model transforms.

## Cell group

The `cellgrup` payload starts with a nested container:

```
0x00  u8[8]  header (unused)
0x08  char[8] "cellmdls"
0x10  u32   section size
0x14  u32   0
0x18  u32   model count
0x1C  ...   model blocks
```

A model block:

```
u32   version (105 in this build)
char  name[] (NUL-terminated, "default" for cell models)
u32   size (16 + chunk area)
u32   0
      chunks (chunk area = size - 16)
```

The next model starts at `model_start + 4 + size`. A model without an
`rmd` chunk is skipped (one of the 76 models in `sanctuary.big`).

A chunk is:

```
char[8] name
u32     total size, including this 16-byte header
u32     0
        data (total - 16 bytes)
```

The `rmd` chunk is a nested chunk container:

```
u32   2
      record: char[8] "default"; u32 total size; u32 0; subchunks
```

## `rmd` subchunks

| Chunk | Layout | Status |
|---|---|---|
| `position` | `u32 count` + `count` × 3 floats | World-space vertices |
| `normals` | `u32 count` + `count` × 3 floats | Per-vertex normals |
| `prims` | See below | Triangle lists per material |
| `textures` | `u32 count` + `count` u16 | Per-vertex UV indices; mapping open |
| `uvs` | `u32 set`, `u32 count` + `count` × 2 floats | One chunk per UV set |
| `mlyrcolr` | `u32 start`, `u32 count` + `count` × 48 bytes | Material layer colors; observed |
| `mlyrctrl` | `u32 start`, `u32 count` + `count` × 12 bytes | Material controllers; observed |
| `texture` | `u32 start`, `u32 count` + per-material records | Texture symbol per material; observed |
| `mdlattr` | Named attribute table | Model metadata |
| `vtxweigh` | `u32 count` + weights | Skinning; static cell models are zero |
| `jointidx` | `u32 count` + joint indices | Skinning |
| `collmodc` | Collision model | Not used by the renderer |

### `prims`

```
u32 group count
per group:
  u32  material index
  u32  triangle count
  u32  index count (3 × triangle count)
  u16  vertex indices[index count]
  u16  flag
  u8   extra[flag == 0xffff ? 40 : 38]
```

The vertex indices are triangle lists and index the model's `position`
array. The extra record carries the strip/ADC data that the original
dumper prints at `0x67d270` (`MaterialIdx`, `NTris`, `NStrips`,
`strip_lens`, `Tri_Coords`, `NAdcs`). The on-disk layout of the extra is
observed, not fully decompiled; the triangle list is the render
geometry.

## Textures and materials

The `cellgrup` entry header carries a list of `u16` symbol indices (the
`n` values in the `.big` entry). The model's `texture` chunk maps a
primitive material to a position in that list; the symbol value indexes
the `.big` name table. In `sanctuary.big`, position 6 resolves to symbol
2441 and the name `Inner City/Textures/Walls/SewBrick3`; positions 7..10
resolve to the next wall/floor textures. The `texture` entry with that
name is the DDS payload.

This mapping is the next step: it must be validated by binding the
textures and comparing the frame. The `textures`/`uvs`/`mlyrcolr` layout
above is observed, not yet proven as the final material path.

## Animation

`vtxweigh`, `jointidx`, `animatio` and `animreel` are present in the
containers. Static cell models carry zero weights; character models use
the animation chunks. The node hierarchy and keyframe encoding are open.

## Coverage

`sanctuary.big`:

| Metric | Value |
|---|---:|
| Model blocks | 76 |
| Parsed models | 75 |
| Skipped (no `rmd`) | 1 |
| Primitive groups | 123 |
| Vertices | 7,458 |
| Triangles | 5,107 |
| World bounds | 2164 × 1124 × 624 |

The headless renderer draws this geometry with per-material colors and a
free camera (`bo2nx --level`). The near-plane clipper is pending, so a
camera inside a surface can show projection artifacts; the default
isometric frame is stable.

## Open questions

- The exact material → texture → UV path (the current lead is above).
- The 40/42-byte `prims` extra record.
- The cell grid/portal data after the models (visibility and streaming).
- `model 74` has no `rmd`; its role is unknown.
- Skeleton, skinning and animation playback.
- The shared `model` entries versus the `cellmdls` inline models.

## Next steps

1. Bind the cellgrup textures and validate the material/UV mapping.
2. Add the near-plane clipper for interior cameras.
3. Parse the cell grid/visibility data.
4. Decode the animation/skeleton chunks.
