#!/usr/bin/env python3
"""Reader for the SFXI installer container.

The PC installer stores its support files in an SFXI resource. Layout
(little-endian):

    magic[4]        "SFXI"
    u32             total size
    entries:
        char name[256]      NUL-padded
        u32 compressed_size
        u8  payload[compressed_size]   zlib stream

The table ends when the next name is empty, when the remaining bytes are
too short, or when the declared payload runs past the end of the file
(the last entry of the reference archive is truncated).

This tool only reads the installer container. It does not read or write
game assets.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"SFXI"
HEADER_SIZE = 12
NAME_SIZE = 256


class Entry:
    __slots__ = ("name", "compressed_size", "compressed", "payload", "truncated")

    def __init__(self, name, compressed_size, compressed, payload, truncated):
        self.name = name
        self.compressed_size = compressed_size
        self.compressed = compressed
        self.payload = payload
        self.truncated = truncated


def read_entries(data: bytes) -> list[Entry]:
    if len(data) < HEADER_SIZE or data[:4] != MAGIC:
        raise ValueError("not an SFXI container")
    entries: list[Entry] = []
    offset = HEADER_SIZE
    while offset + NAME_SIZE + 4 <= len(data):
        raw_name = data[offset : offset + NAME_SIZE]
        name = raw_name.split(b"\0", 1)[0]
        if not name or any(byte < 32 or byte > 126 for byte in name):
            break
        size = struct.unpack_from("<I", data, offset + NAME_SIZE)[0]
        payload_start = offset + NAME_SIZE + 4
        payload_end = payload_start + size
        truncated = payload_end > len(data)
        blob = data[payload_start : min(payload_end, len(data))]
        compressed = False
        payload = blob
        if not truncated:
            try:
                payload = zlib.decompress(blob)
                compressed = True
            except zlib.error:
                payload = blob
        entries.append(
            Entry(
                name.decode("latin1"),
                size,
                compressed,
                payload,
                truncated,
            )
        )
        if truncated:
            break
        offset = payload_end
    return entries


def load(path: Path) -> list[Entry]:
    return read_entries(path.read_bytes())


def cmd_list(args) -> int:
    entries = load(Path(args.container))
    for entry in entries:
        state = "truncated" if entry.truncated else ("zlib" if entry.compressed else "raw")
        print(f"{entry.compressed_size:10d} {len(entry.payload):10d} {state:10s} {entry.name}")
    print(f"{len(entries)} entries")
    return 0


def cmd_extract(args) -> int:
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    entries = load(Path(args.container))
    for entry in entries:
        if entry.truncated:
            print(f"skip truncated entry: {entry.name}", file=sys.stderr)
            continue
        target = out / entry.name
        if target.parent != out:
            print(f"skip unsafe name: {entry.name}", file=sys.stderr)
            continue
        target.write_bytes(entry.payload)
    print(f"extracted {len(entries)} entries to {out}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Read an SFXI installer container")
    sub = parser.add_subparsers(dest="command", required=True)

    p_list = sub.add_parser("list", help="list entries")
    p_list.add_argument("container")
    p_list.set_defaults(func=cmd_list)

    p_extract = sub.add_parser("extract", help="extract entries")
    p_extract.add_argument("container")
    p_extract.add_argument("--out", required=True)
    p_extract.set_defaults(func=cmd_extract)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
