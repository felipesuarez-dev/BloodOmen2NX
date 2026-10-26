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

## Host

`parse_lightmaps` in `src/engine/cell_group.cpp` scans the whole file for
entry signatures, expands the palette and indices into a grayscale
`Image`, and deduplicates by full name. It also reads the group vertex
count: the `u32` immediately after the group's last entry. The observed
counts match the model vertex counts (for example `cryptexithall` 116,
`crypt` 2,298, `coffin01..05` 48, `cryptentrancehall` 652, the roofbits
153/161, the pilars 206/122).

`assign_model_lightmaps` groups the entries by their prefix and matches a
model's vertex count to a group. Placed instances consume groups in
instance order, so two instances of one model can take different prop
groups; a triangle with no instance mapping falls back to its model's
group. The six 48-vertex coffins and the six 153/161-vertex roofbits get
different groups instead of sharing the first one. Within the group it
picks the entry whose name ends with the model's texture name. In
`sanctuary` this produces 62 model light maps covering 2,769 triangles;
`cy01` now covers 15,217 triangles and `lc01` 7,116.
The model reader stores the second UV set in
`LevelGeometry::lightmap_texcoords`. Shared `model` entries (props not in
`cellmdls`) still need their own instance mapping.

`bo2nx --level --lightmap` samples the light map with those UVs and uses
its luminance as the light factor (`texture × tint × light map`). The
`--material` tint can be combined with it. In `sanctuary` 16 of 34
textures resolve to a light map; the frame changes by about 0.0098 RMSE
against the no-lightmap + five-entry version.

## Open questions

- Instance mapping for the shared `model` entries (props not in
  `cellmdls`).
- The group header fields after the name (12, 13 or 9 bytes depending on
  the group).
- The per-vertex data after each group (`u32 count` + `count × 16
  bytes`); it may be vertex colours or light-map UVs.
- The exact combination of the light map with `mlyrcolr`/`mlyrctrl`.
- Whether the second UV set is always the light-map set.
