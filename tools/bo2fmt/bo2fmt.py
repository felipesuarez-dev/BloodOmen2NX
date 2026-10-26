#!/usr/bin/env python3
"""Umbrella CLI for the Blood Omen 2 containers.

    python3 tools/bo2fmt/bo2fmt.py list <container>
    python3 tools/bo2fmt/bo2fmt.py unpack <container> --out <dir>
    python3 tools/bo2fmt/bo2fmt.py repack <dir> --out <container>
    python3 tools/bo2fmt/bo2fmt.py info <file>

`unpack` writes a manifest with the entry metadata (type, name, language,
symbols and padding) so `repack` can reconstruct the container. The BIG
writer is byte-identical for a container that was unpacked and left
unchanged.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import big
import rkv

BIG_MAGIC = b"goefile\0"
MANIFEST = "manifest.json"
SAFE = re.compile(r"[^A-Za-z0-9._-]+")


def detect(data: bytes) -> str:
    if data.startswith(BIG_MAGIC):
        return "big"
    if len(data) >= 8 and int.from_bytes(data[:4], "little") == 1:
        return "rkv"
    return "unknown"


def safe_name(name: str) -> str:
    cleaned = SAFE.sub("_", name).strip("._")
    return cleaned[:80] or "entry"


def data_path(out: Path, index: int, name: str) -> tuple[Path, str]:
    relative = Path("data") / f"{index:04d}_{safe_name(name)}"
    return out / relative, relative.as_posix()


def unpack_big(container: Path, out: Path) -> int:
    data = container.read_bytes()
    names, entries = big.parse(data)
    out.mkdir(parents=True, exist_ok=True)
    manifest = {
        "format": "bo2.big",
        "version": 1,
        "name_padding": big.name_padding(data),
        "names": names,
        "entries": [],
    }
    for index, entry in enumerate(entries):
        target, relative = data_path(out, index, entry.name)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(entry.data)
        manifest["entries"].append(
            {
                "type": entry.type,
                "name": entry.name,
                "name_index": entry.name_index,
                "language": entry.language,
                "symbols": list(entry.symbols),
                "symbols_padding": entry.symbols_padding.hex(),
                "extra": entry.extra.hex(),
                "trailer": entry.trailer.hex(),
                "size": entry.size,
                "file": relative,
            }
        )
    (out / MANIFEST).write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"unpacked {len(entries)} big entries to {out}")
    return 0


def unpack_rkv(container: Path, out: Path) -> int:
    data = container.read_bytes()
    entries = rkv.parse(data)
    out.mkdir(parents=True, exist_ok=True)
    manifest = {"format": "bo2.rkv", "version": 1, "entries": []}
    written = 0
    for index, entry in enumerate(entries):
        if entry.truncated:
            print(
                f"skip truncated entry: {entry.name} "
                f"({entry.stored_size} of {entry.size} bytes)",
                file=sys.stderr,
            )
            continue
        target, relative = data_path(out, index, entry.name)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data[entry.offset : entry.offset + entry.size])
        manifest["entries"].append(
            {
                "name": entry.name,
                "language": entry.language,
                "section": entry.section,
                "sector": entry.sector,
                "size": entry.size,
                "file": relative,
            }
        )
        written += 1
    (out / MANIFEST).write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"unpacked {written} rkv entries to {out}")
    return 0


def repack_big(source: Path, out: Path) -> int:
    manifest_path = source / MANIFEST
    if not manifest_path.is_file():
        raise ValueError(f"missing {MANIFEST} in {source}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != "bo2.big":
        raise ValueError(f"{source} is not an unpacked BIG container")
    entries = []
    for record in manifest["entries"]:
        path = source / record["file"]
        payload = path.read_bytes()
        if len(payload) != record["size"]:
            print(
                f"warning: {record['file']} is {len(payload)} bytes, "
                f"manifest says {record['size']}",
                file=sys.stderr,
            )
        entries.append(
            big.Entry(
                record["type"],
                record["name"],
                record["language"],
                len(payload),
                0,
                payload,
                record.get("symbols", ()),
                record.get("name_index", -1),
                bytes.fromhex(record.get("symbols_padding", "")),
                bytes.fromhex(record.get("extra", "")),
                bytes.fromhex(record.get("trailer", "")),
            )
        )
    blob = big.build(manifest["names"], entries, int(manifest.get("name_padding", 0)))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    print(f"repacked {len(entries)} entries to {out}")
    return 0


def cmd_list(args) -> int:
    data = Path(args.container).read_bytes()
    kind = detect(data)
    if kind == "big":
        names, entries = big.parse(data)
        print(f"big: names {len(names)} entries {len(entries)}")
        for entry in entries:
            print(f"{entry.type:8s} {entry.size:10d} {entry.name}")
        return 0
    if kind == "rkv":
        entries = rkv.parse(data)
        print(f"rkv: entries {len(entries)}")
        for entry in entries:
            print(f"{entry.section:10s} {entry.size:10d} {entry.name}")
        return 0
    raise ValueError("unknown container format")


def cmd_unpack(args) -> int:
    container = Path(args.container)
    data = container.read_bytes()
    kind = detect(data)
    if kind == "big":
        return unpack_big(container, Path(args.out))
    if kind == "rkv":
        return unpack_rkv(container, Path(args.out))
    raise ValueError("unknown container format")


def cmd_repack(args) -> int:
    return repack_big(Path(args.source), Path(args.out))


def cmd_info(args) -> int:
    path = Path(args.file)
    data = path.read_bytes()
    kind = detect(data)
    if kind == "big":
        names, entries = big.parse(data)
        types: dict[str, int] = {}
        for entry in entries:
            types[entry.type] = types.get(entry.type, 0) + 1
        print(f"big {path} names {len(names)} entries {len(entries)}")
        for name, count in sorted(types.items()):
            print(f"  {name:10s} {count}")
        return 0
    if kind == "rkv":
        entries = rkv.parse(data)
        sections: dict[str, int] = {}
        for entry in entries:
            sections[entry.section] = sections.get(entry.section, 0) + 1
        print(f"rkv {path} entries {len(entries)}")
        for name, count in sorted(sections.items()):
            print(f"  {name:10s} {count}")
        return 0
    raise ValueError("unknown container format")


def main() -> int:
    parser = argparse.ArgumentParser(description="Blood Omen 2 container tool")
    sub = parser.add_subparsers(dest="command", required=True)
    p_list = sub.add_parser("list", help="list a BIG or RKV container")
    p_list.add_argument("container")
    p_list.set_defaults(func=cmd_list)
    p_unpack = sub.add_parser("unpack", help="extract a container with its manifest")
    p_unpack.add_argument("container")
    p_unpack.add_argument("--out", required=True)
    p_unpack.set_defaults(func=cmd_unpack)
    p_repack = sub.add_parser("repack", help="rebuild a BIG container from an unpack")
    p_repack.add_argument("source")
    p_repack.add_argument("--out", required=True)
    p_repack.set_defaults(func=cmd_repack)
    p_info = sub.add_parser("info", help="show the container type and entry counts")
    p_info.add_argument("file")
    p_info.set_defaults(func=cmd_info)
    args = parser.parse_args()
    try:
        return args.func(args)
    except (big.BigError, rkv.RkvError, ValueError, OSError) as error:
        print(f"bo2fmt: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
