# Textures

Status: **verified**. Every `texture` entry checked in the 179 `.big`
files carries a standard DDS payload (`DDS ` magic): 22,483 entries,
0 exceptions. Implementation: `src/engine/dds.cpp`; it decodes DXT1,
DXT3, DXT5 and A8R8G8B8 and keeps the top mip.

## Container

A `texture` chunk is a DDS file without any extra wrapper. The sample
from `sanctuary.big` is a 32x16 DXT5 image with six mip levels. The
header and payload can be handed to a DDS loader.

## Font

The game has one font for every language: the `texfont` entry `main/main`
of `code.big` (a second entry, `main/main.fnt`, has the same bytes) and the
texture it names, `fonts/revised`, an uncompressed A8R8G8B8 image of
256 x 256. The loader is `FUN_005df900`.

```
char[8] "texfont"
u32     size of the entry (1,780)
u32     0
u16     texture (a symbol of the entry), u16 0xffff
f32     20.0
f32     2.0
f32     5.0
f32     8.0
u8      upper-case-only flag (0), u8 0xff
u16     glyph count (87)
glyph[count], 20 bytes each:
  u8   code, three 0xff
  f32  x, y, width, height   (texels)
```

- The glyph rectangle is measured from the bottom of the image: the loader
  keeps `texture height - y`. In the rows of the DDS, a glyph covers rows
  `256 - y - height` to `256 - y`.
- The shape of a glyph is in the alpha channel. The colour channels are white
  under the letters and carry the colours of the controller buttons.
- The loader stores the four floats at font `+0x10`, `+0x18`, `+0x1c` and
  `+0x14`, in file order. What they mean (line height, spacing) is not read.
- With the flag set, the loader gives every code from `A` to `Z` a second
  code 0x20 above it and warns when that lower-case code already exists.
  This font has the flag clear and defines both cases itself.

The 87 codes:

| Codes | Glyphs |
|---|---|
| `a` to `z` | the capital letters A to Z: the font has no lower-case shapes |
| `0` to `9`, `! % ' ( ) + , - . / : ; ?` | themselves |
| `A` to `Z` | accented capitals and inverted marks, see [`strtable.md`](strtable.md#text-encoding) |
| `0xc8` to `0xd3` | controller buttons: square, triangle, circle, cross, L1, L2, R1, R2, up, down, right, left |

There is no glyph for the space, for `"`, `#`, `$`, `&`, `*`, `<`, `=`, `>`,
`@`, `[`, `\`, `]`, `^`, `_`, `` ` ``, `{`, `|`, `}`, `~`, or for any other
byte above 0x7f. Widths go from 4 to 28 texels and heights are 19 or 20.

## Inspection

`tools/bo2fmt/textures.py` lists the texture entries of a container and
exports each top mip to PNG with an `index.json` (entry, size, format).
The level containers hold 44 groups of repeated texture names with
different payloads; the exporter keeps every copy with a `~N` suffix.
`slums/sl01.big` holds 239 textures: 189 DXT1, 32 DXT3, 17 DXT5 and one
uncompressed 32-bit image; all 239 decode.

```sh
python3 tools/bo2fmt/textures.py export \
  external/installed/media/gogames/bo2/resources/levels/slums/sl01.big \
  --out external/extracted/sl01
```

## Helper materials

`pyramid/purple`, `pyramid/green`, `pyramid/red` and `pyramid/blue` are
8x8 DXT5 textures of one flat colour at alpha 128 (`pyramid/purple` is
`255,0,255,128`). `pyramid/purple` appears in 151 of the 177 level
containers, and at least one `pyramid/*` texture in 159. They texture the
models of helper entities (kain emitter, SFX anchors, region keepers), not
cell geometry. In `slums/sl01` the `kainemitter` entity's `pyramid/purple`
box enclosed the spawn camera and tinted the whole frame magenta. The
original does not draw an entity whose template carries `flag helperobj`;
see "Helper objects" in [`models.md`](models.md). The texture name plays no
part in that decision.

## Open questions

- Mipmap and swizzle handling: the payloads carry mip chains; the host
  loader currently needs the top level only.
- Atlas packing and material binding.
- The per-face texture index in the model payload.
- Which textures the level renderer must bind per submesh.

## Next steps

1. Render a real texture in the level pipeline (the preview already uses
   one).
2. Add mip selection and the remaining codecs if a real texture needs
   them.
3. Find the texture loader and the pixel-conversion routine in `bo2.exe`
   only where the DDS header is not enough.
4. Document the material binding used by the model payload.
