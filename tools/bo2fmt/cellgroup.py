#!/usr/bin/env python3
"""Reader for the models, attributes, textures and markers of a `cellgrup`.

    python3 tools/bo2fmt/cellgroup.py models <container.big> --entry ui/hud
    python3 tools/bo2fmt/cellgroup.py attributes <container.big> --entry ui/hud

Layout (docs/FORMATS/models.md, "Cell group", "rmd subchunks", "mdlattr",
"texture" and "cellmark"):

    0x00  u8[8]  header
    0x08  char[8] "cellmdls"
    0x10  u32   section size
    0x14  u32   0
    0x18  u32   model count
    0x1c  ...   model blocks

A model block is a version (105), a NUL-terminated name, the block size and a
chunk area. A chunk is an 8-byte name, a u32 total size and a u32 zero; its
data is the rest. The `rmd` chunk is a nested container with the model
geometry; `mdlattr` and `collmodc` are siblings of it.

The chunks after `cellmdls` are the cell group tail: `cellmark` (markers,
56-byte version-4 records), `texture` (one variable-length record per
material) and `mtlctrl` (three u32 per material).
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import big


class CellGroupError(ValueError):
    pass


MODEL_VERSION = 105
MARKER_VERSION = 4
MARKER_SIZE = 56


class Reader:
    def __init__(self, data: bytes, start: int = 0, end: int | None = None):
        self.data = data
        self.pos = start
        self.end = len(data) if end is None else end
        if start > self.end or self.end > len(data):
            raise CellGroupError("reader is outside the data")

    def _need(self, count: int) -> None:
        if self.pos + count > self.end:
            raise CellGroupError("truncated value")

    def u8(self) -> int:
        self._need(1)
        value = self.data[self.pos]
        self.pos += 1
        return value

    def u16(self) -> int:
        self.pos = (self.pos + 1) & ~1
        self._need(2)
        value = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2
        return value

    def u32(self) -> int:
        self.pos = (self.pos + 3) & ~3
        self._need(4)
        value = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return value

    def f32(self) -> float:
        self.pos = (self.pos + 3) & ~3
        self._need(4)
        value = struct.unpack_from("<f", self.data, self.pos)[0]
        self.pos += 4
        return value

    def byte(self) -> int:
        self._need(1)
        value = self.data[self.pos]
        self.pos += 1
        return value

    def rest(self) -> bytes:
        value = self.data[self.pos:self.end]
        self.pos = self.end
        return value


def symbol_name(symbols: list[str], index: int) -> str:
    if index >= len(symbols):
        raise CellGroupError("symbol is outside the entry symbol list")
    return symbols[index]


def read_chunks(data: bytes, start: int, end: int) -> list[dict]:
    chunks = []
    position = start
    while position + 16 <= end:
        name = data[position:position + 8].split(b"\0", 1)[0].decode("latin1")
        total = struct.unpack_from("<I", data, position + 8)[0]
        if total < 16 or position + total > end:
            raise CellGroupError("bad chunk size")
        chunks.append({"name": name, "data": data[position + 16:position + total]})
        position += total
    if position != end:
        raise CellGroupError("chunk area has a trailer")
    return chunks


def model_blocks(data: bytes) -> list[dict]:
    """The model blocks of a cell group, with the chunks of each one by name."""
    if len(data) < 28 or data[8:16].rstrip(b"\0") != b"cellmdls":
        return []
    count = struct.unpack_from("<I", data, 24)[0]
    blocks = []
    position = 28
    for index in range(count):
        if position + 20 > len(data):
            raise CellGroupError("truncated model block")
        name_end = data.find(b"\0", position + 4)
        if name_end < 0:
            raise CellGroupError("unterminated model name")
        name = data[position + 4:name_end].decode("latin1")
        size = struct.unpack_from("<I", data, name_end + 1)[0]
        chunks_start = name_end + 9
        end = position + 4 + size
        if size < 16 or chunks_start > end or end > len(data):
            raise CellGroupError("bad model block size")
        chunks = {chunk["name"]: chunk["data"]
                  for chunk in read_chunks(data, chunks_start, end)}
        blocks.append({"index": index, "name": name, "chunks": chunks})
        position = end
    return blocks


def tail_chunks(data: bytes) -> dict[str, bytes]:
    """The chunks of a cell group after `cellmdls`, by name."""
    chunks = {}
    position = 8
    while position + 16 <= len(data):
        name = data[position:position + 8].split(b"\0", 1)[0].decode("latin1")
        total = struct.unpack_from("<I", data, position + 8)[0]
        if total < 16 or position + total > len(data):
            raise CellGroupError("bad chunk size")
        if name != "cellmdls":
            chunks[name] = data[position + 16:position + total]
        position += total
    if position != len(data):
        raise CellGroupError("chunk area has a trailer")
    return chunks


def attributes(table: bytes, symbols: list[str]) -> list[dict]:
    """The attributes of one `mdlattr` table, in file order."""
    if len(table) < 20:
        raise CellGroupError("attribute table is shorter than its header")
    size = struct.unpack_from("<I", table, 8)[0]
    if size != len(table):
        raise CellGroupError("attribute table size does not match the table")
    reader = Reader(table, 20, len(table))
    records = []
    while reader.pos < len(table):
        kind = reader.u8()
        array = reader.u8()
        name = symbol_name(symbols, reader.u16())
        if kind not in (0, 3, 7):
            raise CellGroupError("unknown attribute type")
        if array > 1:
            raise CellGroupError("bad attribute array flag")
        if array == 0:
            if kind == 0:
                value = reader.f32()
            elif kind == 3:
                value = reader.byte()
                reader.pos += 3
            else:
                value = symbol_name(symbols, reader.u16())
                reader.pos += 2
        else:
            count = reader.u32()
            reader.pos = (reader.pos + 3) & ~3
            if kind == 0:
                reader._need(count * 4)
                value = list(struct.unpack_from(f"<{count}f", reader.data, reader.pos))
                reader.pos += count * 4
            elif kind == 3:
                reader._need(count)
                value = list(reader.data[reader.pos:reader.pos + count])
                reader.pos += count
            else:
                reader._need(count * 2)
                value = [symbol_name(symbols, reader.u16()) for _ in range(count)]
            reader.pos = (reader.pos + 3) & ~3
        records.append({"name": name, "type": kind, "array": bool(array), "value": value})
    return records


def material_textures(chunk: bytes, symbols: list[str]) -> dict[int, list[str]]:
    """The texture names of each material of one `texture` chunk."""
    if len(chunk) < 8:
        raise CellGroupError("texture chunk is truncated")
    start, count = struct.unpack_from("<II", chunk, 0)
    reader = Reader(chunk, 8, len(chunk))
    materials = {}
    for index in range(count):
        textures = reader.u32()
        if reader.pos + textures * 2 > reader.end:
            raise CellGroupError("texture record is truncated")
        names = [symbol_name(symbols, reader.u16()) for _ in range(textures)]
        if textures & 1:
            reader.u16()
        materials[start + index] = names
    if reader.pos != len(chunk):
        raise CellGroupError("texture chunk has a trailer")
    return materials


def material_controls(chunk: bytes) -> list[tuple[int, int, int]]:
    """The three u32 of each record of one `mtlctrl` chunk."""
    if len(chunk) < 4:
        raise CellGroupError("mtlctrl chunk is truncated")
    count = struct.unpack_from("<I", chunk, 0)[0]
    if 4 + count * 12 != len(chunk):
        raise CellGroupError("mtlctrl chunk has a trailer")
    return [struct.unpack_from("<III", chunk, 4 + index * 12) for index in range(count)]


def markers(data: bytes, symbols: list[str]) -> list[dict]:
    """The `cellmark` markers of a cell group, in file order."""
    chunk = tail_chunks(data).get("cellmark")
    if chunk is None:
        return []
    if len(chunk) < 4:
        raise CellGroupError("marker chunk is truncated")
    count = struct.unpack_from("<I", chunk, 0)[0]
    if 4 + count * MARKER_SIZE != len(chunk):
        raise CellGroupError("marker chunk has a trailer")
    records = []
    for index in range(count):
        position = 4 + index * MARKER_SIZE
        version, model, clip, name_ref, class_ref = struct.unpack_from(
            "<IiiII", chunk, position)
        if version != MARKER_VERSION:
            raise CellGroupError("unsupported marker version")
        coordinates = struct.unpack_from("<3f", chunk, position + 20)
        normal = struct.unpack_from("<3f", chunk, position + 32)
        flag, value, number = struct.unpack_from("<IIf", chunk, position + 44)
        records.append({
            "version": version,
            "model": model if model >= 0 else None,
            "clip": clip if clip >= 0 else None,
            "name": symbol_name(symbols, name_ref),
            "class": symbol_name(symbols, class_ref),
            "position": list(coordinates),
            "normal": list(normal),
            "flag": flag,
            "value": value,
            "number": number,
        })
    return records


def dds_format(data: bytes) -> str:
    """The FourCC of a DDS payload, or RGBA when it is uncompressed."""
    if len(data) < 88 or data[:4] != b"DDS ":
        raise CellGroupError("not a DDS payload")
    fourcc = data[84:88]
    return "RGBA" if fourcc == b"\0\0\0\0" else fourcc.decode("latin1")


def find_cell_group(entries, name: str):
    for entry in entries:
        if entry.type == "cellgrup" and entry.name == name:
            return entry
    return None


def load_cell_group(path: str | Path, name: str):
    names, entries = big.load(Path(path))
    entry = find_cell_group(entries, name)
    if entry is None:
        raise CellGroupError(f"no cellgrup {name!r}")
    symbols = []
    for index in entry.symbols:
        if index >= len(names):
            raise CellGroupError("entry symbol is outside the name table")
        symbols.append(names[index])
    return entry, symbols


def rmd_chunks(block: dict) -> dict[str, bytes]:
    """The subchunks of the `rmd` chunk of a model block, by name."""
    rmd = block["chunks"].get("rmd")
    if rmd is None:
        return {}
    if len(rmd) < 20:
        raise CellGroupError("rmd chunk is truncated")
    total = struct.unpack_from("<I", rmd, 12)[0]
    if 4 + total != len(rmd):
        raise CellGroupError("rmd record size does not match the chunk")
    return {chunk["name"]: chunk["data"] for chunk in read_chunks(rmd, 20, len(rmd))}


def model_priority(block: dict, symbols: list[str]) -> float:
    table = block["chunks"].get("mdlattr")
    if table is None:
        table = rmd_chunks(block).get("mdlattr")
    if table is None:
        return 0.0
    for record in attributes(table, symbols):
        if record["name"] == "RenderPriority" and record["type"] == 0 and not record["array"]:
            return record["value"]
    return 0.0


def cmd_models(args) -> int:
    entry, symbols = load_cell_group(args.container, args.entry)
    names, entries = big.load(Path(args.container))
    textures = {candidate.name: candidate.data for candidate in entries
                if candidate.type == "texture"}
    blocks = model_blocks(entry.data)
    by_model = {marker["model"]: marker for marker in markers(entry.data, symbols)
                if marker["model"] is not None}
    for block in blocks:
        marker = by_model.get(block["index"])
        z = marker["position"][2] if marker is not None else 0.0
        priority = model_priority(block, symbols)
        subchunks = rmd_chunks(block)
        materials = (material_textures(subchunks["texture"], symbols)
                     if "texture" in subchunks else {})
        controls = (material_controls(subchunks["mtlctrl"])
                    if "mtlctrl" in subchunks else [])
        parts = []
        indices = sorted(set(materials) | set(range(len(controls))))
        for index in indices:
            listed = materials.get(index, [])
            shown = ", ".join(f"{texture}[{dds_format(textures[texture])}]"
                              if texture in textures else texture for texture in listed) or "-"
            control = controls[index][1] if index < len(controls) else 0
            parts.append(f"{index}: {shown} (mtlctrl {control})")
        print(f"{block['index']}\t{marker['name'] if marker else '-'}\t{z:g}\t"
              f"{priority:g}\t" + "; ".join(parts))
    return 0


def cmd_attributes(args) -> int:
    entry, symbols = load_cell_group(args.container, args.entry)
    for block in model_blocks(entry.data):
        table = block["chunks"].get("mdlattr")
        if table is None:
            continue
        for record in attributes(table, symbols):
            if record["array"]:
                value = ",".join(f"{item:g}" if isinstance(item, float) else str(item)
                                 for item in record["value"])
            elif isinstance(record["value"], float):
                value = f"{record['value']:g}"
            else:
                value = str(record["value"])
            print(f"{block['index']}\t{block['name']}\t{record['name']}\t"
                  f"{record['type']}\t{int(record['array'])}\t{value}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Read a Blood Omen 2 cell group")
    sub = parser.add_subparsers(dest="command", required=True)
    p_models = sub.add_parser("models", help="print one line per model")
    p_models.add_argument("container")
    p_models.add_argument("--entry", required=True)
    p_models.set_defaults(func=cmd_models)
    p_attributes = sub.add_parser("attributes", help="print one line per model attribute")
    p_attributes.add_argument("container")
    p_attributes.add_argument("--entry", required=True)
    p_attributes.set_defaults(func=cmd_attributes)
    args = parser.parse_args()
    try:
        return args.func(args)
    except (OSError, big.BigError, CellGroupError) as error:
        print(f"cellgroup: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
