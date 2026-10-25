# `game.erg`

Status: **verified** (text configuration, parser and round-trip fixture).

## Where it comes from

The file is not on the ISO. It lives in the installer's SFXI container
(`install2.dat` -> `FILESYS/107` -> `game.erg`, 754 compressed bytes,
1,708 uncompressed) and the installer script copies it to `data/`.

## Shape

A plain-text, line-oriented configuration file. It is not a binary
container and not the level data. It contains:

- section-style keys with quoted paths, for example feature flags,
  debug switches and language settings;
- runtime paths: `basePath`, `ArchiveFile`, `Level`, movie paths;
- numeric tuning values: render heap size, scene display-list size, and
  similar.

Two keys matter for the port:

```
ArchiveFile="...pc.rkv"
usebigfile=1
```

They confirm that the asset payload lives in `pc.rkv`, not in the
numbered `.dat` files. The installer expands `pc.rkv` from one of those
chunks.

## Grammar

One entry per line, CRLF in the reference file:

```
key=value
"key with spaces\and backslashes"=1
"string key"="string value"
```

Keys and values may be quoted with `"`. Backslashes are literal path
separators, not escapes; there are no embedded quotes. Unquoted values
are integers or identifiers. Blank lines are preserved by the encoder.
Leading and trailing spaces/tabs outside quotes are ignored.

## Implementation

`include/bo2/engine/erg.hpp` and `src/engine/erg.cpp` implement
`ErgConfig::parse`, `serialize`, `find` and the entry list. The parser
throws on a missing `=`, an unterminated quote or trailing text after a
quoted value. The unit fixture covers quoted keys/values, backslashes,
empty strings and CRLF; `serialize()` reproduces it.

`bo2nx --erg <file>` reads a real file and prints the entries. The
reference `data/game.erg` parses 52 entries, including:

```
LocaleSet="english,french,italian,german,spanish"
Level="slums/sl01"
basePath="/gogames/bo2/"
ArchiveFile="gogames\bo2\pc.rkv"
usebigfile=1
```

## Open questions

- Which keys the engine reads at boot and which are debug-only.
- Whether the file is rewritten by the game (settings) or read-only.
- The meaning of the `-` prefixed debug keys.

## Next steps

1. Use `basePath`, `Level` and `ArchiveFile` in the level loader.
2. Find the boot-time key lookup in `bo2.exe` and classify the keys.
3. Keep the sample file local; only the schema and key names go in this
   repository.
