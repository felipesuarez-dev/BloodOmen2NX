#!/usr/bin/env python3
"""Reader for the `strtable` entries of the `.big` containers.

    python3 tools/bo2fmt/strtable.py dump <container.big> --entry ui/UI [--locale spanish] [--raw]

Layout (docs/FORMATS/strtable.md; StringTableManager, get-by-name FUN_006a90c0):

    char[8] "default\\0"
    u32     payload size
    u32     1
    u32     2
    records of u16:  7, key_ref, value_ref, 0xffff

`key_ref` and `value_ref` index the entry's symbol list and each symbol is an
index into the container name table: the key and the text are both names.
A record that is not `[7, key, value]` is kept raw and skipped by lookups.

The stored text is lower case; upper-case letters stand for accented glyphs
of the game font. display_text() maps them to Unicode. That mapping is
INFERRED from word evidence (the glyph art was not decoded).
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import big

HEADER_SIZE = 20
RECORD_TAG = 7
TERMINATOR = 0xFFFF

# Stored letter -> displayed character. Inferred; only Spanish has a table.
GLYPHS = {
    "spanish": {
        "T": "á",  # a-acute
        "C": "é",  # e-acute
        "U": "í",  # i-acute
        "V": "ó",  # o-acute
        "W": "ú",  # u-acute
        "X": "ñ",  # n-tilde
        "Y": "¿",  # inverted question mark
    },
}


class StrTableError(ValueError):
    pass


def parse_strtable(names: list[str], entry: big.Entry):
    """Return (records, irregular).

    records: list of (key, value) names in file order.
    irregular: list of raw u16 tuples (terminator excluded) that are not
    `[7, key, value]`.
    """
    data = entry.data
    if len(data) < HEADER_SIZE or data[:8] != b"default\0":
        raise StrTableError("not a strtable payload")
    if struct.unpack_from("<I", data, 8)[0] != len(data):
        raise StrTableError("strtable size does not match the payload")
    if (len(data) - HEADER_SIZE) & 1:
        raise StrTableError("strtable payload is truncated")

    def name_of(ref: int) -> str:
        if ref >= len(entry.symbols):
            raise StrTableError("strtable reference is outside the symbol list")
        symbol = entry.symbols[ref]
        if symbol >= len(names):
            raise StrTableError("strtable symbol is outside the name table")
        return names[symbol]

    words = struct.unpack_from("<%dH" % ((len(data) - HEADER_SIZE) // 2), data, HEADER_SIZE)
    records = []
    irregular = []
    current: list[int] = []
    for word in words:
        if word != TERMINATOR:
            current.append(word)
            continue
        if len(current) == 3 and current[0] == RECORD_TAG:
            records.append((name_of(current[1]), name_of(current[2])))
        else:
            irregular.append(tuple(current))
        current = []
    if current:
        raise StrTableError("strtable record has no terminator")
    return records, irregular


def display_text(stored: str, locale: str = big.DEFAULT_LOCALE) -> str:
    """Map the stored accent placeholders to Unicode for `locale` (inferred)."""
    table = GLYPHS.get(locale.lower())
    if table is None:
        return stored
    return "".join(table.get(character, character) for character in stored)


def cmd_dump(args) -> int:
    names, entries = big.load(Path(args.container))
    entry = big.find_localized(names, entries, "strtable", args.entry, args.locale)
    if entry is None:
        print(f"no strtable {args.entry!r} for locale {args.locale!r}", file=sys.stderr)
        return 1
    records, irregular = parse_strtable(names, entry)
    sys.stdout.reconfigure(encoding="utf-8")
    language = big.language_name(names, entry)
    print(
        f"# {args.entry} language={entry.language} ({language}) "
        f"records={len(records)} irregular={len(irregular)}"
    )
    for key, value in records:
        print(f"{key}\t{value if args.raw else display_text(value, args.locale)}")
    for words in irregular:
        print("# irregular: " + " ".join(str(word) for word in words))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Read a Blood Omen 2 strtable entry")
    sub = parser.add_subparsers(dest="command", required=True)
    p_dump = sub.add_parser("dump", help="print key<TAB>text for every record")
    p_dump.add_argument("container")
    p_dump.add_argument("--entry", required=True, help="entry name, for example ui/UI")
    p_dump.add_argument("--locale", default=big.DEFAULT_LOCALE)
    p_dump.add_argument("--raw", action="store_true", help="print the stored text")
    p_dump.set_defaults(func=cmd_dump)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
