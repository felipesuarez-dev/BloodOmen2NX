# Models and animation

Status: **static cell geometry, base textures, the cell grid/BSP, the
material layer records, the D3D texture-stage equation, the skeleton, its
pose evaluation and the skinning are decoded; clip blending is open**.

The static shell of a level lives in the `cellgrup` entry of its `.big`.
The shared `model` entries (characters, props, particles) use the same
`rmd` chunk format. Region entities resolve their short model names to
these entries; the host appends their local geometry at the entity
position. The cell geometry itself is in world space.

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
| `mtlctrl` | `u32 count` + `count` × 12 bytes | Three `u32` values; two bits of the second are read, see Render state and draw order |
| `texture` | `u32 start`, `u32 count` + per-material records | The textures of each material; see `texture` |
| `diffuse` | `u32 count` + `count` × 4 bytes (R, G, B, A) | One colour per vertex; only some models have it; see `diffuse` |
| `mdlattr` | Named attribute table | Model metadata; see `mdlattr` |
| `vtxweigh` | `u32 count` + `count` floats | Per vertex, the weight of its second bone (0 to 0.5 in Kain); see Skeleton pose and skinning |
| `jointidx` | `u32 count` + `count` u16 | Per vertex, the position of its second bone in the bone array |
| `skeleton` / `modlskel` | `u32 count` + `count` × 104-byte bone records | Decoded; see Skeleton pose and skinning |
| `collmodc` | Collision model | Points, packed triangles, spheres and faces decoded |

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

### `texture`

One record per material, of variable length:

```
u32  texture count n (0 for a material without a texture)
u16  symbol[n]
     0xffff when n is odd, so the record ends on a 4-byte boundary
```

A material with several textures is a material whose texture changes: the
frames of an animated surface, or the nine pictures of the dark gift icon of
the HUD, which the game picks by index (`FUN_005dce40`). Read from the data
and checked on every `texture` chunk of `code.big` (59), `sl01.big` (636) and
`kain.big` (22): each one ends exactly at its last record. Materials by
texture count: `code.big` 0: 11, 1: 42, 6: 1, 8: 14, 9: 2, 147: 4;
`sl01.big` 0: 8, 1: 1,125, 3: 7, 12: 2, 33: 1; `kain.big` 0: 1, 1: 28, 6: 1,
147: 1.

The host reads one symbol per material and takes every record of a chunk to
have the same size, which holds only when every material has one texture.

### `diffuse`

`u32 count` and one R, G, B, A byte quadruple per vertex; `count` is the
vertex count of the model. In the `ui/hud` cell group only the dark gift icon
and the enemy icon have it: white on the eight vertices of the picture and
black on the 38 vertices of the 36 triangles behind it. The chunk is one of the names the model loader
matches (`0x61f70f`). The renderer builds vertices of position, colour and
texture coordinate for a model with per-vertex colours, and reorders the
four bytes from R, G, B, A to the device's A, R, G, B (`FUN_005711e0`); that
this chunk is the source of those colours is read from the data, not traced
in the loader. The host does not read it.

### `collmodc`

The collision chunk is a sibling of `rmd` in the model block, not a subchunk.
The decoded payload is:

```
cstring record name (`default`)
u32  header size; bytes[header size]
u32  record flag (0xFFFFFF01)
u32  vertex bytes; point[vertex bytes / 6]   (three i16 per point)
     pad to a 4-byte boundary                 (2 bytes for an odd point count)
u32  index bytes; triangle records           (4 bytes each, 8 for a wide one)
u32  sphere count; sphere[24 bytes each]
u32  face count; face[20 bytes each]
28-byte trailer
```

The vertex block is a point list, not a box list: each point is three `i16`
in model space. A triangle record is the dword the pair test unpacks
(`FUN_0062dfc0`): three 9-bit vertex indices, `a = (record >> 18) & 0x1ff`,
`b = (record >> 9) & 0x1ff`, `c = record & 0x1ff`, and a category in bits 29
and 30. With bit 31 set the record is wide, two dwords with 16-bit indices:
`a` is the low word of the first, `b` and `c` the two words of the second.
The normal of a triangle is `(b - a) x (c - a)`. `sl01` has 73,542 records,
82 of them wide; 57,594 are category 0, 10,428 category 1 and 5,520
category 2. A face slice is the byte offset of its first record and a count
of records. A sphere is 24 bytes: centre `f32[3]`,
radius `f32`, bone index `u32`, flags `u32`. Kain's model has three:
`(0, 0, 51.98) r 19.8`, `(0, 0, 49.62) r 19.3` and `(0, 0, 23.60) r 15.0`, over
the body AABB `(-20, -20, 0)..(20, 20, 72)`. Its `collmodc` holds exactly that
AABB as two points, **no triangles**, the three spheres and one face; the live
mover's collision proxy is that box, and the movement resolver sweeps it (the
three spheres alone slide on the first ramp, `after_w` at x = 262).

A face record is `{u16 anchor_a, u16 anchor_b, u32 adjacent[2], u16 flag,
u16 triangle_count, u32 triangle_byte_offset}`. The anchor pair is the face's
local AABB corners; the triangle slice indexes the global triangle block. The
slices tile the block exactly: for model 57 the 14 faces with triangles sum to
the 130 records, and the 13 faces without triangles are adjacency-only. The
28-byte trailer closes the record; its first `u32` is `face_count - 1`.

Some payloads wrap the record in a second named header, so the flag is not
where the outer header ends; `parse_collmodc` locates it by the section chain
and requires the sections to tile down to the trailer. All 660 `collmodc`
payloads of `sl01` and the 76 of `sanctuary` decode with this rule.

`parse_collmodc` returns the points, the packed triangles, the faces and the
spheres; `parse_collmodc_boxes` returns the face and model AABBs, which are
only a broad phase. `bo2nx --collision-dump --big <level.big>
[--collision-model <substring>] [--collision-out <file>]` prints the instances,
the broad-phase boxes, the hull summary and the raw payload. The `sl01` dump
has 660 cell collisions; the W wall is model 57 `default`, instance 41, with
the model AABB at world x = 256..768, y = -264..512, z = 208..976 and 132
points, 130 triangles and 27 faces.

The runtime side matches the file: `FUN_0066cac0` exports the prims, type 1 a
sphere (centre and radius) and type 3 a triangle (three `u16` vertex indices
and an attribute index); `FUN_00621240` walks runtime arrays of 0x30-byte
faces, 0xc-byte vertices and 0x14-byte attributes; `FUN_0061ed40` is the `rmd`
chunk loader, not the `collmodc` loader.

Measured (`sl01_basic`, `wine_20261029_0043`): the W speed matches 230 u/s
until frame 2316; the oracle slows to 177.76 and 86.67 and stops at
x = 326.15, while the port keeps 230 and stops at x = 351.55 on the model 68
wall (x = 352.0). The 25.4 u difference is not the mesh format: the stop is
the model 57 ledge/ramp edge at z = 217.4, 9.4 u above the feet (the oracle
slides from y = 57.13 to y = 44.14 between frames 2311 and 2318 and then
stops). A sphere at the feet cannot reach a surface 9.4 u up. The slide and
the stop are written by the `FUN_0070eac0` "set position" message, not by the
movement response: the message projects the 2.15 u move on a plane with
normal (0.707, 0.707, 0) (230 cos 45 = 162 u/s at -45 degrees). The payload
sender and the volume behind that projection are the open piece
(`re/decomp/kain_movement/NOTES_blocker.md`).


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
of that path is therefore `texture × vertex diffuse` (doubled for
`MODULATE2X`). Which value feeds the vertex diffuse of level geometry is
settled by the next section: it is not the material layers.

### Why `--material diffuse|layers` render near-black (BO2NX-68)

The host exposes the raw layers and a diagnostic tint:
`--material off|diffuse|layers|layers2x`. `off` keeps the preview shade,
`diffuse` multiplies the texture by layer 1, `layers` by the sum of the
three and `layers2x` by twice the sum. The near-black result is the
premise, not an arithmetic slip: the material layer colors are not an
albedo multiplier.

| Evidence | Source |
|---|---|
| In `sl01`, of the 1,075 materials that triangles use, 1,015 have a grey `c1` of 0.098 or 0.118 (25/255, 30/255) and 960 have `c0 = c2 = (0,0,0)`. The second control weight is 1.0 for the bulk; some control records overlap ASCII text (`0x74786574`, `text`), so the record layout is not fixed-size there (open) | `mlyrcolr`/`mlyrctrl`, host dump of the used materials |
| Textured shared models have all-zero layers: `psntanm`, `psntcnf`, `rat`, `nm_head` (`c0 = c1 = c2 = 0`). Multiplying by them would draw characters black, and the oracle frame shows them lit | host dump of the `sl01` `model` entries |
| Helper models carry editor colours, for example `c1 = (0.176, 0.063, 0.063)` on every `pyramid/red` model | same dump |
| The device is initialised with `D3DRS_LIGHTING = FALSE` (`SetRenderState(0x89, 0)` at `0x55fe04`), so material diffuse is never combined with D3D lights for this geometry | disassembly of the device state setup around `0x55fdc4` |
| The render-state cache binds a second texture to stage 1 with `COLOROP = MODULATE`, `ALPHAOP = SELECTARG2` (`0x565bf3`..`0x565c10`) and disables stage 1 when none is bound | disassembly of the state cache near `0x565b73` |
| Measured on the oracle pose: `--material diffuse` has mean RGB 0.016 against 0.108 in the Wine frame, and a luma correlation of 0.015 (`off`: 0.13) | `docs/ORACLE_COMPARE.md` gate |

So the layers are the exporter's fallback colours (they colour the
untextured preview surfaces) and play no part in the lighting of textured
surfaces; the modes stay as a diagnostic and are not a path to parity.

The static lighting appears to be baked. Each `.slm` group stores, after its
light-map entries, one RGBA `f32` colour per vertex of the lit model
(`docs/FORMATS/litedata.md`). Inferred, not traced: the stage-1 `MODULATE`
above is the light map (the disassembly shows a second texture bound to
stage 1; the code that binds the `.slm` image there was not found), and the
per-vertex colours are the vertex diffuse. The equation this would give is
`texture × vertex colour × light map luminance`. A spike of exactly that
equation (vertex colours bound with the existing vertex-count group match)
raised the masked world error against the Wine frame from 35.4 to 48.2 of
255 and the mean RGB from 0.241 to 0.288 (Wine: 0.108), so it was
not kept: the original is much darker than any combination available
here, and the group-to-instance binding is itself a heuristic. Fog, the
per-level ambient values and the dynamic lights are not implemented.

`sanctuary.big` carries `litedata/sanctuary.slm` (388,673 bytes). Its
header is `version = 1`, `header size = 112`, `group name length = 13`
and the group `cryptexithall`; the 13 is the group name length, not a
light count. The file contains 71 light-map entries across the
cell/object groups. The host applies them with `--lightmap`, sampled with
the model's second UV set; 16 of the 34 sanctuary textures resolve. The
loader also matches models to light-map groups by vertex count and model
order (62 model light maps, 2,769 triangles in sanctuary). The layers take
no part in that combination (see above).

The observed `mtlctrl` record is `{0, 0x4c00, 1}`: 1,082 of the 1,084
materials of `sl01`; the other two and most HUD pieces have `{0, 0x8c00, 1}`.
(An earlier version of this page gave `{0, 0x004c0000, 0x00000100}`: the
same record read three bytes late.) The observed first word of a `mlyrctrl`
record is `0x000a0000` plus 0, 1 or 6 in its low bits, sometimes with
`0x08000000`; the two `u16` fields are not interpreted. The bits the renderer
reads are in the next two sections.

### `mdlattr`

The attributes of a model: what kind of object it is and how it is drawn.
The layout below is read from the data (the 42 models of the `ui/hud` cell
group), not from the loader:

```
char[8] table name ("default")
u32     size of the table, this header included
u32     1
u32     2
record[(size - 20) / 8]:
  u8   type: 0 float, 3 bool, 7 symbol
  u8   0
  u16  key (symbol: the name of the attribute)
  4-byte value: f32; or u8 and three 0xff; or u16 symbol and 0xffff
```

The two header words and attributes with more than one value are not read.
In memory a record is `u16 key`, `u8 type`, `u8 count` and the value
(`FUN_006c05c0` finds a record by key; `FUN_006c2250` returns the floats of
a type 0 record and `FUN_006c2190` the bytes of a type 3 record;
`FUN_006c2170` the count). The readers are `FUN_006a81a0` (float),
`FUN_006a80a0` (bool) and `FUN_006a8620` (true when the bool is present and
set).

The attribute names are symbols the executable builds at start (the
`0x5d0b00`..`0x5d1310` initialisers): `ClipEnable`, `ClipFar`, `ClipNear`,
`DFacade`, `FogColor`, `FogEnable`, `FogFar`, `FogNear`, `Lightmaps`,
`NoCollide`, `NoDraw`, `NoFog`, `NoLighting`, `ObjectType`,
`RenderPriority`, `RenderType`, `SkySphere`, `SortBias`, `SpriteFacade`,
`VertexAlpha`, `Water`, `ZFacade` and others. The HUD models carry
`ObjectType`, `EntityClass`, `UseMaxMesh`, `NoCollisionModel` and
`RenderPriority`.

### Render state and draw order

How the renderer turns a model into draw calls. Every rule names the
function it was read from.

A model becomes a mesh object (`FUN_0056d130`) with one material record per
material (`FUN_005711e0`, `FUN_00571a70`, `FUN_00571bf0`). The state of a
material is a word of flags:

| Flag | Set when | Effect (`FUN_00565b50`) |
|---|---|---|
| bits 0..10 | always: the slot of the material's texture | texture of stage 0 |
| `0x800` | the texture blends, or the model has `VertexAlpha` | alpha blending on, destination factor `INVSRCALPHA` |
| `0x1000` | not by this path | alpha blending on, destination factor `ONE`, fog colour 0 |
| `0x2000` | `mtlctrl` word 1 has `0x4000` | back faces culled; without it, none |
| `0x4000` | `(mlyrctrl word 0 & 0xc00000) != 0x400000` | texture addressing wraps; without it, clamps |
| `0x8000` | not by this path | alpha test off |
| `0x10000` | the texture does not write depth, or `mtlctrl` word 1 has `0x10000` | depth write off |
| `0x80000` | the model has `NoFog` or `SkySphere` | fog off |

A texture blends and writes depth by its DDS format (`FUN_0056f320`):

| Format | Blends | Writes depth |
|---|---|---|
| DXT1, DXT2, DXT4, RGB without alpha | no | yes |
| DXT3 | yes | yes |
| DXT5, RGB with alpha | yes | no |

The material also keeps three colours and one alpha (`FUN_00571a70`): the
alpha is the smallest of the three layer alphas of `mlyrcolr`. With bit 26
of `mlyrctrl` word 0 clear, which is every material of `sl01` and of the
HUD, the first and the third colour are 1 and the second is the third
layer's colour, or 1 when the low six bits of the word are 6.

When a mesh is queued (`FUN_0056ce80`) each of its batches gets `0x800` as
well when the model has a `RenderPriority` other than 0 or the material
alpha is below 1. A batch with `0x800` or `0x1000` goes to the sorted pass
with a sort value k (`FUN_0056d130`, `FUN_0056ce80`):

| `RenderPriority` p | k |
|---|---|
| 0 (the default) | the depth of the instance origin along the view direction, plus `SortBias` |
| below 0 | `100000 + p` |
| above 0 | `-100000 - p` |
| any, with `SkySphere` | the above `+ 100000` |

The frame is drawn in this order (`FUN_005611c0`):

1. The batches without `0x800` or `0x1000`, grouped by state.
2. The sorted pass, triangle by triangle, from the largest k to the smallest
   (`FUN_00562e60` stores `k * -8.38848` as a 24-bit key and the flush sorts
   it upward). So: negative priorities and the sky, then the rest from far
   to near, then the positive priorities in rising order. Equal keys keep
   the order in which they were queued.

Objects are queued cell by cell, in the order of each cell's object list
(`FUN_007f6340`, `FUN_005fbea0`), and the cell's own geometry after its
objects (`FUN_00605be0`). The depth test is `LESSEQUAL` and the source
blend factor `SRCALPHA` (set by the flush).

Not read: the alpha test reference and function, and how the colour a class
sets on its model (`FUN_005dce00`, parameter 8) reaches the vertices.

## Cell grid and visibility

The chunks after `cellmdls` are a second chunk stream inside the same
`cellgrup` payload. The reference levels use this order:

```
cellmdls [cellanim] cellinst cellight cellmark cells attrs cellbsp
```

`cells`, `attrs`, `cellbsp`, `cellinst`, `cellight`, `cellmark` and
`cellanim` are decoded here.

### `cellanim`

The clips of the cell group's animated models. The data is a `u32` count and
that many clips, each stored as an `animreel` chunk without its 16-byte chunk
header, so it starts at the inner version ([`animreel.md`](animreel.md)):

```
u32 count
count x { u32 inner version (6), u32, u32 events, events, clip header, blob, descriptors }
```

The dispatcher `FUN_00665200` reads the count and `FUN_00665b50` reads one
clip per item with `FUN_006689a0`, the reader an `animreel` entry uses after
its chunk header. The clips end exactly at the end of the section.

Measured on the installed levels: 127 of the 152 level containers that load
have the section, with 2,490 clips in all, and every one parses (`sl01` has
19). The UI scenes have it too ([`region.md`](region.md#ui-scenes)): `ui/hud`
has seven clips of 11 to 101 frames, each on the scale of bone 0, and
`ui/darkgifts` eight clips of 31 frames on the rotation X of bone 0.

`parse_cell_group` keeps them in `LevelGeometry::cell_animations`, in file
order; `tools/bo2fmt/animreel.py` has the reference reader
(`parse_cellanim`). Which model a clip belongs to is not decoded.

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
  u16  name (symbol)
  u16  external reference (symbol)
  u32  model index
  Vec3 position
  Vec3 rotation
  u32  flags
version 4 (52 bytes):
  u32  version = 4
  u16  name (symbol)
  u16  external reference (symbol)
  u32  model index
  Vec3 position
  Vec3 scale
  Vec3 rotation
  u32  flags
```

`model` indexes the `cellmdls` model block, including blocks without an
`rmd`. `0xFFFFFFFF` is a sentinel: the instance has no model and is
skipped. The two symbols are the instance's object name and an external
model reference (FUN_00682c50 reads them as the `EditTerrain` name and its
external reference, "EditTerrain %s has unresolved external reference %s").
In `sl01` the names are `SlumsTest19`, `Line06`, `box01`, ...; the external
reference resolves to the empty name in 648 of 650 records and to
`slums/fire` in 2. Each placed instance becomes a `goe::Terrain` named
after it (FUN_006827f0, which renames a repeated name to `name|N`), and the
Terrain looks its lighting up by that name (FUN_005cf670, see
[`litedata.md`](litedata.md)).
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
  i32  model: index into `cellmdls`, -1 for none
  i32  clip: index into `cellanim`, -1 for none
  u32  name (symbol)
  u32  class (symbol): the `EntityClass` of the object
  Vec3 position
  Vec3 normal
  u32  flag
  u32  value
  f32  number
```

`FUN_006889a0` selects the version and `FUN_00688c90` parses version 4.
A marker is one object of the cell group. The meaning of the two indices is
read from the data: in `ui/hud` every marker names the model of its own
index and the seven clips are named by the seven animated objects (the health
bar has clip 0, the lore bar 1, the letterbox 2, the charm haze 3, the intent
glow 4, the rage bar 5, the enemy health bar 6). In `sl01` the 19 clips are
named once each: by the eight animated models (the gate, the spotlight, four
flocks of birds and two boats) and by eleven camera markers that have a clip
and no model. The flag, the value and the number are open. The host exposes
the markers but does not render them.

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

## Helper objects

**Answer:** the original does not draw an entity whose entity template
(`.etp`) carries `flag helperobj`. The `pyramid/*` boxes are the models of
such entities (kain emitter, SFX anchors, region and level keepers, camera
and signal markers). The port applies the same flag; it does not filter by
texture name.

The `pyramid/purple|green|red|blue` textures never appear in `cellgrup`;
they belong to shared `model` entries (`kainemitter`, `sfxobj`,
`regionkeeper`, `detector`, `cinematic`, ...) that region entities place
(`docs/FORMATS/region.md`).

| Step | Function | Behavior |
|---|---|---|
| Parse the template | `FUN_00785c8d` | Reads the `.etp` text line by line (`\n` or `\r`). Tokens are lower-cased (`FUN_0068f430`) and compared with `FUN_0068ec30` (`strcmp`); a line whose first token starts with `//` is a comment. `helperobj` sets byte `+0x66` of the template. Other flag bytes: `notplaceable` `+0x60`, `waittouseanims` `+0x61`, `static` `+0x62`, `staticcollision` `+0x63`, `collisionresponse_stop` `+0x64`, `nevercull` `+0x65`, `alwayslit` `+0x67`, `neverlit` `+0x68`, `tokenchallenge` `+0x69`, `keeprendermodeldata` `+0x6a`, `training` `+0x6b`. |
| `include <path>` | `FUN_00785c8d` | Looks the path up and splices the included text in front of the remaining lines, so its models and flags come first. `apinclude` is ignored unless `DAT_00a6ad40` is set. |
| Create the entity | `FUN_0070d0d5` | Copies template `+0x66` to bit 9 (`0x200`) of the entity flags at `+0xf0`, `+0x62` to bit 6 and `+0x63` to bit 7; clears the hidden byte at `+0x139`. |
| Draw the entity | `FUN_0070f100` | Returns without drawing when `+0x139` is set or `flags & 0x200`. |

Template files are `strmfile` entries named
`assets/etypes/<path>.etp` in the level container. A region entity's type
names its template: the type is the path under `assets/etypes/` without
extension, which is often just the file name (`sfxobj` is `sfxobj.etp`)
and sometimes a path (`camera/animcamera`, `animated/animated_nocull`,
`levels/hyldencity/cinematics/calvary_arrives/calvary_arrives`).
`include` operands are paths of the same form. Counted over the template
copies of the 177 containers, 478 of 5,671 `include` lines end in `.etp`
and 16 are bare names (`include camera` inside `camera/animcamera.etp`).
The port resolves the exact path first and then
the file name alone, for types and includes alike
(`camera.etp` is `include camera/ExportedCamera.etp`, which carries the
flag). In the 161 levels that have a region table, 4,160 of 15,584 entities
resolve to a template with `helperobj`, and one type (`lclub`) has no
template.
Reference examples from `slums/sl01`:

```
sfxobj.etp        model sfxobj\sfxobj / goscript AmbSoundScriptV2 / flag helperobj / flag static
kainemitter.etp   model kainemitter / behavior kainemitter / flag static / flag helperobj
detector.etp      model detector / goscript detectorscript / behavior detector / flag static
```

Over the 177 level containers, region entities whose type name matches a
model with a `pyramid/*` texture split as follows (entity counts):

| Carries `helperobj` (not drawn) | Entities | Does not carry it | Entities |
|---|---:|---|---:|
| `sfxobj` | 1,380 | `detector` | 758 |
| `mediumsignalface` | 587 | `planhelperline` | 514 |
| `kainemitter` | 140 | `bo2cinematic` | 268 |
| `smallsignalface` | 128 | `planhelper` | 179 |
| `regionkeeper` | 115 | `conversation` | 80 |
| `firebox` | 83 | `animdetector`, `cinematic`, `sla_platformdetector` | 13 |
| `musicobj`, `invissfxobj`, `regionloader` | 26 | | |

`hint_proximity`, `camera`, `checkpoint`, `hidden_checkpoint`,
`levelkeeper` and `bo2creature_helperstatic` also carry the flag and use
other helper models.

The port (`src/engine/entity_template.cpp`) parses `model`, `include` and
`flag helperobj`, resolves the entity type against the container's
`.etp` files and skips the entity model when the flag is set. In `sl01`
47 of the 104 region entities are helper objects, including the
`kainemitter` (`player`) whose purple box enclosed the spawn camera. An
included template is parsed once per library, since an include graph fans
out exponentially otherwise.

Collision: skipping the entity also drops its triangles from the port's
collision mesh, which is built from the render geometry (`sl01`: 49,624 to
49,378 triangles). That matches the original, where Kain is not blocked by
these volumes. The Wine captures (`wine_20261029_0110`, `wine_20261029_0052`)
start Kain at `-8.8, 57.1, 208.0`. Testing his feet and a point 30 units
above them against the model bounds placed at the entity (yaw applied), he
stands inside the `hidden_checkpoint` box (unrotated `x -28.8..11.2,
y 47.1..67.1, z 208..268`) and the `kainemitter` box (`player`, origin `z
218.2`). At the start of `after_w` his velocity goes from 0 to 100 to 230
along +X with no pause while he leaves both volumes. The evidence covers
those two boxes only (the other helpers are off his path). The port's
collision from render geometry is a simplification of the original's
`collmodc` models (above); it must not bring helper volumes back.

Open: the types in the right-hand column have no `helperobj` flag, yet the
original shows no pyramid for them. What is ruled out and what is known:

- They are not hidden. A memory dump of the Wine oracle at the `sl01`
  spawn (`wine_capture.py --dump-instances`, see `docs/ORACLE_WINE.md`,
  matched to the region entities by position) shows the hidden byte
  `+0x139` at 0 and flag `0x200` clear on all 16 `detector`, `planhelper*`,
  `conversation` and `bo2cinematic` instances. They are the same entity
  class as the drawn ones (vtable `0x87721c`).
- The `show helpers` debug option (case 6 of `FUN_004a36f0`) sets debug
  flag group 5, index `0x16` (`FUN_00690260`). Its readers draw debug
  probes and lines (for example `FUN_00711f30`); it does not gate entity
  models.
- Entity flag `0x10`, set by `PlanHelperBehavior` (`FUN_00757dc0`), only
  marks the entity for processing in one game mode (`FUN_00718490`).
- The entity model is drawn by the `goe::AnimModelInst` at `+0x90`
  (vtable `0x86eb98`) through `FUN_005c9b50` and `FUN_005dcbf0`. With a
  render LOD set (`+0x9c`), it draws that LOD. Without one it falls back to
  `FUN_005d8be0`, which draws only when the model instance (`+0x98`) has
  render data. In the dump every drawn mesh entity (`animated`,
  `animgate`, `leverf`, `psnt*`, `rat`, `umahvf`) has the LOD fields `+0x9c`
  and `+0xa0` set, and every one of these helpers has both at 0, like the
  `cl_*` particle emitters. Why their model instances carry no render data,
  although the `model` entries hold `rmd` geometry, is not traced. Static
  props that are drawn are absent from `sl01`, so the dump cannot separate
  "static" from "helper".

The port still draws those types, since no rule is proven yet.

## Skeleton pose and skinning

`vtxweigh`, `jointidx`, `animatio` and `animreel` are present in the
containers. Static cell models carry zero weights; character models use
the animation chunks. The `animreel` clip encoding is decoded
([`animreel.md`](animreel.md)) and so is the `animatio` graph
([`animatio.md`](animatio.md)). This section is how one clip poses a
skeleton and how the pose moves the vertices. The port is
`src/engine/skeleton_pose.cpp`.

### Bone record

The character model carries the skeleton twice: as the `skeleton` subchunk
of the `rmd` and as the sibling `modlskel` chunk. Both are `u32 count` and
`count` records of 104 bytes. FUN_0067bdd0 prints the record with its field
names and FUN_005e27e0 reads the same offsets:

| Offset | Type | Field | Use |
|---|---|---|---|
| 0x00 | u32 | name | Symbol |
| 0x04 | u32 | `stateMask` | Copied to the runtime bone |
| 0x08 | u32 | `stateVal` | Its top three bits are the bone's LOD level |
| 0x0C | u32 | `boneIdx` | The bone id: the `node` of a clip channel |
| 0x10 | u32 | `currentMatrixIndex` | The bone's position in the array |
| 0x14 | u32 | `parentMatrixIndex` | The parent's position; negative for none |
| 0x18 | u32 | `firstCollidePrimIndex` | |
| 0x1C | u32 | `numCollidePrim` | |
| 0x20 | u32 | `firstVertIndex` | First vertex the bone owns |
| 0x24 | u32 | `numVerts` | How many it owns |
| 0x28 | u16 | `chanMask` | Bit 0x200: the bone keeps its parent's scale |
| 0x2A | u16 | `rotOrder` | |
| 0x2C | f32[3] | `Pivot` | The joint position in model space |
| 0x38 | f32[3] | `RO` | |
| 0x44 | f32[3] | `JO` | |
| 0x50 | f32[3] | `Rot` | |
| 0x5C | f32[3] | `Scl` | |

FUN_005e27e0 turns each record into a 0x60-byte runtime bone: a pointer to
the parent (the bone at `parentMatrixIndex`), the key
`currentMatrixIndex << 16 | boneIdx`, the LOD level `stateVal >> 29`,
`chanMask`, `rotOrder` and the five vectors.

In Kain's model (104 bones) the matrix index is the position, every parent
comes before its child, `rotOrder`, `RO`, `JO` and `Rot` are 0 and `Scl` is
(-1, 1, 1) in every bone. The pose path below reads none of those four
vectors. The ids are sparse (0..93, 200, 201, 210, 211, 230, 300, 400..403).

### Pose

The clip sampler (vtable 0x86f8d4, built by FUN_0063bb40) binds nine
channels to each bone (FUN_00638920) and FUN_00637900 samples them at the
clip time: a rotation, a translation and a scale. A missing translation
component is the bone's pivot, a missing rotation is 0 and a missing scale
is 1 ([`animreel.md`](animreel.md)).

FUN_00637710 builds the bone's matrix in its parent's space. The engine
works on row vectors and every step post-multiplies:

```
M = T(-pivot) * S(scale) * Rx(rx) * Ry(ry) * Rz(rz) * S(1 / parent scale) * T(translation)
```

- The angles are radians. The helpers are FUN_00692aa0 (x), FUN_00692cb0 (y)
  and FUN_00692eb0 (z); each leaves the matrix alone below 1e-4 rad. On a
  point they are the usual right-handed rotations: x turns (y, z) into
  (y c - z s, y s + z c), y turns (x, z) into (x c + z s, z c - x s) and z
  turns (x, y) into (x c - y s, y c + x s).
- FUN_00693110 translates and FUN_00693270 scales.
- The parent-scale step runs only when the bone has a parent and its
  `chanMask` lacks 0x200. The inverse is stored per bone when its matrix is
  built: 1 for a scale of exactly 1, `1 / s` from 0.01 up and 100 below.

A clip stores the translation in the same model space as the pivot: in
`kain/run` 182 of the 204 constant translations of the bones equal the
bone's pivot, 22 are small offsets from it and 12 channels are animated.
With every channel at its default the matrix of every bone is the identity,
so the bind pose needs no inverse bind matrix.

The model-space matrix is the local matrix times the parent's
(FUN_005e43e0, which recurses to the parent and caches the result per bone;
FUN_00691030 is the product). A bone without a parent takes the entity's
world matrix, and the sampler gives the bone of index 0 an identity matrix
(0x639160): the root's channels, root motion included, never move the
skeleton. The entity moves instead ([`animreel.md`](animreel.md), Root
motion).

### Skinning

FUN_0056d130 prepares the mesh: per bone its vertex range, per vertex a
weight and a joint. Each frame FUN_0056cc20 fetches one matrix per bone and
calls the skinning routine once per bone on that bone's range. FUN_0056ec00
installs the routine from two global flags: FUN_0056ecf0 by default,
FUN_00573a40 or FUN_00573ec0 otherwise (not decompiled; *inferred*: CPU
variants of the same loop).

```
a  = v * M[owner]            owner: the bone whose range holds the vertex
b  = v * M[jointidx[v]]
v' = a + (b - a) * vtxweigh[v]
```

So `jointidx` is a second bone and `vtxweigh` its share. In Kain every one
of the 1,988 vertices is in exactly one range; 1,118 name their owner with
weight 0 and the other 870 lean on a neighbour with a weight up to 0.5.

The routine writes a position, a colour and the UV: the original lights the
vertex on the CPU and emits no normal. The port has no such pass, so
`skin_vertices` blends the normal with the same two matrices and weight for
its own shading. That is a port choice, not a translation.

### Pose blend

A clip that starts with a blend does not cut to its first frame. The machine
swaps the plain sampler for the blend sampler (vtable 0x86f91c, built by
FUN_00639990 from FUN_0063bb40) for the blend time, and back when it ends
(FUN_005e1f80). The blend sampler keeps two poses per bone in a 0x60-byte
record: the pose that was on screen and the pose of the new clip, each as a
quaternion, a translation and a scale, plus the dot product and the angle
between the two quaternions (FUN_0069b5b0). A quaternion comes from the three
angles through the rotation matrix (FUN_0069aa30).

The factor is `1 - remaining / blend time`, where the remaining time is how
far the clip clock is from `in` (FUN_00639de0); it is clamped to 0..1. A blend
record may name a transition filter that reshapes the factor.

FUN_00638310 builds the bone matrix from the record: the translation and the
scale are linear in the factor and the rotation is FUN_0069b640. That blend
takes the shorter arc (FUN_0069b5b0 negates the second quaternion when the dot
product is negative), uses the weights `1 - f` and `f` from a dot product of
0.99999 up and `sin((1 - f) a) / sin a` and `sin(f a) / sin a` below, and
normalises the result. The matrix is then the one of FUN_00637710 with that
rotation: `T(-pivot) * S * R * S(1 / parent scale) * T(t)`.

The port keeps the two poses as `BoneTransform` lists and blends them with
`blend_poses`; `local_matrix` takes the quaternion. The transition filter is
not decoded: the factor is linear.

### What the port leaves out

- The LOD collapse: FUN_005e43e0 gives a bone its parent's matrix when the
  bone's LOD level is below a threshold of the instance.
- The ranking of the matrix sources. FUN_00638690 ranks up to 16 sources per
  bone by the layer masks, and the code at 0x5d1b50 is another source that
  ends in FUN_00637710 (*inferred* from the RTTI names next to it: the
  `goe::*MatrixMap` classes). The cross blend of the run is drawn as a sum of
  two poses over every bone ([`animatio.md`](animatio.md), Kain's run state).
- The transition filter of a blend record.
- The vertex colour of the skinning routine.

Over the 134 Kain clips 15 bones (hands and face) carry a scale other than
1, and every bone but the root has 0x200, so Kain's fingers inherit the
hand's scale.

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

- Per-cell/object light-map selection (the group name to model binding),
  fog, the per-level ambient values and the `mtlctrl` bits other than
  `0x4000` and `0x10000` of the second word.
- The `mdlattr` header words, attributes with several values, the alpha test
  reference and how a model's colour parameter reaches its vertices.
- The 40/42-byte `prims` extra record.
- `cells` flags, `cellattr`/`attrs` value semantics, the `portal2`
  plane/index and the BSP traversal.
- `cellinst` external references, flags and the Euler order.
- `cellight` type-specific parameters and the marker symbol semantics.
- Which model each `cellanim` clip belongs to, and what plays it in a level.
- `model 74` has no `rmd`; its role is unknown.
- The `RO`, `JO`, `Rot` and `Scl` vectors of the bone record, `rotOrder`, the
  state mask/value and the other `chanMask` bits.
- Blending of the `animatio` layers on the skeleton ([`animatio.md`](animatio.md)) and the auxiliary `animreel` records ([`animreel.md`](animreel.md)).
- Shared-model light-map selection by model name and the remaining entity
  properties.

## Next steps

1. Use the cell grid and BSP for culling and cell visibility.
2. Select shared-model light maps and bind the `cellanim` clips to their models.
3. Match the light-map group to the cell/object and combine it with the material layers.
4. Blend the `animreel` clips on the skeleton as the `animatio` layers describe.
