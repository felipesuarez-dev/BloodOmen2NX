# `pc.rkv` (PC audio archive)

Status: **validated against the reference build**. Implementation:
`tools/bo2fmt/rkv.py`. The Spanish GoldMaster archive has 2,666 Ogg
Vorbis streams; the first one decodes as mono 44.1 kHz Vorbis.

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
build whose index is `0x18000`. This build has `index_size = 0x187f8`, so
`data_start = 0x19000`. The parser computes it and validates the first
entry with its `OggS` signature.

## Measured build

| Field | Value |
|---|---|
| File size | 208,033,792 bytes |
| `index_size` | `0x187f8` |
| Language | `spanish` |
| Section | `strstrem` |
| Entries | 2,666 |
| First entry | `streams/wav/STR_canyonswind`, 819,877 bytes, Ogg Vorbis mono 44.1 kHz |
| Last entry | `vo/WHAM_VORA_02`, declared 50,002 bytes, 49,152 stored |

The last entry declares 850 bytes more than the file contains. The
installer's file CRC covers the produced `pc.rkv`, so this is a property
of the reference archive, not a truncated extraction. The parser reports
it as `truncated` with `stored_size = 49152`; `list` marks it and
`extract` skips it unless `--allow-truncated` is passed.

## Validation

The reference parser rejects malformed input instead of returning partial
data:

- a version other than 1, or a file shorter than the 12 bytes needed for
  the version, index size and language count;
- an index whose declared size ends past the file;
- strings and counts that overrun the declared index size;
- an entry flag other than 1.

An entry whose sector and size point past the file is reported as
truncated instead of aborting the whole archive. This keeps the index
usable and makes the reference build's short last stream visible.

## Integration

- `tools/bo2fmt/rkv.py` implements list and extract.
- `extract --allow-truncated` writes the available bytes and warns.
- The parser computes `data_start`; it does not hardcode `0x1a000`.
- Synthetic fixtures cover the index, sector math and truncation.

## Open questions

- The public GOG inventory reports 2,824 streams; this Spanish build has
  2,666. The difference is not yet explained.
- Whether the last stream's declared size includes padding or the source
  archive is short.
- Whether the section name is always `strstrem`.
- The event table (`sound`, `strmfile`) lives in the `.big` files and is
  separate from this archive.

## Next steps

1. Document the sound/strmfile event table.
2. Decode more streams and compare them with the original.
3. Feed `vo/`, `streams/wav/` and `music/wav/` into the host audio
   backend.
