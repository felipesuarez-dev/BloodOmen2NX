# Textures

Status: **verified**. Every `texture` entry checked in the 179 `.big`
files carries a standard DDS payload (`DDS ` magic): 22,483 entries,
0 exceptions. Implementation: `src/engine/dds.cpp`; it decodes DXT1,
DXT3, DXT5 and A8R8G8B8 and keeps the top mip.

## Container

A `texture` chunk is a DDS file without any extra wrapper. The sample
from `sanctuary.big` is a 32x16 DXT5 image with six mip levels. The
header and payload can be handed to a DDS loader.

The font is an uncompressed A8R8G8B8 256x256 DDS plus `texfont` glyph
records: a 24-byte table header and 87 records of 20 bytes (`u8 code`,
three `0xFF`, four floats for x, y, w, h). The Spanish build uses the
same glyph codes for accented characters.

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
