# Textures

Status: **payload identified as DDS** by public parsers; not yet extracted
from our build.

A `texture` chunk inside a `.big` carries a standard DDS payload (`DDS `
magic). The font is an uncompressed A8R8G8B8 256x256 DDS plus `texfont`
glyph records: a 24-byte table header and 87 records of 20 bytes
(`u8 code`, three `0xFF`, four floats for x, y, w, h). The Spanish build
uses the same glyph codes for accented characters.

## Open questions

- Whether every `texture` chunk is DDS or some are another codec.
- Mipmap and swizzle handling.
- Atlas packing and material binding.
- The per-face texture index in the model payload.

## Next steps

1. Decode the `.dat` container first.
2. Find the texture loader and the pixel-conversion routine in `bo2.exe`.
3. Decode one texture and compare it with a capture from the original.
4. Document the format with offsets and a synthetic fixture.
