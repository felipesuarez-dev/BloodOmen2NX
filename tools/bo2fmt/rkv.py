#!/usr/bin/env python3
"""Reader for the Blood Omen 2 `pc.rkv` audio archive.

The PC archive is an Ogg Vorbis stream container. The layout is described
by independent public parsers (see docs/PRIOR_ART.md and
docs/FORMATS/rkv.md); the index is length-prefixed and the data start is
derived from the index size, not from a fixed offset.

Container:

    u32 version = 1
    u32 index_size
    u32 language_count
      string language
      u32 section_count
        string section
        u32 entry_count
        u32 0
        entry: string name; u32 1; u32 sector; u32 size; u32 0
    padding
    data_start = index_size + 8 + 0x800
    data       = data_start + sector * 2048
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

SECTOR = 2048
DATA_BIAS = 0x800


class RkvError(ValueError):
    pass


class Entry:
    __slots__ = ("name", "section", "language", "sector", "size", "offset")

    def __init__(self, name, section, language, sector, size, offset):
        self.name = name
        self.section = section
        self.language = language
        self.sector = sector
        self.size = size
        self.offset = offset


def _u32(data: bytes, offset: int) -> int:
    if offset + 4 > len(data):
        raise RkvError("truncated u32")
    return struct.unpack_from("<I", data, offset)[0]


def _string(data: bytes, offset: int) -> tuple[str, int]:
    length = _u32(data, offset)
    offset += 4
    if offset + length > len(data):
        raise RkvError("truncated string")
    return data[offset : offset + length].decode("latin1"), offset + length


def parse(data: bytes) -> list[Entry]:
    if len(data) < 12 or _u32(data, 0) != 1:
        raise RkvError("not an rkv container")
    index_size = _u32(data, 4)
    pos = 8
    language_count = _u32(data, pos)
    pos += 4
    entries: list[Entry] = []
    for _ in range(language_count):
        language, pos = _string(data, pos)
        section_count = _u32(data, pos)
        pos += 4
        for _ in range(section_count):
            section, pos = _string(data, pos)
            entry_count = _u32(data, pos)
            pos += 4
            pos += 4  # zero
            for _ in range(entry_count):
                name, pos = _string(data, pos)
                pos += 4  # 1
                sector = _u32(data, pos)
                pos += 4
                size = _u32(data, pos)
                pos += 4
                pos += 4  # zero
                offset = index_size + 8 + DATA_BIAS + sector * SECTOR
                entries.append(Entry(name, section, language, sector, size, offset))
    return entries


def load(path: Path) -> list[Entry]:
    return parse(path.read_bytes())


def cmd_list(args) -> int:
    entries = load(Path(args.container))
    print(f"entries: {len(entries)}")
    for entry in entries:
        print(f"{entry.size:10d} {entry.section:10s} {entry.name}")
    return 0


def cmd_extract(args) -> int:
    data = Path(args.container).read_bytes()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    entries = parse(data)
    written = 0
    for index, entry in enumerate(entries):
        name = Path(entry.name)
        if name.is_absolute() or ".." in name.parts:
            print(f"skip unsafe name: {entry.name}", file=sys.stderr)
            continue
        blob = data[entry.offset : entry.offset + entry.size]
        if len(blob) != entry.size:
            print(f"skip truncated entry: {entry.name}", file=sys.stderr)
            continue
        target = out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            target = target.with_name(f"{target.name}.{index}")
        target.write_bytes(blob)
        written += 1
    print(f"extracted {written} entries to {out}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Read a Blood Omen 2 pc.rkv audio archive")
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
