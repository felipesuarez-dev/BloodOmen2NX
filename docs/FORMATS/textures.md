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
