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
u16     language index (see "Language")
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
inside the entry. Some entries carry a small gap between the data-length
field and the payload; the reader skips it and the host writer preserves
it, so an unpacked and unchanged container repacks byte for byte.

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
`cellgrup` uses them to resolve its texture names and the `strtable` uses
them for its keys and text (`docs/FORMATS/strtable.md`); `BigEntry.symbols`
exposes them.

## Language

The `language index` is an index into the container's own name table, and
all 179 installed containers start that table with the same six names:

| Index | Name | Meaning |
|---:|---|---|
| 0 | `default` | Copy used by every locale; for English it is the only copy |
| 1 | `english` | |
| 2 | `french` | |
| 3 | `italian` | |
| 4 | `german` | |
| 5 | `spanish` | The language the Spanish build runs in |

Only three types carry a language other than 0 (measured on all 179
files):

| Type | Languages present |
|---|---|
| `strtable` | 92 entries: 17 with language 0, 15 for each of 1 to 5 |
| `lipsync` | one entry per voice clip and language, about 11.9k each; language 0 matched the English entry byte for byte on the one container compared |
| `texture` | 44 load screens (11 levels x 4) for languages 2 to 5, plus language 0; there is no language-1 texture |

Every resource that has a non-zero language also has a language 0 copy
(checked on all containers). The copies are not stored in precedence
order: `code.big` lists `ui/UI` as languages 1, 2, 3, 4, 5 and then 0.

**Engine rule.** The chunk reader FUN_006c8fb0 reads the entry name, then the
language, and keeps the entry only when the language is 0, the `default`
symbol (initialised by FUN_006c81d0) or the current locale
(`GetCurrent()`, FUN_00697dd0); any other entry is skipped with FUN_006a03c0.
So the engine loads the entry of the current locale and the `default` copy
of the same resource. Which one a lookup returns is decided where the
resource is registered and is not decompiled; the oracle shows the locale
entry winning, so the port prefers the locale entry and falls back to
`default`.

**Port rule.** `BigArchive::find(type, name, locale)` returns the entry whose
language name equals `locale`, else the `default` copy, else nothing (an
entry that only exists in other languages is skipped, as in the engine).
`find(type, name)` is unchanged and returns the first match of any
language, which is what the non-localized types need. The Python
reference is `big.find_localized`.

**Locale.** The locale is a name (a symbol in the engine), not a number.
The Spanish build forces `spanish`: Game::Init FUN_00515920 reads the
`LocaleSet` list of `game.erg` into a locale list (FUN_00697d80 adds one,
the constructor FUN_006962e0 puts `default` first), and the GameSpecific
step FUN_004aae40, called after `PreCode.big` and before `code.big` is
loaded, calls Select (FUN_00697d90) with `spanish` and stores
`Locale=spanish` in the config. The language chooser the same function can
open (the `LanguageSetupUI` class) is never shown because `Locale` is
already set, and there is no in-game language option and no OS locale
detection. The port mirrors this with `kDefaultLocale = "spanish"`
(`big.DEFAULT_LOCALE` in Python).

## Open questions

- The meaning of the `n` u16 values beyond the `cellgrup` and `strtable`
  symbol lists.
- Which duplicate the engine returns when an entry and its `default` copy
  collide (FUN_006c8fb0 only filters); the port prefers the locale entry,
  which matches the oracle.
- `planinfo`, `animatio`, `musicbnk`, `texfont` and `liteinfo`. `region`,
  `animreel` and `strtable` are decoded in `docs/FORMATS/region.md`,
  `animreel.md` and `strtable.md`.
- The `region`, `planinfo` and shared `model` records (the `cellgrup` tail is decoded in `docs/FORMATS/models.md`).
- Which types the renderer and audio backend consume first.

## Next steps

1. Bind the shared `model` entries and decode the material lighting equation.
2. Document `region` and `planinfo` for the level loader.
3. Port the remaining validated readers to `src/engine/`.
