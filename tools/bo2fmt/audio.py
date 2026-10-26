#!/usr/bin/env python3
"""Extract audio metadata and event definitions from Blood Omen 2 data.

    python3 tools/bo2fmt/audio.py sounds <container.big> --out sounds.json
    python3 tools/bo2fmt/audio.py strmfiles <container.big> --out <dir>

The Ogg streams themselves live in `pc.rkv`; use
`python3 tools/bo2fmt/rkv.py extract <pc.rkv> --out <dir>`.

A `sound` payload starts with a NUL-terminated record name, a u32 payload
size, two header u32 values and a sequence of 8-byte property records.
The field meanings are still open; this tool extracts the names and the
raw records so they can be classified later.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

import big


class AudioError(ValueError):
    pass


def parse_sound(data: bytes) -> dict:
    end = data.find(b"\0")
    if end < 0 or end > 255:
        raise AudioError("sound payload has no record name")
    name = data[:end].decode("latin1")
    offset = end + 1
    if offset + 12 > len(data):
        raise AudioError("sound payload is shorter than its header")
    declared = struct.unpack_from("<I", data, offset)[0]
    if declared != len(data):
        raise AudioError(f"sound {name!r} declares {declared} bytes, has {len(data)}")
    version = struct.unpack_from("<I", data, offset + 4)[0]
    flags = struct.unpack_from("<I", data, offset + 8)[0]
    body = data[offset + 12 :]
    records = []
    for position in range(0, len(body) - 7, 8):
        key, field, value = struct.unpack_from("<HHI", body, position)
        records.append({"key": key, "field": field, "value": value})
    remainder = body[len(records) * 8 :]
    return {
        "name": name,
        "declared_size": declared,
        "version": version,
        "flags": flags,
        "records": records,
        "remainder": remainder.hex(),
    }


def collect_sounds(path: Path) -> list[dict]:
    names, entries = big.load(path)
    result = []
    for entry in entries:
        if entry.type != "sound":
            continue
        try:
            parsed = parse_sound(entry.data)
        except AudioError as error:
            result.append(
                {
                    "entry": entry.name,
                    "error": str(error),
                    "size": len(entry.data),
                }
            )
            continue
        parsed["entry"] = entry.name
        result.append(parsed)
    return result


def collect_strmfiles(path: Path, out: Path) -> int:
    _, entries = big.load(path)
    written = 0
    out.mkdir(parents=True, exist_ok=True)
    for index, entry in enumerate(entries):
        if entry.type != "strmfile":
            continue
        safe = "".join(
            character if character.isalnum() or character in "._-" else "_"
            for character in entry.name
        ).strip("._")
        stem = safe or "entry"
        if not stem.lower().endswith(".etp"):
            stem += ".etp"
        target = out / f"{index:04d}_{stem}"
        target.write_bytes(entry.data)
        written += 1
    return written


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract Blood Omen 2 audio metadata")
    sub = parser.add_subparsers(dest="command", required=True)
    p_sounds = sub.add_parser("sounds", help="extract sound names and records")
    p_sounds.add_argument("container")
    p_sounds.add_argument("--out", required=True)
    p_strm = sub.add_parser("strmfiles", help="extract strmfile event scripts")
    p_strm.add_argument("container")
    p_strm.add_argument("--out", required=True)
    args = parser.parse_args()
    try:
        if args.command == "sounds":
            sounds = collect_sounds(Path(args.container))
            Path(args.out).write_text(json.dumps(sounds, indent=2), encoding="utf-8")
            bad = sum(1 for sound in sounds if "error" in sound)
            print(f"extracted {len(sounds)} sound entries ({bad} with errors)")
            return 0
        if args.command == "strmfiles":
            written = collect_strmfiles(Path(args.container), Path(args.out))
            print(f"extracted {written} strmfile entries")
            return 0
    except (AudioError, big.BigError, OSError) as error:
        print(f"audio.py: {error}", file=sys.stderr)
        return 1
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
