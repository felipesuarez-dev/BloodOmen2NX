# SFXI installer container

Status: **verified**. Implemented in `tools/bo2fmt/sfxi.py` and covered by
`tests/python/test_sfxi.py`.

## Where it appears

The PC installer (`install2.dat`) keeps its support files in a PE resource
`FILESYS/107`. The resource is an SFXI container with 24 entries: the
installer script, `game.erg`, `list.txt`, the installer bitmaps/raw
images, `binkw32.dll`, and a few small data files.

## Layout

Little-endian:

```
magic[4]              "SFXI"
u32                   total size
repeat:
    char name[256]    NUL-padded
    u32 size          compressed payload size
    u8  payload[size] zlib stream
```

The table ends when the next name is empty, when fewer than 260 bytes
remain, or when the declared payload runs past the end of the file. In the
reference archive the last entry (`filelist.txt`) is truncated by 12 bytes;
the reader reports it and skips extraction.

## Evidence

The first entry starts at `0x0C` (`script.txt`), its size field is at
`0x10C`, and its zlib stream at `0x110`. The next name (`start.jpg`)
starts at `0x7F9`, exactly `0x110 + 0x6E9`. Every following entry follows
the same rule.

## What it contains

- `script.txt`: the installer manifest. It maps each source file to an
  install path and marks each entry `copy` or `decompress`.
- `game.erg`: the game's text configuration file (see `erg.md`).
- `list.txt`: the asset path list (see `dat.md`).
- Installer UI assets and `binkw32.dll`.

## Open questions

- The last entry's size field is 12 bytes larger than the remaining data.
  It is either a terminator convention or a truncated file; it does not
  affect the other entries.
- The installer's decompressor for the numbered `data/*.dat` chunks is
  code in `install2.dat`, not part of SFXI.
