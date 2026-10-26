#!/usr/bin/env python3
"""Extract the entity table from a Blood Omen 2 `region` entry.

    python3 tools/bo2fmt/region.py list <level.big>
    python3 tools/bo2fmt/region.py entities <level.big> --out entities.json

The `region` entry is itself a `goefile` container. Its payload starts
with two u32 values and a sequence of named sections. Each section is an
8-byte name, a u32 total size (including the 12-byte header) and the
payload. `entities` sections contain `entity` chunks with a name, a model
name, a position and a property list.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

import big


class RegionError(ValueError):
    pass


def _u32(data: bytes, offset: int) -> int:
    if offset + 4 > len(data):
        raise RegionError("truncated region u32")
    return struct.unpack_from("<I", data, offset)[0]


def _cstr(data: bytes, offset: int, limit: int) -> tuple[str, int]:
    end = data.find(b"\0", offset, limit)
    if end < 0:
        raise RegionError("unterminated region string")
    return data[offset:end].decode("latin1"), end + 1


def _align4(value: int) -> int:
    return (value + 3) & ~3


def parse_entity(data: bytes) -> dict:
    if len(data) < 12:
        raise RegionError("entity record is truncated")
    flags = _u32(data, 0)
    name, offset = _cstr(data, 4, len(data))
    model, offset = _cstr(data, offset, len(data))
    offset = _align4(offset)
    if offset + 12 > len(data):
        raise RegionError("entity position is truncated")
    position = list(struct.unpack_from("<fff", data, offset))
    offset += 12
    properties = []
    while offset < len(data):
        if data[offset] == 0:
            offset += 1
            continue
        key, offset = _cstr(data, offset, len(data))
        offset = _align4(offset)
        if offset + 4 > len(data):
            raise RegionError(f"property {key!r} is truncated")
        kind = _u32(data, offset)
        offset += 4
        if kind == 8:
            value, offset = _cstr(data, offset, len(data))
            offset = _align4(offset)
        elif offset + 4 <= len(data):
            value = _u32(data, offset)
            offset += 4
        else:
            raise RegionError(f"property {key!r} has no value")
        properties.append({"key": key, "type": kind, "value": value})
    return {
        "flags": flags,
        "name": name,
        "model": model,
        "position": position,
        "properties": properties,
    }


def region_payload(data: bytes) -> bytes:
    if not data.startswith(b"goefile\0"):
        return data
    if len(data) < 0x24:
        raise RegionError("region container is shorter than its header")
    entries_start = 0x10 + _u32(data, 0x18)
    offset = entries_start
    while offset + 12 <= len(data):
        name = data[offset : offset + 8].rstrip(b"\0").decode("latin1")
        size = _u32(data, offset + 8)
        if size < 12 or offset + size > len(data):
            raise RegionError(f"bad nested region entry size for {name!r}")
        if name == "region":
            return data[offset + 12 : offset + size]
        offset += size
    raise RegionError("nested container has no region entry")


def parse_sections(data: bytes) -> list[dict]:
    if len(data) < 8:
        raise RegionError("region payload is shorter than its header")
    version = _u32(data, 0)
    extra = _u32(data, 4)
    sections = []
    offset = 8
    while offset + 12 <= len(data):
        raw_name = data[offset : offset + 8]
        if raw_name == b"\0" * 8:
            break
        name = raw_name.rstrip(b"\0").decode("latin1")
        size = _u32(data, offset + 8)
        if size < 12 or offset + size > len(data):
            raise RegionError(f"bad section size for {name!r}")
        payload = data[offset + 12 : offset + size]
        sections.append({"name": name, "payload": payload})
        offset += size
    return [{"version": version, "extra": extra, "sections": sections}][0]["sections"]


def parse_entity_section(payload: bytes) -> list[dict]:
    if len(payload) < 8:
        raise RegionError("entities section is truncated")
    package = _u32(payload, 0)
    marker = _u32(payload, 4)
    entities = []
    offset = 8
    while offset + 12 <= len(payload):
        raw_name = payload[offset : offset + 8]
        if raw_name == b"\0" * 8:
            break
        name = raw_name.rstrip(b"\0").decode("latin1")
        size = _u32(payload, offset + 8)
        if name != "entity" or size < 12 or offset + size > len(payload):
            break
        entity = parse_entity(payload[offset + 12 : offset + size])
        entity["package"] = package
        entity["marker"] = marker
        entities.append(entity)
        offset += size
    return entities


def collect_entities(path: Path) -> list[dict]:
    _, entries = big.load(path)
    regions = [entry for entry in entries if entry.type == "region"]
    result = []
    for region in regions:
        for section in parse_sections(region_payload(region.data)):
            if section["name"] != "entities":
                continue
            for entity in parse_entity_section(section["payload"]):
                entity["region"] = region.name
                result.append(entity)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Blood Omen 2 region entity table")
    sub = parser.add_subparsers(dest="command", required=True)
    p_list = sub.add_parser("list", help="list region sections")
    p_list.add_argument("container")
    p_entities = sub.add_parser("entities", help="extract entity placements")
    p_entities.add_argument("container")
    p_entities.add_argument("--out", required=True)
    args = parser.parse_args()
    try:
        if args.command == "list":
            _, entries = big.load(Path(args.container))
            for region in [entry for entry in entries if entry.type == "region"]:
                sections = parse_sections(region_payload(region.data))
                print(f"{region.name}: {len(sections)} sections")
                for section in sections:
                    print(f"  {section['name']:10s} {len(section['payload'])} bytes")
            return 0
        entities = collect_entities(Path(args.container))
        Path(args.out).write_text(json.dumps(entities, indent=2), encoding="utf-8")
        print(f"entities: {len(entities)}")
        return 0
    except (RegionError, big.BigError, OSError) as error:
        print(f"region.py: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
