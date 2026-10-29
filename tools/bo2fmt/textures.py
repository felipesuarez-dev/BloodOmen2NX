#!/usr/bin/env python3
"""Export Blood Omen 2 texture entries to PNG for inspection.

    python3 tools/bo2fmt/textures.py list <container.big>
    python3 tools/bo2fmt/textures.py export <container.big> --out <dir> [--match text]

Every `texture` entry is a plain DDS file (docs/FORMATS/textures.md), so the
top mip is decoded with Pillow (DXT1, DXT3, DXT5 and uncompressed RGBA).
`export` keeps the entry path, writes one PNG per texture entry and an
`index.json` with the size and pixel format of each one. A container can
hold the same texture name more than once (with different payloads); the
later copies get a `~N` suffix. Write the output under `external/`; it is
game data.
"""

from __future__ import annotations

import argparse
import io
import json
import struct
import sys
from pathlib import Path

import big


class TextureError(ValueError):
    pass


def dds_info(data: bytes) -> dict:
    if len(data) < 128 or data[:4] != b"DDS ":
        raise TextureError("payload is not a DDS file")
    height, width = struct.unpack_from("<II", data, 12)
    mips = struct.unpack_from("<I", data, 28)[0]
    fourcc = data[84:88]
    if fourcc == b"\0\0\0\0":
        bits = struct.unpack_from("<I", data, 88)[0]
        pixel_format = f"rgb{bits}"
    else:
        pixel_format = fourcc.decode("latin1")
    return {"width": width, "height": height, "mips": max(mips, 1), "format": pixel_format}


def to_png(data: bytes) -> bytes:
    try:
        from PIL import Image
    except ImportError as error:
        raise TextureError("Pillow is required to decode DDS (python3-pil)") from error
    with Image.open(io.BytesIO(data)) as image:
        out = io.BytesIO()
        image.convert("RGBA").save(out, "PNG")
        return out.getvalue()


def texture_entries(path: Path, match: str = "") -> list:
    _, entries = big.load(path)
    needle = match.lower()
    return [e for e in entries if e.type == "texture" and needle in e.name.lower()]


def safe_target(out: Path, name: str, used: set[str] | None = None) -> Path | None:
    relative = Path(name)
    if not name or relative.is_absolute() or ".." in relative.parts:
        return None
    target = out / relative.with_name(relative.name + ".png")
    if used is None:
        return target
    copy = 1
    while target.as_posix().lower() in used:
        target = out / relative.with_name(f"{relative.name}~{copy}.png")
        copy += 1
    used.add(target.as_posix().lower())
    return target


def cmd_list(args) -> int:
    for entry in texture_entries(Path(args.container), args.match):
        try:
            info = dds_info(entry.data)
            print(f"{info['format']:6s} {info['width']:5d}x{info['height']:<5d} {entry.name}")
        except TextureError as error:
            print(f"error  {entry.name}: {error}")
    return 0


def cmd_export(args) -> int:
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    index = []
    used: set[str] = set()
    failed = 0
    for entry in texture_entries(Path(args.container), args.match):
        target = safe_target(out, entry.name, used)
        if target is None:
            print(f"skip unsafe name: {entry.name!r}", file=sys.stderr)
            continue
        record = {"entry": entry.name}
        try:
            record.update(dds_info(entry.data))
            png = to_png(entry.data)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(png)
        except (TextureError, OSError, ValueError) as error:
            record["error"] = str(error)
            failed += 1
        else:
            record["png"] = target.relative_to(out).as_posix()
        index.append(record)
    (out / "index.json").write_text(json.dumps(index, indent=2) + "\n")
    print(f"exported {len(index) - failed} textures to {out} ({failed} failed)")
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Export Blood Omen 2 textures to PNG")
    sub = parser.add_subparsers(dest="command", required=True)
    p_list = sub.add_parser("list", help="list texture entries with size and format")
    p_list.add_argument("container")
    p_list.add_argument("--match", default="", help="case-insensitive entry name filter")
    p_list.set_defaults(func=cmd_list)
    p_export = sub.add_parser("export", help="write one PNG per texture entry")
    p_export.add_argument("container")
    p_export.add_argument("--out", required=True)
    p_export.add_argument("--match", default="", help="case-insensitive entry name filter")
    p_export.set_defaults(func=cmd_export)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
