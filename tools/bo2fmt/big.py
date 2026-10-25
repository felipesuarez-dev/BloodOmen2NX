#!/usr/bin/env python3
"""Reader for the Blood Omen 2 `.big` container ("goefile").

Status: EXPERIMENTAL. The layout is reconstructed from public independent
descriptions (see docs/PRIOR_ART.md and docs/FORMATS/big.md) and has not
been validated against this build yet. It must not be treated as decoded
until a real file round-trips.

Container:

    0x00  char[8]  "goefile\\0"
    0x08  u32      total file length
    0x0C  u32      unknown (1)
    0x10  char[8]  "symlist\\0"
    0x18  u32      directory length, including this header
    0x1C  u32      0
    0x20  u32      name count
    0x24  ...      NUL-terminated names
          pad to even offset
    entries start at 0x10 + directory_length

Entry:

    char[8] type
    u32     entry length, including this header and the padding
    u32     0
    u16     name index
    u16     language index
    char[8] "symlist\0"
    u32     symbols size (from the symlist to the data length)
    u32     0
    u32     n
    n       u16 values
    u16     0xffff (only when n > 0)
    u32     data length
    data, padded to 4

The data length lives at `entry_start + 20 + symbols_size`. The data
offset is derived from the end of the entry, which is the robust formula
used by independent parsers: data = start + length - align4(size).
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

MAGIC = b"goefile\0"
SYMLIST = b"symlist\0"


class BigError(ValueError):
    pass


class Entry:
    __slots__ = ("type", "name", "language", "size", "offset", "data")

    def __init__(self, type_name, name, language, size, offset, data):
        self.type = type_name
        self.name = name
        self.language = language
        self.size = size
        self.offset = offset
        self.data = data


def _u32(data: bytes, offset: int) -> int:
    if offset + 4 > len(data):
        raise BigError("truncated u32")
    return struct.unpack_from("<I", data, offset)[0]


def _u16(data: bytes, offset: int) -> int:
    if offset + 2 > len(data):
        raise BigError("truncated u16")
    return struct.unpack_from("<H", data, offset)[0]


def _cstr(data: bytes, offset: int, limit: int) -> tuple[str, int]:
    end = data.find(b"\0", offset, limit)
    if end < 0:
        raise BigError("unterminated string")
    return data[offset:end].decode("latin1"), end + 1


def parse(data: bytes) -> tuple[list[str], list[Entry]]:
    if len(data) < 0x24 or data[:8] != MAGIC:
        raise BigError("not a goefile container")
    directory_length = _u32(data, 0x18)
    name_count = _u32(data, 0x20)
    entries_start = 0x10 + directory_length
    if entries_start < 0x24 or entries_start > len(data):
        raise BigError("bad directory length")

    pos = 0x24
    names: list[str] = []
    for _ in range(name_count):
        name, pos = _cstr(data, pos, entries_start)
        names.append(name)
    if pos & 1:
        pos += 1
    if pos > entries_start:
        raise BigError("name table overruns the directory")

    entries: list[Entry] = []
    pos = entries_start
    while pos < len(data):
        if len(data) - pos < 44:
            if any(data[pos:]):
                raise BigError(f"truncated entry at {pos:#x}")
            break
        type_name = data[pos : pos + 8].rstrip(b"\0").decode("latin1")
        if not type_name:
            if any(data[pos:]):
                raise BigError(f"bad entry type at {pos:#x}")
            break
        entry_length = _u32(data, pos + 8)
        entry_end = pos + entry_length
        if entry_length < 44 or entry_end > len(data):
            raise BigError(f"bad entry length at {pos:#x}")
        name_index = _u16(data, pos + 16)
        language_index = _u16(data, pos + 18)
        symbols_size = _u32(data, pos + 28)
        data_length_at = pos + 20 + symbols_size
        if data_length_at + 4 > entry_end:
            raise BigError(f"bad symbol list at {pos:#x}")
        data_length = _u32(data, data_length_at)
        padded = (data_length + 3) & ~3
        if padded > entry_length:
            raise BigError(f"bad data length at {pos:#x}")
        data_offset = entry_end - padded
        if data_offset < data_length_at + 4 or data_offset > entry_end:
            raise BigError(f"bad data range at {pos:#x}")
        entries.append(
            Entry(
                type_name,
                names[name_index] if name_index < len(names) else f"#{name_index}",
                language_index,
                data_length,
                data_offset,
                data[data_offset : data_offset + data_length],
            )
        )
        pos = entry_end
    return names, entries


def load(path: Path) -> tuple[list[str], list[Entry]]:
    return parse(path.read_bytes())


def cmd_list(args) -> int:
    names, entries = load(Path(args.container))
    print(f"names: {len(names)}  entries: {len(entries)}")
    for entry in entries:
        print(f"{entry.type:8s} {entry.size:10d} {entry.name}")
    return 0


def cmd_extract(args) -> int:
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    _, entries = load(Path(args.container))
    written = 0
    for index, entry in enumerate(entries):
        name = Path(entry.name)
        if name.is_absolute() or ".." in name.parts:
            print(f"skip unsafe name: {entry.name}", file=sys.stderr)
            continue
        target = out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            target = target.with_name(f"{target.name}.{index}")
        target.write_bytes(entry.data)
        written += 1
    print(f"extracted {written} entries to {out}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Read a Blood Omen 2 .big container")
    sub = parser.add_subparsers(dest="command", required=True)
    p_list = sub.add_parser("list")
    p_list.add_argument("container")
    p_list.set_defaults(func=cmd_list)
    p_extract = sub.add_parser("extract")
    p_extract.add_argument("container")
    p_extract.add_argument("--out", required=True)
    p_extract.set_defaults(func=cmd_extract)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
