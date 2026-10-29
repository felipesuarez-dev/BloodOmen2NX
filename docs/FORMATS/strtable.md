# `strtable` string tables

Status: **layout verified** on all 92 `strtable` entries of the installed
tree; the **accent mapping is inferred**. Implementation:
`tools/bo2fmt/strtable.py` (reference) and `src/engine/string_table.cpp`
(C++ reader). Container and language rules: [`big.md`](big.md#language).

A `strtable` maps a key to a text, and both are **names of the container
name table**, not bytes in the payload. The port reads the Spanish copy of
a table by default, because the Spanish build forces the `spanish` locale
(FUN_004aae40).

## Quick path

```sh
python3 tools/bo2fmt/strtable.py dump external/installed/media/gogames/bo2/code.big --entry ui/UI
# key<TAB>text, Spanish by default; --locale default, --raw for the stored text
```

```cpp
const auto archive = BigArchive::load(path);
const BigEntry* entry = archive.find("strtable", "ui/UI", kDefaultLocale);
const StringTable table = StringTable::parse(archive, *entry);
const std::string* stored = table.find("start_game_menu_item_name");
std::string shown = display_text(*stored, kDefaultLocale);   // UTF-8
```

## Layout

Little-endian. The payload is a header followed by records:

```
char[8] "default\0"   language of the table header, always "default"
u32     payload size  equals the entry data length (92 of 92)
u32     1             constant in all 92 tables, meaning unknown
u32     2             constant in all 92 tables, meaning unknown
repeat until the end:
  u16 7         record tag
  u16 key_ref
  u16 value_ref
  u16 0xffff    terminator
```

`key_ref` and `value_ref` index the entry's own symbol list (the `n` u16
values of the `.big` entry, `BigEntry.symbols`). Each symbol is an index
into the container name table:

```
key   = names[ symbols[key_ref] ]
value = names[ symbols[value_ref] ]
```

Different keys can share one value symbol. No table has a duplicate key,
and no reference falls outside the symbol list or the name table.

Split the payload on `0xffff` and do not assume a fixed record size.

## Irregular record

One table has a record that is not `[7, key, value]`: the Italian
`ui/UI` (language 3). Up to its terminator it holds nine words starting with
`0x0107` instead of `7`, and the neighbouring record is merged into it
(the table has 135 regular records where the other languages have 137).
The words after the tag do not follow the key/value pair shape, and the
encoding is not decoded.

The readers keep such a record raw (`StringTable::irregular()`, the second
value of `parse_strtable`) and leave it out of lookups. The other
languages, including Spanish, have none.

## Which tables exist

| Table | Container | Languages |
|---|---|---|
| `ui/UI` (menus, pause, options, saves) | `code.big` | 0 to 5 |
| `<level>_hints` (tutorial hints) | one per level `.big` | 0 to 5; a few levels only ship language 0 |

17 tables have language 0 and 15 each have languages 1 to 5, 92 in all.
Pick the table with `find(type, name, locale)` (`big.md`): the `spanish`
table when it exists, else the `default` copy.

## Text encoding

The stored text is lower-case ASCII. **Upper-case letters stand for
accented glyphs** of the game font (`fonts/revised`), which has no separate
per-language font. Spanish uses seven placeholders:

| Stored | Displayed | Unicode |
|---|---|---|
| `T` | a with acute | U+00E1 |
| `C` | e with acute | U+00E9 |
| `U` | i with acute | U+00ED |
| `V` | o with acute | U+00F3 |
| `W` | u with acute | U+00FA |
| `X` | n with tilde | U+00F1 |
| `Y` | inverted question mark | U+00BF |

**Inferred, not decoded.** The mapping comes from word evidence: in all 238
records of the Spanish tables the only upper-case letters outside control codes are
exactly these seven, and each one turns the surrounding word into correct
Spanish. The glyph art was not read, so the mapping is not confirmed against
the font. `display_text` / `display_text()` apply it for the `spanish` locale
and return the text unchanged for other locales. French, German and Italian
use their own placeholder letters; they are not mapped because the port runs
the Spanish build.

Control codes appear in the text. All the ones seen are lower case, so the
accent mapping leaves them alone (no upper-case letter follows `@`, `\` or `%` in
any Spanish record):

| Code | Count in Spanish | Likely meaning (inferred from use) |
|---|---:|---|
| `@c` | 188 | centre the line |
| `@n` | 175 | line break |
| `\x`, `\q`, `\c`, `\2` | 21, 14, 1, 1 | button icon placeholder |
| `\` and a space | 3 | unknown |
| `%d` | 2 | number substituted at run time |

Neither the control codes nor the glyph rendering are interpreted by the
reader; they are the job of the text renderer.

## Engine side

The table is requested by name through `StringTableManager` (RTTI
`.?AVStringTableManager@goe@@`, vtable 0x876474): FUN_006a90c0 gets a table
by name and FUN_006a8f40 looks it up in the manager's list. The payload
decoder behind it is a generic record reader that was not followed.

## Open questions

- The constants `1` and `2` of the header.
- The encoding of the irregular record and the tag `0x0107`.
- The glyph table of the font, to confirm or correct the accent mapping
  and to map the other languages.
- Which of two same-named tables the manager keeps; the port prefers the
  locale entry (`big.md`).
