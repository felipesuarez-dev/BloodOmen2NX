# Light maps (`litedata/*.slm`)

Status: **partially verified**. The entry scanner finds 71 unique
light maps across the cell/object groups of `sanctuary.slm`; 16 of the
34 `sanctuary` textures resolve. The per-cell/object selection and the
per-vertex data are still open.

`litedata` entries hold static light maps for the level. The reference
file is `sanctuary.big` -> `litedata` -> `sanctuary.slm` (388,673
bytes). The light map is texture-space: each entry is named after the
texture it lights, and the model's second UV set indexes it.

## Header

```
u32 version = 1
u32 header_size = 112
u32 group name length
char group[group name length]   (no NUL)
12 bytes (observed; not interpreted)
entries...
```

The first entry starts at `12 + group_length + 12`. After a group's
entries the file stores per-vertex data (`u32 count` + `count × 16
bytes`) and then another group header (`u32 group length`, group name,
12 bytes). The group names are cell or object names (`cryptexithall`,
`crypt`, `cryptentrancehall`, `coffin01`, `roofbit_08`, `mesh13`,
`object37`, `sanctuary/pilarb`, `box02`, ...).

## Entry

```
u32  name length (no NUL)
char name[name length]
u16  version = 1
u8   format = 4
u8   flags = 0x20
u16  0x10
u16  0x61
u16  width
u16  height
16 × u32 palette               (luminance in the high byte)
(width * height + 1) / 2 bytes of 4-bit indices
```

The name is the group prefix plus the normalized texture name:
`cryptexithall-inner city-textures-walls-sewbrick3` maps to
`Inner City/Textures/Walls/SewBrick3` (lowercase, `/` becomes `-`).
Indices are packed high nibble first. The palette is 16 colors with the
luminance in the high byte of each `u32`.

## Per-vertex colours

The `u32 count` that follows a group's entries is its vertex count, and
the `count × 16` bytes after it are one RGBA colour of four little-endian
`f32` per vertex. Measured on `slums/sl01.slm` (2,032,896 bytes): all 286
groups carry the array, 38,849 vertices in total, alpha is always 1.0, the
channels range from 0.157 to 1.0 and average (0.855, 0.825, 0.910), a
slightly blue white. The mean light-map palette luminance over the 286
groups' entries is 135/255 (range 12 to 194). Level geometry has no vertex
colour of its own in `prims`, and the original device runs with D3D
lighting off, so these arrays are the only per-vertex colour source for
lit cell models (`models.md`, "Why `--material diffuse|layers` render
near-black"). The host reads them (at most 0x8000 per group, the limit of
FUN_006368c0) and copies each lit instance's array onto its placed
vertices.

## Measured entries

| Name suffix | Width | Height | Index bytes |
|---|---:|---:|---:|
| `inner city-textures-walls-sewbrick3` | 64 | 32 | 1,024 |
| `inner city-textures-floors-sewwalk1` | 128 | 128 | 8,192 |
| `inner city-textures-trims-sewwalkedge` | 64 | 16 | 512 |
| `inner city-textures-trims-conctrim1` | 64 | 8 | 256 |
| `inner city-textures-walls-grey_brick` | 64 | 64 | 2,048 |

The first four entries end exactly at the next entry. The `grey_brick`
entry is followed by about 376 KB, but that is not part of its 64x64
index buffer: it contains additional groups (coffin, cryptentrancehall,
roofbit, mesh, object, pilar, box, ...) and their entries. The host
scanner treats every valid entry signature in the file as a candidate,
so it collects 71 unique entries from sanctuary; the first matching
entry per texture wins.

## Group header and lighting modes

After the group name, FUN_00635f20 reads:

```
u8  flag A   mode 1: the per-vertex colours follow when set
u8  flag B   mode 2: the per-prim light maps follow when set
u8  flag C   mode 2: the per-vertex colours follow when clear
u32 prim count           (at most 0x80)
u8  per prim: has a light map (mode 2, under flag B)
```

That is 7 bytes plus one per prim, the 9, 12 and 13 bytes seen before.
FUN_00636bd0 then follows the lighting mode of the level's `liteinfo`
object (global `0x00A6AC40`, vtable `0x86eea8`, field `+0x1c`):

- **Mode 1:** the per-vertex colours only (FUN_00636bd0).
- **Mode 2:** FUN_006368c0 reads one light-map entry (FUN_00636380,
  FUN_00636200) for each prim whose byte is set, then the per-vertex
  colours unless flag C is set.

The Wine oracle reads mode 2 at the `sl01` spawn (capture
`wine_lightmode_1`).

At draw time the render-state cache sets stage 1 to `COLOROP MODULATE`,
`ALPHAOP SELECTARG2` with the light map, or disables stage 1 without one
(`0x565bf3`-`0x565c52`). The `.slm` palette carries only the high byte
(`0xLL000000`), so the light map acts as a luminance factor. The exact
texture format the engine builds from it (FUN_0061bc10, FUN_005fe500) is
not traced.

## Binding to the level geometry

The original keys every lighting group by its object name. The
`litedata` resource is loaded by `goe::RMDManager` (vtable `0x86f414`,
version 1 only: FUN_0060b670). A `goe::RMDLoadContext` (`0x86f404`,
FUN_0060a9c0) reads a group count and parses each group with FUN_0060b040.
That parser reads the object name symbol and the lighting data
(FUN_00636bd0) and inserts both into a map keyed by the symbol, with the
errors "Read lighting for an object with no name.  File is probably
bad." and "ERROR: there are multiple objects named '%s'".

Each `cellinst` record becomes a `goe::Terrain` named after the instance
(FUN_00682c50, FUN_006827f0). The Terrain constructor (FUN_005cf670)
lower-cases its name and looks it up in that map (FUN_0060b460). A
repeated instance name is renamed `name|N` first. No `.slm` group in the
installed game contains a `|` (38,663 entries scanned), so only the first
instance of a repeated name is lit; 81 of the 47,650 instances of all
levels repeat a name. In `sl01` all 286 groups match an instance name
exactly (650 instances).

## Host

`parse_lightmaps` in `src/engine/cell_group.cpp` scans the whole file for
entry signatures, expands the palette and indices into a grayscale
`Image`, and deduplicates by full name. It also reads the group vertex
count: the `u32` immediately after the group's last entry. The observed
counts match the model vertex counts (for example `cryptexithall` 116,
`crypt` 2,298, `coffin01..05` 48, `cryptentrancehall` 652, the roofbits
153/161, the pilars 206/122).

`assign_model_lightmaps` groups the entries by their prefix. A placed
instance takes the group named after it (lower-cased), as above, and a
repeated name takes none. Only without a symbol table, when the instances
have no names, does it fall back to the old heuristic: match a model's
vertex count to a group and let placed instances consume the groups in
instance order. A triangle with no instance falls back to its model's
group. Within the group it
picks the entry whose name ends with the model's texture name. In
`sanctuary` this produces 62 model light maps covering 2,769 triangles;
`cy01` now covers 15,217 triangles and `lc01` 7,116. Entity models use
`assign_named_model_lightmaps` (group name equals the model short name)
before the vertex-count fallback.
The model reader stores the second UV set in
`LevelGeometry::lightmap_texcoords`. Shared `model` entries (props not in
`cellmdls`) still need their own instance mapping.

`bo2nx --level --lightmap` samples the light map with those UVs and uses
its luminance as the light factor. When the level has `.slm` vertex
colours it draws the original equation, `texture × vertex colour × light
map` (stage 0 modulates the texture with the vertex diffuse because D3D
lighting is off; stage 1 modulates with the light map). Unlit vertices are
white; what the original does for a Terrain without a lighting group is
not traced. Without vertex colours it keeps `texture × tint × light map`. The
`--material` tint can be combined with it. In `sanctuary` 16 of 34
textures resolve to a light map; the frame changes by about 0.0098 RMSE
against the no-lightmap + five-entry version.

## Open questions

- Instance mapping for shared `model` entries that are not placed by
  `cellinst`. Region entities now use group-name matching (with a
  vertex-count fallback) for their models.
- What flag A means in mode 2.
- How the per-vertex colours (below) combine with the light map in the
  original; a spike of `texture × vertex colour × light map` ended farther
  from the Wine frame than the unlit preview (`models.md`, BO2NX-68).
- Whether the second UV set is always the light-map set.
