# Textures

Status: **unknown**.

## Open questions

- Pixel formats: indexed palettes, 16-bit, 32-bit, compression.
- Palette layout and endianness.
- Texture pages and atlas packing.
- Mipmaps and swizzling.
- Where the texture table lives.

## Next steps

1. Decode the `.dat` container first.
2. Find the texture loader and the pixel-conversion routine in `bo2.exe`.
3. Decode one texture and compare it with a capture from the original.
4. Document the format with offsets and a synthetic fixture.
