# Light maps (`litedata/*.slm`)

Status: **partially verified**. The first five entries of
`sanctuary.slm` parse and render; the large trailing section of the
`grey_brick` entry is still open.

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

The first entry starts at `12 + group_length + 12`.

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
entry is followed by about 376 KB that is not part of the 64x64 index
buffer; it is probably additional light-map pages or a second section
whose layout is not decoded yet.

## Host

`parse_lightmaps` in `src/engine/cell_group.cpp` reads the entries,
expands the palette and indices into a grayscale `Image`, and matches
the entries to `LevelGeometry::texture_names` by name suffix. The model
reader stores the second UV set in `LevelGeometry::lightmap_texcoords`.

`bo2nx --level --lightmap` samples the light map with those UVs and uses
its luminance as the light factor (`texture × tint × light map`). The
`--material` tint can be combined with it. In `sanctuary` 5 of 34
textures have a light map; the 640x480 frame changes in 28,551 pixels
(mean 0.114 with the light map, 0.107 without).

## Open questions

- The `grey_brick` trailing section (about 376 KB).
- The 12 header bytes after the group name.
- The exact combination of the light map with `mlyrcolr`/`mlyrctrl`.
- Whether the second UV set is always the light-map set.
