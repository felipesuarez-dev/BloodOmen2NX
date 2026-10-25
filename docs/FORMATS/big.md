# `.big` container (`goefile`)

Status: **validated against the reference build**. Implementation:
`tools/bo2fmt/big.py` (reference) and `src/engine/big_archive.cpp` (C++
reader). The parser read 179 `.big` files (246,332 entries) from the
installed tree without errors; see "Coverage". The C++ reader passes the
unit tests and lists the installed containers.

`goefile` is where the game content lives: geometry, models, textures,
samples, `tunedata`, `cellgrup`, `region`, `planinfo`, `bpackage`,
`animatio`, `animreel`, and the other resource types below. The static
level shell is the `cellgrup` entry; its model and `rmd` layout is in
`docs/FORMATS/models.md`. It is not the
CDC `bigfile.dat` used by Soul Reaver; that format does not apply.

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
u32     symbols size
u32     0
u32     n
n       u16 values, padded to the symbols size
u32     data length (payload only)
data, padded to 4
```

`symbols size` is the byte distance from the `symlist` field (entry
`+0x14`) to the data length. In the entries checked it is `align4(20 + 2*n)`: 20 bytes for an
empty symbol list and 1448 bytes for the 713-symbol `bpackage` in
`sanctuary.big`.

The data length therefore lives at:

```
data_length_at = entry_start + 0x14 + symbols_size
```

The payload starts immediately after the data length. There is no `u64`
trailer in this build. The payload offset is still derived from the end
of the entry, which is the robust public formula:

```
data = entry_start + entry_length - align4(data_length)
```

The parser checks that both formulas agree and that the payload fits
inside the entry.

## Validation

The reference parsers reject malformed input instead of returning partial
data:

- a file shorter than `0x24` bytes or without the `goefile\0` magic;
- a directory that starts before `0x24` or ends past the file;
- names that do not terminate before the directory end;
- an entry shorter than the 44-byte fixed part or ending past the file;
- a `symbols size` that places the data length outside the entry;
- a payload length whose 4-byte alignment does not fit the entry;
- non-zero bytes after the last entry.

## Coverage

Measured on the installed tree (`external/installed`, 179 `.big` files):

| Type | Entries |
|---|---:|
| `lipsync` | 71,182 |
| `sound` | 62,073 |
| `animreel` | 32,791 |
| `texture` | 22,483 |
| `sample` | 21,251 |
| `strmfile` | 16,428 |
| `effect` | 7,865 |
| `model` | 5,168 |
| `animatio` | 4,081 |
| `tunedata` | 1,720 |
| `bpackage` | 480 |
| `cellgrup` | 236 |
| `region` | 165 |
| `planinfo` | 159 |
| `litedata` | 142 |
| `strtable` | 92 |
| `musicbnk` | 11 |
| `texfont` | 4 |
| `liteinfo` | 1 |

Every texture entry checked starts with the `DDS ` magic. A sample from
`sanctuary.big` is a 32x16 DXT5 image with six mip levels. The model
payload checked starts with a length, a `default` name and a size,
followed by an `rmd` chunk and named records (`default`, `position`, ...).
The full model layout is open.

The name table can hold more than 3,000 names (the sanctuary file has
3,332, covering four languages plus `default`). The `name index` resolves
the entry name. The `n` u16 values are resource symbol references: the
`cellgrup` uses them to resolve its texture names, and `BigEntry.symbols`
exposes them. The `language index` is stored but not interpreted.

## Open questions

- The meaning of the language index and of the `n` u16 values, beyond
  the `cellgrup` symbol list.
- `region`, `planinfo`, `animatio`, `animreel`, `strtable`, `musicbnk`,
  `texfont`, `liteinfo`.
- The `cellinst`, `cellight` and `cellmark` records after `cellmdls` (the `cells`, `attrs` and `cellbsp` tail is decoded in `docs/FORMATS/models.md`).
- Which types the renderer and audio backend consume first.

## Next steps

1. Decode the material lighting equation and `cellinst`, `cellight` and `cellmark`.
2. Document `region` and `planinfo` for the level loader.
3. Port the remaining validated readers to `src/engine/`.
