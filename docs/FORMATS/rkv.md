# `pc.rkv` (PC audio archive)

Status: **specification from independent public parsers**; not yet
validated against this Spanish build. Implementation:
`tools/bo2fmt/rkv.py`.

`pc.rkv` is the **audio** archive, not the master asset file. It stores
Ogg Vorbis streams for voice (`vo/`), ambience (`streams/wav/`) and music
(`music/wav/`). Level, model, texture and entity data live in `.big`
files.

## Header and index

Little-endian:

```
u32 version = 1
u32 index_size
u32 language_count
  string language
  u32 section_count
    string section                 ("strstrem")
    u32 entry_count
    u32 0
    entry: string name; u32 1; u32 sector; u32 size; u32 0
padding
data_start = index_size + 8 + 0x800
data       = data_start + sector * 2048
```

Strings are length-prefixed (`u32 length` followed by bytes), not
NUL-terminated.

`0x1a000` is not a format constant: it is `index_size + 8 + 0x800` for a
build whose index is `0x18000`. Compute it and optionally validate it by
checking that the first entry starts with `OggS`.

## Integration

- `tools/bo2fmt/rkv.py` implements list and extract.
- The parser computes `data_start`; it does not hardcode `0x1a000`.
- Synthetic fixtures cover the index and sector math.

## Open questions

- The language string and index size of the Spanish GoldMaster.
- Whether all 2,824 streams reported for the GOG build are present here.
- Whether the section name is always `strstrem`.

## Next steps

1. Validate against a real `pc.rkv` from an installed tree.
2. Count streams and compare with the public inventory.
3. Decode one Ogg stream and compare it with the original.
4. Document the event table (`sound`, `strmfile`) separately.
