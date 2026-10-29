#!/usr/bin/env python3
"""Reference reader for Blood Omen 2 `animatio` entries (docs/FORMATS/animatio.md).

    python3 tools/bo2fmt/animatio.py nodes <entry file> [--symbols-from container.big --entry NAME]
    python3 tools/bo2fmt/animatio.py nodes --big container.big --entry NAME [--match text]
    python3 tools/bo2fmt/animatio.py states --big container.big --entry NAME [--match text]

`nodes` lists the clip nodes of a graph (in, out, speed, reel). `states` lists
the state machine: for each state its transitions and the actions that play a
sequence (and so which clip nodes), start another state or start a state on
another layer. With `--big` the reel names come from the entry's symbol list.

The version 0 stream is read exactly as FUN_005e8ee0 does and must end at the
last byte; the version 1 alias list is read as FUN_005e9ae0 does. Fields whose
meaning is not decoded keep their raw values.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

import big

# FUN_00668700 and the other readers use these primitives (FUN_006a09a0,
# FUN_006a09e0, FUN_006a0920, FUN_006a0960): ints and floats are 4-byte aligned,
# u16 values 2-byte aligned and bytes are not aligned.
FORMAT_ID = 0  # first int; FUN_005e9ae0 returns null for anything else
VERSION_GRAPH = 0  # second int: a graph (FUN_005e8ee0)
VERSION_ALIAS = 1  # second int: a list of resource references (FUN_005e9840)

# Serialized class ids of the object table, from the GameAnimationManager
# (vtable 0x867cf8) tables: id 0 is the engine's null object, 1 and 2 are the
# two game classes created by FUN_004a5ce0 and FUN_004a5fe0.
CLASS_NULL = 0
CLASS_CLIP_AND_BONE_ID = 1  # reads an int and a tagged clip reference
CLASS_CLIP_AND_BONE_ID2 = 2  # reads two ints and a tagged clip reference

# Action kinds, FUN_00642c00 (switch) and FUN_006429b0 (operand fixup).
ACTION_PLAY_SEQUENCE = 1  # FUN_00643f10(sequence, blend, ...)
ACTION_GOTO_STATE = 2  # continue with another state on the same layer
ACTION_START_STATE = 3  # FUN_005e3b80(state, ..., layer = arg)
ACTION_PLAY_ON_LAYER = 4  # FUN_005e3e50(sequence, blend, ..., layer = arg)
ACTION_CREATE_ENTITY = 5  # factory object at arg (layer type table)
ACTION_CALL = 6  # call a mapped function

# Condition functions with fixed ids (FUN_00613220): combinators over operands.
CONDITION_NOT = 0x400  # FUN_006134e0
CONDITION_OR = 0x401  # FUN_00613510
CONDITION_AND = 0x402  # FUN_00613570

# Tagged references are (index << 1) | 1; 0 is null (FUN_005e1b60 and friends).
REEL_TABLE2_BASE = 0x4000  # reel indexes >= this live in the second reel table

# Port-only limit on a stream element count so a corrupt count cannot
# allocate gigabytes. Not an original limit.
MAX_COUNT = 1 << 22


class AnimatioError(ValueError):
    pass


@dataclass(frozen=True)
class Ref:
    """FUN_005e9840: two u16 indexes into the entry symbol list (name, type)."""

    name: int
    kind: int


@dataclass
class Action:
    """FUN_005e8e00: four ints, plus a fifth for kinds 1 to 6."""

    kind: int
    target: int  # sequence index (kinds 1, 4) or state index (kinds 2, 3)
    blend: int  # tagged index into Graph.blends, 0 when none
    param: int  # tagged index into Graph.variants, 0 when none
    arg: int = 0  # layer for kinds 3 and 4; tagged layer-type index for kind 5


@dataclass
class Transition:
    """0x14 bytes (FUN_005ea770, FUN_00642c00): condition, then the actions it runs."""

    variant: int  # tagged index into Graph.variants (the condition operand)
    condition: int  # condition function id, resolved through FUN_0060f760
    actions: list = field(default_factory=list)


@dataclass
class State:
    """0x24 bytes (FUN_005eab00, fixup FUN_00642a80)."""

    transitions: list
    variant: int  # +0x0c tagged index into Graph.variants
    factory: int  # +0x10 factory id (FUN_0060f760), -1 when none
    child: int  # +0x14 state index, -1 when none
    blend: int  # +0x18 tagged index into Graph.blends
    obj: int  # +0x1c tagged index into Graph.objects
    name_ref: int  # +0x20 tagged byte offset into Graph.names
    name: str = ""


@dataclass
class Binding:
    """8 bytes (FUN_005eaea0)."""

    value: int
    condition: int


@dataclass
class Sequence:
    """12 bytes (FUN_005eb000, fixup FUN_00643d60)."""

    head: int  # tagged index of the first Graph.chain element
    events: int  # tagged index into Graph.event_lists
    name_ref: int
    name: str = ""


@dataclass
class ChainLink:
    """8 bytes (FUN_005eb180): a sequence is the run of links up to a clip of -1."""

    clip: int  # clip node index, -1 ends the run
    blend: int  # tagged index into Graph.blends


@dataclass
class ClipNode:
    """36 bytes (FUN_005eb2e0, fixup FUN_005e1b60, printer FUN_005e1c20)."""

    in_time: float
    out_time: float
    speed: float
    reel_ref: int  # +0x0c tagged reel index
    events: int  # +0x10 tagged index into Graph.event_lists
    offset: int  # +0x14 tagged index into Graph.offsets
    blend: int  # +0x18 tagged index into Graph.blends
    mask: int  # +0x1c tagged index into Graph.masks
    name_ref: int  # +0x20 tagged byte offset into Graph.names
    name: str = ""

    @property
    def reel_index(self) -> int | None:
        return None if self.reel_ref == 0 else self.reel_ref >> 1


@dataclass
class Graph:
    version: int
    flags: int  # the third int
    header: tuple  # nine 32-bit values stored as floats (opaque)
    states: list
    bindings: list
    sequences: list
    chain: list
    clips: list
    reels2: list  # second reel table, FUN_005e9180
    reels: list  # first reel table, FUN_005eb5d0: list of Ref
    masks: list  # FUN_005eb700: (byte, byte, u16, int)
    bytes80: bytes
    layers: list  # FUN_005eb900: (type id, tagged mask index)
    symbols: list  # FUN_005e9460: (symbol index, state index)
    ints: list
    names: bytes
    objects: list  # (class id, ints...)
    blends: list  # FUN_005eba40: (float, int, int, int)
    offsets: list  # FUN_005e8ab0: nine floats
    event_lists: list  # FUN_005ebbf0: lists of (time, int, int, int)
    variants: list  # FUN_005ebed0: (type, value)
    last: int

    def name_at(self, tag: int) -> str:
        if tag == 0:
            return ""
        offset = tag >> 1
        if offset >= len(self.names):
            raise AnimatioError(f"name offset {offset} is outside the name pool")
        end = self.names.find(b"\0", offset)
        end = len(self.names) if end < 0 else end
        return self.names[offset:end].decode("latin-1")

    def sequence_clips(self, sequence: int) -> list:
        """Clip node indexes of a sequence (FUN_00643d60 walks the run of links)."""
        head = self.sequences[sequence].head
        if head == 0:
            return []
        out = []
        index = head >> 1
        while index < len(self.chain) and self.chain[index].clip >= 0:
            out.append(self.chain[index].clip)
            index += 1
        return out

    def reel(self, clip: ClipNode) -> Ref | None:
        index = clip.reel_index
        if index is None:
            return None
        if index >= REEL_TABLE2_BASE:
            return None  # a pair of reels (second table); not resolved to one name
        return self.reels[index] if index < len(self.reels) else None


class _Stream:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def _take(self, fmt: str, align: int, what: str):
        size = struct.calcsize(fmt)
        pos = (self.pos + align - 1) & ~(align - 1)
        if pos + size > len(self.data):
            raise AnimatioError(f"truncated {what} at offset {pos:#x}")
        self.pos = pos + size
        return struct.unpack_from(fmt, self.data, pos)[0]

    def i32(self, what: str = "int") -> int:
        return self._take("<i", 4, what)

    def u32(self, what: str = "u32") -> int:
        return self._take("<I", 4, what)

    def f32(self, what: str = "float") -> float:
        return self._take("<f", 4, what)

    def u16(self, what: str = "u16") -> int:
        return self._take("<H", 2, what)

    def u8(self, what: str = "byte") -> int:
        return self._take("<B", 1, what)

    def count(self, element_size: int, what: str) -> int:
        n = self.i32(what + " count")
        if n < 0 or n > MAX_COUNT or n * element_size > len(self.data) - self.pos:
            raise AnimatioError(f"{what} count {n} does not fit in the stream")
        return n

    def ref(self) -> Ref:
        return Ref(self.u16("reference name"), self.u16("reference type"))


def _action(s: _Stream) -> Action:
    kind = s.i32("action kind")
    target = s.i32("action target")
    blend = s.i32("action blend")
    param = s.i32("action parameter")
    arg = 0
    if 1 <= kind <= 6:
        arg = s.i32("action argument")
    return Action(kind, target, blend, param, arg)


def _states(s: _Stream) -> list:
    out = []
    for _ in range(s.count(4, "state")):
        transitions = []
        for _ in range(s.count(12, "transition")):
            variant = s.i32("transition variant")
            condition = s.i32("transition condition")
            actions = [_action(s) for _ in range(s.count(16, "action"))]
            transitions.append(Transition(variant, condition, actions))
        tail = [s.i32("state field") for _ in range(6)]
        out.append(State(transitions, *tail))
    return out


def _pairs(s: _Stream, what: str) -> list:
    return [(s.i32(what), s.i32(what)) for _ in range(s.count(8, what))]


def parse(data: bytes) -> Graph | list:
    """Parse an `animatio` entry.

    Returns a Graph for version 0 and a list of Ref for version 1 (an alias
    entry that only names other resources).
    """
    s = _Stream(data)
    if s.i32("format id") != FORMAT_ID:
        raise AnimatioError("unsupported format id")
    version = s.i32("version")
    if version == VERSION_ALIAS:
        refs = [s.ref() for _ in range(s.count(4, "reference"))]
        if s.pos != len(data):
            raise AnimatioError(f"{len(data) - s.pos} trailing bytes after the references")
        return refs
    if version != VERSION_GRAPH:
        raise AnimatioError(f"unsupported version {version}")

    flags = s.i32("flags")
    header = tuple(s.u32("header value") for _ in range(9))
    states = _states(s)
    bindings = [Binding(a, b) for a, b in _pairs(s, "binding")]
    sequences = [Sequence(*(s.i32("sequence field") for _ in range(3)))
                 for _ in range(s.count(12, "sequence"))]
    chain = [ChainLink(a, b) for a, b in _pairs(s, "chain link")]
    clips = []
    for _ in range(s.count(36, "clip node")):
        in_time, out_time, speed = s.f32("clip in"), s.f32("clip out"), s.f32("clip speed")
        clips.append(ClipNode(in_time, out_time, speed,
                              *(s.i32("clip field") for _ in range(6))))
    reels2 = []
    for _ in range(s.count(4, "second reel")):
        if s.i32("second reel tag") == 0:
            reels2.append((s.ref(), s.ref()))
        else:
            reels2.append(None)
    reels = [s.ref() for _ in range(s.count(4, "reel"))]
    masks = []
    for _ in range(s.count(8, "mask")):
        masks.append((s.u8(), s.u8(), s.u16(), s.i32()))
    bytes80 = bytes(s.u8() for _ in range(s.count(1, "byte table")))
    layers = _pairs(s, "layer")
    symbols = []
    for _ in range(s.count(6, "symbol")):
        symbols.append((s.u16("symbol"), s.i32("symbol state")))
    ints = [s.i32() for _ in range(s.count(4, "int table"))]
    names = bytes(s.u8() for _ in range(s.count(1, "name pool")))
    objects = []
    for _ in range(s.count(4, "object")):
        class_id = s.i32("class id")
        if class_id < 0:
            objects.append((class_id,))
        elif class_id == CLASS_CLIP_AND_BONE_ID:
            objects.append((class_id, s.i32(), s.i32()))
        elif class_id == CLASS_CLIP_AND_BONE_ID2:
            objects.append((class_id, s.i32(), s.i32(), s.i32()))
        else:
            raise AnimatioError(f"unknown object class {class_id}")
    blends = [(s.f32(), s.i32(), s.i32(), s.i32()) for _ in range(s.count(16, "blend"))]
    offsets = [tuple(s.f32() for _ in range(9)) for _ in range(s.count(36, "offset"))]
    event_lists = []
    for _ in range(s.count(4, "event list")):
        event_lists.append([(s.f32(), s.i32(), s.i32(), s.i32())
                            for _ in range(s.count(16, "event"))])
    variants = []
    for _ in range(s.count(8, "variant")):
        kind = s.i32("variant type")
        if kind in (1, 4, 5):
            variants.append((kind, s.f32("variant value")))
        elif kind in (9, 10):
            variants.append((kind, s.u16("variant symbol")))
        else:
            variants.append((kind, s.i32("variant value")))
    last = s.i32("final int")
    if s.pos != len(data):
        raise AnimatioError(f"{len(data) - s.pos} trailing bytes after the graph")

    graph = Graph(VERSION_GRAPH, flags, header, states, bindings, sequences, chain, clips,
                  reels2, reels, masks, bytes80, layers, symbols, ints, names, objects,
                  blends, offsets, event_lists, variants, last)
    for state in graph.states:
        state.name = graph.name_at(state.name_ref)
    for sequence in graph.sequences:
        sequence.name = graph.name_at(sequence.name_ref)
    for clip in graph.clips:
        clip.name = graph.name_at(clip.name_ref)
    return graph


def reel_name(graph: Graph, clip: ClipNode, symbol_names: list | None) -> str:
    """Name of the clip's reel, given the entry's symbol list resolved to strings."""
    ref = graph.reel(clip)
    if ref is None:
        return ""
    if symbol_names is None or ref.name >= len(symbol_names):
        return f"symbol {ref.name}"
    return symbol_names[ref.name]


def _symbol_names(entry: "big.Entry", names: list) -> list:
    return [names[value] if value < len(names) else f"name {value}" for value in entry.symbols]


def _load(args) -> tuple[Graph, list | None]:
    symbol_names = None
    if args.big:
        if not args.entry:
            raise AnimatioError("--big needs --entry")
        names, entries = big.load(Path(args.big))
        matches = [e for e in entries if e.type == "animatio" and e.name == args.entry]
        if not matches:
            raise AnimatioError(f"no animatio entry named {args.entry}")
        data = matches[0].data
        symbol_names = _symbol_names(matches[0], names)
    else:
        if not args.entry_file:
            raise AnimatioError("give an entry file or --big and --entry")
        data = Path(args.entry_file).read_bytes()
    graph = parse(data)
    if not isinstance(graph, Graph):
        raise AnimatioError("version 1 alias entry: " +
                            ", ".join(str(ref) for ref in graph))
    return graph, symbol_names


def cmd_nodes(args) -> int:
    graph, symbol_names = _load(args)
    needle = args.match.lower()
    for index, clip in enumerate(graph.clips):
        reel = reel_name(graph, clip, symbol_names)
        if needle and needle not in clip.name.lower() and needle not in reel.lower():
            continue
        reel_index = "-" if clip.reel_index is None else clip.reel_index
        print(f"{index:4d} {clip.name:<28} in {clip.in_time:8.4f} out {clip.out_time:8.4f} "
              f"speed {clip.speed:6.3f} reel {reel_index} {reel}")
    return 0


def _describe(graph: Graph, symbol_names: list | None, action: Action) -> str:
    if action.kind in (ACTION_PLAY_SEQUENCE, ACTION_PLAY_ON_LAYER):
        seq = graph.sequences[action.target]
        clips = [f"{graph.clips[c].name}({reel_name(graph, graph.clips[c], symbol_names)})"
                 for c in graph.sequence_clips(action.target)]
        where = "" if action.kind == ACTION_PLAY_SEQUENCE else f" on layer {action.arg}"
        return f"play {seq.name} [{' '.join(clips)}]{where}"
    if action.kind in (ACTION_GOTO_STATE, ACTION_START_STATE):
        verb = "goto" if action.kind == ACTION_GOTO_STATE else "start"
        where = "" if action.kind == ACTION_GOTO_STATE else f" on layer {action.arg}"
        return f"{verb} state {action.target} {graph.states[action.target].name}{where}"
    return f"action {action.kind} target {action.target} arg {action.arg:#x}"


def cmd_states(args) -> int:
    graph, symbol_names = _load(args)
    needle = args.match.lower()
    for index, state in enumerate(graph.states):
        if needle and needle not in state.name.lower():
            continue
        extra = ""
        if state.factory != -1:
            extra = f" factory {state.factory:#x} child {state.child}"
        print(f"state {index} {state.name}{extra}")
        for transition in state.transitions:
            print(f"  if condition {transition.condition:#x} operand {transition.variant >> 1}")
            for action in transition.actions:
                print(f"    {_describe(graph, symbol_names, action)}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Read Blood Omen 2 animatio entries")
    sub = parser.add_subparsers(dest="command", required=True)
    for name, func, help_text in (
        ("nodes", cmd_nodes, "list the clip nodes of an animatio graph"),
        ("states", cmd_states, "list the states, their transitions and actions"),
    ):
        p = sub.add_parser(name, help=help_text)
        p.add_argument("entry_file", nargs="?", help="extracted animatio entry")
        p.add_argument("--big", help="container holding the entry")
        p.add_argument("--entry", help="animatio entry name inside --big")
        p.add_argument("--match", default="", help="case-insensitive name filter")
        p.set_defaults(func=func)
    args = parser.parse_args()
    try:
        return args.func(args)
    except (AnimatioError, big.BigError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
