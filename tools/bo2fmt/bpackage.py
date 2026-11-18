#!/usr/bin/env python3
"""Reader for the `bpackage` entries of the `.big` containers.

    python3 tools/bo2fmt/bpackage.py list <container.big>
    python3 tools/bo2fmt/bpackage.py classes <container.big> <package>

A package holds the script classes of the game. Every value is aligned to its
size counted from the start of the payload; a name is an index into the symbol
list of the entry; a chunk is a `u32` size and that many bytes, and its content
can end up to three bytes before the end of the chunk.

Layout (docs/FORMATS/bpackage.md, writer FUN_0058cbb0 and the two functions it
calls per class, FUN_0058d000 and FUN_0058d420):

    u16     version (1)
    u16     import count
    name    import[count]
    u16     class count
    chunk   declaration[class count]
    chunk   link[class count]
    u32     0

The declaration of a class carries its states, enums, fields, methods and
bodies; the link record carries its parent, the state tables it shares, the
slot table and the reference list of each body.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import big

VERSION = 1
MAX_CHUNK_PADDING = 3


class PackageError(ValueError):
    pass


class Reader:
    """Little-endian reader with the alignment of the package writer."""

    def __init__(self, data: bytes, start: int = 0, end: int | None = None):
        self.data = data
        self.pos = start
        self.end = len(data) if end is None else end
        if start > self.end or self.end > len(data):
            raise PackageError("reader is outside the data")

    def remaining(self) -> int:
        return self.end - self.pos

    def _need(self, count: int) -> None:
        if self.pos + count > self.end:
            raise PackageError("truncated value")

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

    def words(self, count: int) -> list[int]:
        self.pos = (self.pos + 3) & ~3
        self._need(count * 4)
        values = list(struct.unpack_from(f"<{count}I", self.data, self.pos))
        self.pos += count * 4
        return values

    def rest(self) -> bytes:
        value = self.data[self.pos:self.end]
        self.pos = self.end
        return value

    def name(self, symbols: list[str]) -> str:
        index = self.u16()
        if index >= len(symbols):
            raise PackageError("name index is outside the symbols of the entry")
        return symbols[index]

    def chunk(self) -> "Reader":
        size = self.u32()
        start = self.pos
        if start + size > self.end:
            raise PackageError("chunk ends outside the data")
        self.pos = start + size
        return Reader(self.data, start, start + size)


def _finish(reader: Reader) -> None:
    if reader.remaining() > MAX_CHUNK_PADDING:
        raise PackageError("chunk content does not end at the chunk")
    reader.pos = reader.end


def _read_type(reader: Reader, symbols: list[str]) -> tuple:
    kind = reader.u16()
    if kind in (9, 10, 11):
        return (kind, reader.name(symbols))
    if kind == 6:
        element = _read_type(reader, symbols)
        return (6, element, reader.u32())
    if kind == 7:
        return (7, _read_type(reader, symbols))
    if kind > 11:
        raise PackageError("unknown type kind")
    return (kind,)


def _read_default(reader: Reader, symbols: list[str], type_: tuple):
    kind = type_[0]
    if kind in (1, 2, 3, 5, 11):
        return {"flag": reader.u16(), "value": reader.u32()}
    if kind == 4:
        flag = reader.u16()
        default = {"flag": flag}
        if flag == 1:
            default["name"] = reader.name(symbols)
        return default
    if kind in (6, 7, 10):
        return {"flag": reader.u16()}
    if kind == 9:
        flag = reader.u16()
        default = {"flag": flag}
        if flag == 1:
            default["object"] = reader.chunk().rest()
        return default
    return None


def _read_declaration(reader: Reader, symbols: list[str]) -> dict:
    cls = {
        "name": reader.name(symbols),
        "kind": reader.u16(),
        "data_size": reader.u16(),
        "slot_count": reader.u16(),
    }
    state_count = reader.u16()
    enum_count = reader.u16()
    field_count = reader.u16()
    method_count = reader.u16()
    body_count = reader.u16()
    states = [reader.name(symbols) for _ in range(state_count)]
    enums = []
    for _ in range(enum_count):
        name = reader.name(symbols)
        word = reader.u16()
        count = reader.u16()
        value_names = [reader.name(symbols) for _ in range(count)]
        values = reader.words(count)
        enums.append({"name": name, "word": word, "values": list(zip(value_names, values))})
    fields = []
    for _ in range(field_count):
        name = reader.name(symbols)
        flags = reader.u16()
        offset = reader.u32()
        type_ = _read_type(reader, symbols)
        fields.append({
            "name": name,
            "flags": flags,
            "offset": offset,
            "type": type_,
            "default": _read_default(reader, symbols, type_),
        })
    methods = []
    for _ in range(method_count):
        name = reader.name(symbols)
        byte4 = reader.u8()
        byte5 = reader.u8()
        slot = reader.u16()
        if slot != 0xFFFF and slot >= cls["slot_count"]:
            raise PackageError("method slot is outside the class")
        result = _read_type(reader, symbols)
        params = []
        for _ in range(reader.u16()):
            params.append((_read_type(reader, symbols), reader.u32()))
        methods.append({
            "name": name,
            "byte4": byte4,
            "byte5": byte5,
            "slot": None if slot == 0xFFFF else slot,
            "result": result,
            "params": params,
        })
    bodies = []
    for _ in range(body_count):
        method = reader.u16()
        flags = reader.u16()
        state = reader.u16()
        locals_size = reader.u16()
        stack_size = reader.u16()
        if method >= method_count:
            raise PackageError("body implements a method outside the class")
        if state >= state_count:
            raise PackageError("body belongs to a state outside the class")
        locals_ = []
        for _ in range(reader.u16()):
            locals_.append((_read_type(reader, symbols), reader.u32()))
        code = []
        if not flags & 1:
            code = reader.words(reader.u32())
        bodies.append({
            "method": method,
            "flags": flags,
            "native": bool(flags & 1),
            "state": state,
            "locals_size": locals_size,
            "stack_size": stack_size,
            "locals": locals_,
            "code": code,
        })
    _finish(reader)
    cls.update(states=states, enums=enums, fields=fields, methods=methods, bodies=bodies)
    return cls


def _read_link(cls: dict, reader: Reader, symbols: list[str]) -> None:
    parent = None
    if reader.u16():
        parent = reader.name(symbols)
    state_owners = []
    for _ in range(len(cls["states"])):
        flag = reader.u16()
        if flag == 0:
            state_owners.append(None)
        elif flag == 1:
            state_owners.append(reader.name(symbols))
        else:
            raise PackageError("bad state ownership flag")
    slots = []
    while True:
        state = reader.u16()
        if state == 0xFFFF:
            break
        slot = reader.u16()
        class_name = reader.name(symbols)
        body = reader.u16()
        if state >= len(cls["states"]):
            raise PackageError("slot entry names a state outside the class")
        if slot >= cls["slot_count"]:
            raise PackageError("slot entry is outside the class")
        slots.append({"state": state, "slot": slot, "class": class_name, "body": body})
    references = {}
    while True:
        body = reader.u16()
        if body == 0xFFFF:
            break
        if body >= len(cls["bodies"]):
            raise PackageError("reference list of a body outside the class")
        refs = []
        while True:
            kind = reader.u16()
            if kind == 0xFFFF:
                break
            if kind > 5:
                raise PackageError("unknown reference kind")
            word = reader.u16()
            if word >= len(cls["bodies"][body]["code"]):
                raise PackageError("reference word is outside the body code")
            ref = {"kind": kind, "word": word, "name": reader.name(symbols)}
            if kind in (3, 4):
                ref["index"] = reader.u16()
            elif kind == 5:
                ref["second"] = reader.name(symbols)
                ref["index"] = reader.u16()
            refs.append(ref)
        references[body] = refs
    _finish(reader)
    cls.update(parent=parent, state_owners=state_owners, slots=slots, references=references)


def parse(data: bytes, symbols: list[str]) -> dict:
    """Return the package of a `bpackage` payload with its symbol list."""
    reader = Reader(data)
    version = reader.u16()
    if version != VERSION:
        raise PackageError("unsupported package version")
    imports = [reader.name(symbols) for _ in range(reader.u16())]
    class_count = reader.u16()
    declarations = [reader.chunk() for _ in range(class_count)]
    links = [reader.chunk() for _ in range(class_count)]
    if reader.u32() != 0:
        raise PackageError("package does not end with zero")
    if reader.remaining() != 0:
        raise PackageError("package has bytes after its end")
    classes = [_read_declaration(chunk, symbols) for chunk in declarations]
    for cls, chunk in zip(classes, links):
        _read_link(cls, chunk, symbols)
    return {"version": version, "imports": imports, "classes": classes}


def load_packages(path: str | Path) -> list[tuple[str, dict]]:
    """Return the (entry name, package) pairs of every `bpackage` of a container."""
    names, entries = big.load(Path(path))
    packages = []
    for entry in entries:
        if entry.type != "bpackage":
            continue
        symbols = []
        for index in entry.symbols:
            if index >= len(names):
                raise PackageError("entry symbol is outside the name table")
            symbols.append(names[index])
        packages.append((entry.name, parse(entry.data, symbols)))
    return packages


def package_totals(package: dict) -> dict:
    classes = package["classes"]
    bodies = [body for cls in classes for body in cls["bodies"]]
    return {
        "classes": len(classes),
        "methods": sum(len(cls["methods"]) for cls in classes),
        "bodies": len(bodies),
        "natives": sum(1 for body in bodies if body["native"]),
        "words": sum(len(body["code"]) for body in bodies),
        "fields": sum(len(cls["fields"]) for cls in classes),
        "enums": sum(len(cls["enums"]) for cls in classes),
    }


def cmd_list(args) -> int:
    for name, package in load_packages(args.container):
        totals = package_totals(package)
        print(
            f"{name}\t{package['version']}\t{len(package['imports'])}\t"
            f"{totals['classes']}\t{totals['methods']}\t{totals['bodies']}\t"
            f"{totals['natives']}\t{totals['words']}"
        )
    return 0


def cmd_classes(args) -> int:
    for name, package in load_packages(args.container):
        if name != args.package:
            continue
        for cls in package["classes"]:
            print(
                f"{cls['name']}\t{cls['kind']}\t{cls['parent'] or '-'}\t"
                f"{cls['data_size']}\t{cls['slot_count']}\t"
                f"{','.join(cls['states'])}\t{len(cls['fields'])}\t"
                f"{len(cls['methods'])}\t{len(cls['bodies'])}"
            )
        return 0
    print(f"no bpackage {args.package!r}", file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Read a Blood Omen 2 bpackage entry")
    sub = parser.add_subparsers(dest="command", required=True)
    p_list = sub.add_parser("list", help="print one line per package")
    p_list.add_argument("container")
    p_list.set_defaults(func=cmd_list)
    p_classes = sub.add_parser("classes", help="print one line per class of a package")
    p_classes.add_argument("container")
    p_classes.add_argument("package")
    p_classes.set_defaults(func=cmd_classes)
    args = parser.parse_args()
    try:
        return args.func(args)
    except (OSError, big.BigError, PackageError) as error:
        print(f"bpackage: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
