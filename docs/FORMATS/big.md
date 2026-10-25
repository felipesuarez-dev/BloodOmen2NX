# `.big` container (`goefile`)

Status: **specification from independent public parsers**; our parser is
experimental until it round-trips a real file. Implementation:
`tools/bo2fmt/big.py`.

`goefile` is where the game content lives: geometry, models, textures,
samples, `tunedata`, `cellgrup`, `region`, `planinfo`, `bpackage`,
`animatio` and `animreel`. It is not the CDC `bigfile.dat` used by Soul
Reaver; that format does not apply.

## Header

Little-endian:

```
0x00  char[8]  "goefile\0"
0x08  u32      total file length
0x0C  u32      unknown (1)
0x10  char[8]  "symlist\0"
0x18  u32      directory length, including this header
0x1C  u32      0
0x20  u32      name count
0x24  ...      NUL-terminated names
      pad to even offset
```

Entries start at `0x10 + directory_length`.

## Entry

```
char[8] type
u32     entry length, including this header and the data padding
u32     0
u16     name index
u16     language index
char[8] "symlist\0"
u32     unknown (20 or 28)
u32     0
u32     n; n u16 values
u32     data length (payload only)
u64     unknown
data, padded to 4
```

The payload offset is derived from the end of the entry, which is the
robust formula used by independent parsers:

```
data = entry_start + entry_length - align4(data_length)
```

This avoids depending on the variable `unknown` field and on the `n`
values. The parser rejects entries whose computed range is inconsistent.

## Validation

The reference parser rejects malformed input instead of returning partial
data:

- a file shorter than `0x24` bytes or without the `goefile\0` magic;
- a directory that starts before `0x24` or ends past the file;
- names that do not terminate before the directory end;
- an entry shorter than the fixed header or ending past the file;
- a symbol list (`n` u16 values) or a payload length that does not fit
  inside the entry;
- non-zero bytes after the last entry.

## Resource types observed

`goefile`, `symlist`, `bpackage`, `cellgrup`, `effect`, `lipsync`,
`litedata`, `model`, `planinfo`, `region`, `sample`, `sound`, `strmfile`,
`texture`, `tunedata`, `animatio`, `animreel`.

Textures carry a DDS payload (`DDS ` magic).

## Open questions

- The meaning of the language index and of the `n` u16 values.
- Whether the two offset formulas (GameExtractor and `BO2_patcher`) agree
  on every entry of our Spanish build.
- Per-type payload layouts: models, `cellgrup`, `region`, `planinfo`,
  `animatio`, `animreel`.

## Next steps

1. Validate the parser against a real `.big` from an installed tree.
2. Diff the two public offset formulas on every entry.
3. Extract one `texture` (DDS) and one `model` and document the payload.
4. Port the validated parser to `src/engine/`.
