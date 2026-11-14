#!/usr/bin/env python3
"""Reader for the `texfont` entries of the `.big` containers.

    python3 tools/bo2fmt/font.py dump <container.big> [--entry main/main]

Layout (docs/FORMATS/textures.md, "Font"; loader FUN_005df900):

    char[8] "texfont\\0"
    u32     payload size
    u32     0
    u16     texture (a symbol of the entry), u16 0xffff
    f32     four header values, in file order
    u8      upper-case-only flag, u8 0xff
    u16     glyph count
    glyphs, 20 bytes each: u8 code, three 0xff, f32 x, y, width, height

The y of a glyph is measured from the bottom of the texture; in the rows of
the DDS it covers `texture height - y - height` to `texture height - y`. The
shape is in the alpha channel. With the upper-case-only flag set, every code
from `A` to `Z` gets a second code 0x20 above it, unless that code exists.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import big

HEADER_SIZE = 40
GLYPH_SIZE = 20


class FontError(ValueError):
    pass


def parse_texfont(names: list[str], entry: big.Entry) -> dict:
    """Return the font of a `texfont` entry as texture, header, flag and glyphs."""
    data = entry.data
    if len(data) < HEADER_SIZE or data[:8] != b"texfont\0":
        raise FontError("not a texfont payload")
    if struct.unpack_from("<I", data, 8)[0] != len(data):
        raise FontError("texfont size does not match the payload")
    texture_symbol = struct.unpack_from("<H", data, 16)[0]
    if texture_symbol >= len(entry.symbols) or entry.symbols[texture_symbol] >= len(names):
        raise FontError("texfont texture is outside the name table")
    texture = names[entry.symbols[texture_symbol]]
    header = struct.unpack_from("<4f", data, 20)
    upper_case_only = data[36] != 0
    count = struct.unpack_from("<H", data, 38)[0]
    if HEADER_SIZE + count * GLYPH_SIZE > len(data):
        raise FontError("texfont glyphs are truncated")
    glyphs = []
    for index in range(count):
        offset = HEADER_SIZE + index * GLYPH_SIZE
        code = data[offset]
        x, y, width, height = struct.unpack_from("<4f", data, offset + 4)
        glyphs.append((code, x, y, width, height))
    if upper_case_only:
        present = {glyph[0] for glyph in glyphs}
        for code in range(ord("A"), ord("Z") + 1):
            if code in present and code + 0x20 not in present:
                upper = next(glyph for glyph in glyphs if glyph[0] == code)
                glyphs.append((code + 0x20, *upper[1:]))
    return {
        "texture": texture,
        "header": header,
        "upper_case_only": upper_case_only,
        "glyphs": glyphs,
    }


def cmd_dump(args) -> int:
    names, entries = big.load(Path(args.container))
    entry = next(
        (candidate for candidate in entries
         if candidate.type == "texfont" and candidate.name == args.entry),
        None,
    )
    if entry is None:
        print(f"no texfont {args.entry!r}", file=sys.stderr)
        return 1
    font = parse_texfont(names, entry)
    header = " ".join(f"{value:g}" for value in font["header"])
    print(
        f"# {entry.name} texture={font['texture']} header={header} "
        f"upper_case_only={int(font['upper_case_only'])} glyphs={len(font['glyphs'])}"
    )
    for code, x, y, width, height in font["glyphs"]:
        print(f"{code:#04x}\t{x:g}\t{y:g}\t{width:g}\t{height:g}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Read a Blood Omen 2 texfont entry")
    sub = parser.add_subparsers(dest="command", required=True)
    p_dump = sub.add_parser("dump", help="print the header and one line per glyph")
    p_dump.add_argument("container")
    p_dump.add_argument("--entry", default="main/main", help="entry name (default main/main)")
    p_dump.set_defaults(func=cmd_dump)
    args = parser.parse_args()
    try:
        return args.func(args)
    except (OSError, big.BigError, FontError) as error:
        print(f"font: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
