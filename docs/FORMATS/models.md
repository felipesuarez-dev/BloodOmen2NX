# Models and animation

Status: **static cell geometry, base textures, the cell grid/BSP, the
material layer records, the D3D texture-stage equation, the skeleton
hierarchy/weights/joints and the raw animation payloads are decoded; the
bind pose and keyframe evaluation are open**.

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
| `mlyrcolr` | `u32 start`, `u32 count` + `count` × 48 bytes | Three RGBA colors; see Material layers |
| `mlyrctrl` | `u32 start`, `u32 count` + `count` × 12 bytes | Three controls; see Material layers |
| `mtlctrl` | `u32 count` + `count` × 12 bytes | Three `u32` values; semantics open |
| `texture` | `u32 start`, `u32 count` + per-material records | Texture symbol per material; bound by the host |
| `mdlattr` | Named attribute table | Model metadata |
| `vtxweigh` | `u32 count` + `count` floats | Per-vertex skin weights |
| `jointidx` | `u32 count` + `count` u16 | Per-vertex joint indices |
| `skeleton` / `modlskel` | `u32 count` + `count` × (u32, u32) + 96 × count bytes | Bone hierarchy and bind pose |
| `collmodc` | Collision model | Raw payload extracted; layout below |

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

### `collmodc`

The collision chunk is a sibling of `rmd` in the model block, not a
subchunk. The payload starts with a NUL-terminated record name
(`default`), a u32 header size, two header u32 values and a vector of
8-byte property records. After the header the observed sequence is:

```
u32  vertex flag (0xFFFFFF01 in the reference models)
u8   compressed flag
u32  data size; bytes[data size]       (compressed vertex blob)
u32  index count
u32  index bytes; bytes[index bytes]
u32  edge count; edge[24 bytes]
u32  face count; face[16 bytes]
u16, u16, u32, u8, u32 x4              (trailer)
```

The host stores the raw `collmodc` payload per model
(`LevelGeometry::collisions`) and reports the model and byte counts.
The compressed vertex blob, the edge/face semantics and the collision
runtime are not decoded yet.

## Textures and materials

The `cellgrup` entry header carries a list of `u16` symbol indices (the
`n` values in the `.big` entry). The model's `texture` chunk maps a
primitive material to a position in that list; the symbol value indexes
the `.big` name table. In `sanctuary.big`, position 6 resolves to symbol
2441 and the name `Inner City/Textures/Walls/SewBrick3`; positions 7..10
resolve to the next wall/floor textures. The `texture` entry with that
name is the DDS payload.

The C++ reader resolves the name, finds the `texture` entry and decodes
the top DDS mip. The `textures` chunk is used as the per-vertex UV index
into the first `uvs` set (`set = 0`); the triangle's material selects the
texture record. In `sanctuary` 33 of 34 names resolve to a texture entry;
the one that does not is a non-texture symbol and falls back to the
material color.

### Material layers

The `mlyrcolr` and `mlyrctrl` chunks describe the material layers. Each
starts with a material range:

```
u32 start
u32 count
count × record
```

`mtlctrl` differs: it starts with a `u32 count` followed by `count`
records (no start field).

| Chunk | Record | Fields |
|---|---|---|
| `mlyrcolr` | 48 bytes | three RGBA colors (16 bytes each) |
| `mlyrctrl` | 12 bytes | `u32 value`, `f32 weight`, `u16 arg0`, `u16 arg1`, three times |
| `mtlctrl` | 12 bytes | three `u32` values |

The original debug dumper reads the three colors at offsets 0, 0x10 and
0x20 (`FUN_0067e850`) and the controls as the four fields above
(`FUN_0067e880`). The loader `FUN_0061ed40` creates render-model
elements for chunk IDs `0xd..0x10` (`mlyrcolr`), `0x11..0x14`
(`mlyrctrl`) and `0x15..0x18` (`texture`); `FUN_0067dd40` prints them.

The host parses `mlyrcolr` and `mlyrctrl` into
`LevelGeometry::materials`. The software renderer uses the second layer
(the diffuse slot after the ambient slot) as the surface color when a
material has no texture, scaled by that layer's control weight.

The D3D8 trace of the reference build (captured from the loading/UI path)
sets texture stage 0 to `D3DTOP_MODULATE` (4) or `D3DTOP_MODULATE2X` (5),
with `COLORARG1 = TEXTURE`, `COLORARG2 = DIFFUSE`, and the same alpha
operations. Stages 1 and 2 are `SELECTARG1` pass-through. The final color
is therefore `texture × vertex diffuse` (doubled for `MODULATE2X`); the
material colors feed the vertex diffuse, not a separate texture-stage
constant.

The host exposes the raw layers and a diagnostic tint:
`--material off|diffuse|layers|layers2x`. The static lighting itself comes
from the `.slm` light maps (`docs/FORMATS/litedata.md`), sampled with the
model's second UV set and enabled with `--lightmap`. `off` keeps the current preview,
`diffuse` uses layer 1, `layers` sums the three and `layers2x` doubles the
sum. Applying the direct tint to `sanctuary` darkens the frame from mean
0.107 to 0.086 and clips the maximum from 0.667 to 0.118. That negative
result shows the material colors cannot be the albedo multiplier on their
own: the static light map is missing.

`sanctuary.big` carries `litedata/sanctuary.slm` (388,673 bytes). Its
header is `version = 1`, `header size = 112`, `group name length = 13`
and the group `cryptexithall`; the 13 is the group name length, not a
light count. The file contains 71 light-map entries across the
cell/object groups. The host applies them with `--lightmap`, sampled with
the model's second UV set; 16 of the 34 sanctuary textures resolve. The
loader also matches models to light-map groups by vertex count and model
order (62 model light maps, 2,769 triangles in sanctuary). The exact combination with the material layers is
open; the `--material` modes stay diagnostic.

The observed `mtlctrl` record is `{0, 0x004c0000, 0x00000100}` per
material. The observed `mlyrctrl` record is `{0x000a0000|1|6, 1.0, 0, 0}`;
the high word and the two `u16` fields are not interpreted.

## Cell grid and visibility

The chunks after `cellmdls` are a second chunk stream inside the same
`cellgrup` payload. The reference levels use this order:

```
cellmdls [cellanim] cellinst cellight cellmark cells attrs cellbsp
```

`cells`, `attrs`, `cellbsp`, `cellinst`, `cellight` and `cellmark` are
decoded here. `cellanim` is still opaque.

### `cells`

```
u32 cell count
per cell:
  u32  version = 2
  u16  resource symbol
  u16  reserved (0xFFFF)
  u32  flags
  Vec3 minimum
  Vec3 maximum
  chunk "default":
    u32 total
    u32 0
    subchunks: cellattr, portal2, terrains, dbgmodel
```

The engine resolves the symbol through its resource table; the host
keeps the raw value. `flags` and the reserved word are observed, not
interpreted. `FUN_00687ae0` parses version 2; `FUN_00687920`
selects the version and `FUN_00687990` is the version 1 parser, which
does not occur in the reference tree.

The two `Vec3` values are the bounds of the cell, not of the level.

### `cellinst`

The `cellmdls` model geometry is in **local** space. The `cellinst` chunk
places copies of those models in world space:

```
u32 instance count
per instance, version 3 (40 bytes):
  u32  version = 3
  u16  material A
  u16  material B
  u32  model index
  Vec3 position
  Vec3 rotation
  u32  flags
version 4 (52 bytes):
  u32  version = 4
  u16  material A
  u16  material B
  u32  model index
  Vec3 position
  Vec3 scale
  Vec3 rotation
  u32  flags
```

`model` indexes the `cellmdls` model block, including blocks without an
`rmd`. `0xFFFFFFFF` is a sentinel: the instance has no model and is
skipped. `material A` and `material B` are symbols in the cellgrup list;
they often resolve to textures, but their override semantics are open.
`flags` is stored and not interpreted. Version 3 has no scale (implicit
1.0). Version 3 occurs in `hubworld`; the level containers use version 4.

The host places each instance as
`world = position + R * (scale * local)`, where `R` is the Euler
rotation applied X, then Y, then Z. Normals are rotated; UVs and
triangle indices are copied. The reference rotations are axis-aligned,
so the Euler order is not yet distinguishable from the bounds alone.

In `sanctuary` 112 instances produce 74 placed models; 38 are the
`0xFFFFFFFF` sentinel or a model block without `rmd`. Of the 74 placed
models, 73 fit inside a cell within 8 units and one straddles a cell
boundary. The union of the placed models matches the cell union on X and
Z. The previous host drew the `cellmdls` vertices as if they were world
space; the instance pass fixes the level layout.

### `cellight`

Lights are stored with a variable record. The reference levels use version
3:

```
u32 light count
per light:
  u32  version = 3
  u32  handle
  Vec3 position
  4 × f32 color
  5 × f32 parameters
  u8[3] flags
  u8   type
       type 1: Vec3 + 2 × f32
       type 2: Vec3
  u32  flags
  u32  index count
       index count × u32
```

The host stores the handle, position, color, parameters, type and the
index list; the three-byte flags and the type-specific vectors are
skipped. `FUN_00688f20` selects the version and `FUN_00689300` parses
version 3. The light is not used by the renderer yet; the static frame
still uses the fixed preview light.

### `cellmark`

Markers use a fixed version 4 record (56 bytes) in the reference levels:

```
u32 marker count
per marker:
  u32  version = 4
  u32  id A
  u32  id B
  u32  symbol A
  u32  symbol B
  Vec3 position
  Vec3 normal
  u32  flag
  u32  value
  f32  number
```

`FUN_006889a0` selects the version and `FUN_00688c90` parses version 4.
The symbols are kept raw; the marker kind and the meaning of the numeric
fields are open. The host exposes the markers but does not render them.

### `portal2`

```
u32 portal count
per portal:
  u32  version (1 or 2)
  u16  cell A
  u16  cell B
  u32  vertex count
       vertex count × Vec3
  Vec3 plane
  u32  index (version 2 only)
```

Version 1 leaves `index` at -1. `FUN_0068d8e0` parses version 1 and
`FUN_0068dba0` parses version 2. The three floats after the vertices
are stored as a `Vec3`; their use as a plane is consistent with the
layout but not confirmed by a renderer path yet.

### `terrains`

```
u32 count
count × u32 terrain id
```

The host exposes the ids but does not use them yet.

### `dbgmodel`

A debug model chunk. The host records its byte size and skips it.

### Attribute tables

`attrs` and the nested `cellattr` chunk share one table format:

```
char[8] name
u32     size (total table size, including the name)
u32     values[(size - 12) / 4]
```

`attrs` is one 32-byte `default` table with five values in every
reference level. `cellattr` is one table per cell and its size varies.
`FUN_006a8ad0` dispatches the table, `FUN_006bf1d0` selects the version
and `FUN_006bf250`/`FUN_006bf320` drive the keyword-table parser. The
value semantics are open.

### `cellbsp`

```
u32 version = 1
u32 leaf count
u32 maximum depth
node:
  u8 type
  0: align 2, u16 value              (leaf)
  1: align 4, Vec3 plane; f32 distance; left node; right node
```

The alignment is relative to the start of the `cellbsp` payload, not to
the file. The recursion is `FUN_0068b270`; `FUN_0066b030` checks the
version and `FUN_0066b070` reads the header and allocates the root.

The header's leaf count matches the number of leaves in all 162
reference groups that have both `cells` and `cellbsp`. The depth is the
maximum recursion depth and ranges from 1 to 24. Leaf values are
usually `0xFFFF`; the host keeps them raw. The traversal and the exact
meaning of the split plane are not decoded yet.

### Handler map

`FUN_00665200` is the CellGroup chunk dispatcher. It matches the packed
name table at `0x87231f` (`cellmdls cellinst cellcams cellolgt cellslgt
celldlgt cellight cellmark cellorgn cellspln cells`), plus `cellanim`,
`attrs` and `cellbsp`. `FUN_0079ff50` and `FUN_0078d410` are **not**
cell-grid handlers: they match `orgnmrkr` and `demodata` from the table
at `0x879adc`/`0x879ae4`, and `FUN_00793930` runs during teardown. The
cell loader is the `FUN_00665200` family.

### Cell coverage

Measured on the 135 level containers:

| Metric | Value |
|---|---:|
| `cellgrup` entries with `cells` | 173 |
| Groups with both `cells` and `cellbsp` | 162 |
| Cell version | 2 in all entries |
| BSP version | 1 in all entries |
| BSP leaf count matching the header | 162 / 162 |
| BSP depth range | 1..24 |
| `attrs` body size | 32 bytes in all groups |
| `cellinst` versions | 4 in level containers; 3 in `hubworld` |
| `cellight` groups | 114 (version 3) |
| `cellmark` groups | 131 (version 4) |
| `sanctuary` instances | 112 (74 placed, 38 skipped) |
| `canyons/cy01` instances | 575 (429 placed, 146 skipped) |
| `lowercity/lc01` instances | 512 (400 placed, 112 skipped) |

`sanctuary.big`: 3 cells, 6 portals, 121 terrain ids, 33 BSP nodes
(17 leaves, depth 8). `canyons/cy01.big`: 8 cells, 32 portals, 674
terrain ids, 167 BSP nodes (84 leaves, depth 10).

## Animation

`vtxweigh`, `jointidx`, `animatio` and `animreel` are present in the
containers. Static cell models carry zero weights; character models use
the animation chunks. The node hierarchy and keyframe encoding are open.

## Model coverage

`sanctuary.big`:

| Metric | Value |
|---|---:|
| Model blocks | 76 |
| Parsed models | 75 |
| Skipped (no `rmd`) | 1 |
| Primitive groups | 123 |
| Vertices | 7,458 |
| Triangles | 5,107 |
| Textures resolved | 33 / 34 |
| World bounds | 2164 × 1124 × 624 |

The headless renderer draws this geometry with the DDS textures and a
free camera (`bo2nx --level`). A triangle near-plane clipper keeps
interior cameras stable; a camera inside the closed shell sees the shell
interior, while the default isometric frame shows the level.

## Open questions

- Per-cell/object light-map selection, the vertex diffuse equation and
  the `mtlctrl` values.
- The 40/42-byte `prims` extra record.
- `cells` flags, `cellattr`/`attrs` value semantics, the `portal2`
  plane/index and the BSP traversal.
- `cellinst` material A/B overrides, flags and the Euler order.
- `cellight` type-specific parameters and the marker symbol semantics.
- `cellanim`.
- `model 74` has no `rmd`; its role is unknown.
- Skeleton pose evaluation, skinning and animation playback.
- `animatio`/`animreel` keyframe encoding.
- The shared `model` entries versus the `cellmdls` inline models.

## Next steps

1. Use the cell grid and BSP for culling and cell visibility.
2. Bind the shared `model` entries and decode `cellanim`.
3. Match the light-map group to the cell/object and combine it with the material layers.
4. Decode the `animatio`/`animreel` keyframe streams.
