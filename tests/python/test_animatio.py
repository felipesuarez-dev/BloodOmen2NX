import argparse
import contextlib
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import animatio  # noqa: E402
import big  # noqa: E402


class Writer:
    """Writes the stream primitives with the alignment of FUN_006a09a0 and friends."""

    def __init__(self):
        self.out = bytearray()

    def _align(self, n):
        while len(self.out) % n:
            self.out.append(0)

    def i(self, value):
        self._align(4)
        self.out += struct.pack("<i", value)

    def u32(self, value):
        self._align(4)
        self.out += struct.pack("<I", value)

    def f(self, value):
        self._align(4)
        self.out += struct.pack("<f", value)

    def h(self, value):
        self._align(2)
        self.out += struct.pack("<H", value)

    def b(self, value):
        self.out.append(value)


def tag(index):
    return (index << 1) | 1


NAMES = b"cl_a\0cl_b\0st_a\0st_b\0sq_a\0"
NAME_CL_A, NAME_CL_B, NAME_ST_A, NAME_ST_B, NAME_SQ_A = 0, 5, 10, 15, 20


def action(w, kind, target, blend, param, arg):
    for value in (kind, target, blend, param):
        w.i(value)
    if 1 <= kind <= 6:
        w.i(arg)


def graph_bytes(trailing=b"", names=NAMES, objects=None):
    w = Writer()
    w.i(0)  # format id
    w.i(0)  # version: graph
    w.i(0)  # flags
    for value in range(1, 10):
        w.u32(0x4F000000 + value)  # nine opaque header values
    # states
    w.i(2)
    w.i(1)  # state 0: one transition
    w.i(tag(0))
    w.i(0x402)
    w.i(2)
    action(w, animatio.ACTION_PLAY_SEQUENCE, 0, 0, 0, 0x5C0073)
    action(w, animatio.ACTION_START_STATE, 1, 0, 0, 1)
    for value in (0, -1, -1, 0, 0, tag(NAME_ST_A)):
        w.i(value)
    w.i(0)  # state 1: no transitions
    for value in (tag(0), 0x401, 0, 0, 0, tag(NAME_ST_B)):
        w.i(value)
    # bindings
    w.i(1)
    w.i(5)
    w.i(8)
    # sequences
    w.i(1)
    for value in (tag(0), 0, tag(NAME_SQ_A)):
        w.i(value)
    # chain: clip 1, then the terminator
    w.i(2)
    w.i(1)
    w.i(0)
    w.i(-1)
    w.i(0)
    # clip nodes
    w.i(2)
    w.f(0.0)
    w.f(0.8)
    w.f(1.5)
    for value in (tag(1), 0, 0, 0, 0, tag(NAME_CL_A)):
        w.i(value)
    w.f(0.25)
    w.f(1.0)
    w.f(1.7)
    for value in (tag(0), tag(0), 0, tag(0), tag(0), tag(NAME_CL_B)):
        w.i(value)
    # second reel table: one pair, one empty slot
    w.i(2)
    w.i(0)
    w.h(7)
    w.h(1)
    w.h(8)
    w.h(1)
    w.i(1)
    # reels
    w.i(2)
    for name, kind in ((0, 1), (2, 1)):
        w.h(name)
        w.h(kind)
    # masks: byte, byte, u16, int (unaligned neighbours)
    w.i(1)
    w.b(1)
    w.b(2)
    w.h(3)
    w.i(4)
    # byte table: three bytes, the next int is aligned again
    w.i(3)
    for value in (1, 2, 3):
        w.b(value)
    # layer types
    w.i(1)
    w.i(5)
    w.i(1)
    # symbol -> state map
    w.i(1)
    w.h(3)
    w.i(0)
    # int table
    w.i(1)
    w.i(7)
    # name pool
    w.i(len(names))
    for value in names:
        w.b(value)
    # objects
    objects = [(1, 43, 77), (2, 1, 2, 3), (-1,)] if objects is None else objects
    w.i(len(objects))
    for obj in objects:
        for value in obj:
            w.i(value)
    # blends
    w.i(1)
    w.f(0.2)
    w.i(7)
    w.i(1)
    w.i(0)
    # offsets
    w.i(1)
    for value in range(9):
        w.f(float(value))
    # event lists
    w.i(1)
    w.i(1)
    w.f(0.5)
    for value in (1, 2, 3):
        w.i(value)
    # variants: float, int, sound symbol, effect symbol
    w.i(4)
    w.i(1)
    w.f(0.25)
    w.i(2)
    w.i(9)
    w.i(9)
    w.h(5)
    w.i(10)
    w.h(6)
    w.i(0)  # final int
    return bytes(w.out) + trailing


class ParseTest(unittest.TestCase):
    def setUp(self):
        self.graph = animatio.parse(graph_bytes())

    def test_header(self):
        self.assertEqual(self.graph.version, 0)
        self.assertEqual(self.graph.header[0], 0x4F000001)
        self.assertEqual(len(self.graph.header), 9)

    def test_states_and_actions(self):
        first, second = self.graph.states
        self.assertEqual(first.name, "st_a")
        self.assertEqual(second.name, "st_b")
        transition = first.transitions[0]
        self.assertEqual(transition.condition, animatio.CONDITION_AND)
        self.assertEqual([a.kind for a in transition.actions],
                         [animatio.ACTION_PLAY_SEQUENCE, animatio.ACTION_START_STATE])
        self.assertEqual(transition.actions[0].arg, 0x5C0073)
        self.assertEqual((transition.actions[1].target, transition.actions[1].arg), (1, 1))
        self.assertEqual(second.transitions, [])
        self.assertEqual(second.factory, animatio.CONDITION_OR)

    def test_action_without_argument(self):
        # Kind 0 and kinds above 6 carry only four ints (FUN_005e8e00).
        for kind in (0, 7):
            w = Writer()
            action(w, kind, 3, 0, 0, 99)
            w.i(123)  # the next field must not be consumed as an argument
            s = animatio._Stream(bytes(w.out))
            parsed = animatio._action(s)
            self.assertEqual((parsed.kind, parsed.target, parsed.arg), (kind, 3, 0))
            self.assertEqual(s.i32(), 123)

    def test_sequences_and_chain(self):
        sequence = self.graph.sequences[0]
        self.assertEqual(sequence.name, "sq_a")
        self.assertEqual(self.graph.sequence_clips(0), [1])

    def test_clip_nodes(self):
        first, second = self.graph.clips
        self.assertEqual((first.name, first.in_time), ("cl_a", 0.0))
        self.assertAlmostEqual(first.out_time, 0.8, places=6)
        self.assertAlmostEqual(first.speed, 1.5, places=6)
        self.assertEqual(first.reel_index, 1)
        self.assertAlmostEqual(second.speed, 1.7, places=6)
        self.assertEqual(second.reel_index, 0)
        self.assertEqual(second.name, "cl_b")

    def test_reels_and_names(self):
        self.assertEqual(self.graph.reels, [animatio.Ref(0, 1), animatio.Ref(2, 1)])
        self.assertEqual(self.graph.reels2, [(animatio.Ref(7, 1), animatio.Ref(8, 1)), None])
        symbols = ["kain/walk", "", "kain/run"]
        self.assertEqual(animatio.reel_name(self.graph, self.graph.clips[0], symbols), "kain/run")
        self.assertEqual(animatio.reel_name(self.graph, self.graph.clips[1], symbols), "kain/walk")
        self.assertEqual(animatio.reel_name(self.graph, self.graph.clips[0], None), "symbol 2")

    def test_alignment_of_small_fields(self):
        self.assertEqual(self.graph.masks, [(1, 2, 3, 4)])
        self.assertEqual(self.graph.bytes80, b"\x01\x02\x03")
        self.assertEqual(self.graph.layers, [(5, 1)])
        self.assertEqual(self.graph.symbols, [(3, 0)])
        self.assertEqual(self.graph.ints, [7])
        self.assertEqual(self.graph.names, NAMES)

    def test_objects_blends_events_variants(self):
        self.assertEqual(self.graph.objects, [(1, 43, 77), (2, 1, 2, 3), (-1,)])
        self.assertAlmostEqual(self.graph.blends[0][0], 0.2, places=6)
        self.assertEqual(self.graph.offsets[0], tuple(float(v) for v in range(9)))
        self.assertEqual(self.graph.event_lists, [[(0.5, 1, 2, 3)]])
        self.assertEqual(self.graph.variants, [(1, 0.25), (2, 9), (9, 5), (10, 6)])
        self.assertEqual(self.graph.last, 0)

    def test_null_and_bad_name_tags(self):
        self.assertEqual(self.graph.name_at(0), "")
        with self.assertRaises(animatio.AnimatioError):
            self.graph.name_at(tag(len(NAMES) + 5))


class RejectionTest(unittest.TestCase):
    def test_trailing_bytes(self):
        with self.assertRaises(animatio.AnimatioError):
            animatio.parse(graph_bytes(trailing=b"\0\0\0\0"))

    def test_truncated(self):
        data = graph_bytes()
        for cut in (3, 12, 40, len(data) // 2, len(data) - 1):
            with self.assertRaises(animatio.AnimatioError):
                animatio.parse(data[:cut])

    def test_bad_format_and_version(self):
        data = graph_bytes()
        with self.assertRaises(animatio.AnimatioError):
            animatio.parse(struct.pack("<i", 1) + data[4:])
        with self.assertRaises(animatio.AnimatioError):
            animatio.parse(data[:4] + struct.pack("<i", 2) + data[8:])

    def test_unknown_object_class(self):
        with self.assertRaises(animatio.AnimatioError):
            animatio.parse(graph_bytes(objects=[(3, 0)]))

    def test_count_larger_than_the_stream(self):
        # The state count claims more records than the remaining bytes can hold.
        data = bytearray(graph_bytes())
        struct.pack_into("<i", data, 48, 0x7FFFFFFF)
        with self.assertRaises(animatio.AnimatioError):
            animatio.parse(bytes(data))
        struct.pack_into("<i", data, 48, -1)
        with self.assertRaises(animatio.AnimatioError):
            animatio.parse(bytes(data))


class AliasTest(unittest.TestCase):
    def test_version_one_list(self):
        w = Writer()
        w.i(0)
        w.i(1)
        w.i(1)
        w.h(0)
        w.h(1)
        self.assertEqual(animatio.parse(bytes(w.out)), [animatio.Ref(0, 1)])

    def test_version_one_trailing_bytes(self):
        w = Writer()
        w.i(0)
        w.i(1)
        w.i(0)
        with self.assertRaises(animatio.AnimatioError):
            animatio.parse(bytes(w.out) + b"\0\0\0\0")


class CliTest(unittest.TestCase):
    def run_command(self, func, **kwargs):
        args = argparse.Namespace(entry_file=None, big=None, entry=None, match="")
        for key, value in kwargs.items():
            setattr(args, key, value)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(func(args), 0)
        return out.getvalue()

    def test_nodes_from_a_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "entry"
            path.write_bytes(graph_bytes())
            text = self.run_command(animatio.cmd_nodes, entry_file=str(path))
        self.assertIn("cl_a", text)
        self.assertIn("speed  1.700", text)
        self.assertIn("symbol 2", text)  # no symbol list without --big

    def test_nodes_from_a_container_resolve_reel_names(self):
        entry = big.Entry("animatio", "g", 0, 0, 0, graph_bytes(), symbols=(0, 1, 2))
        container = big.build(["kain/walk", "", "kain/run"], [entry])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.big"
            path.write_bytes(container)
            text = self.run_command(animatio.cmd_nodes, big=str(path), entry="g")
            self.assertIn("reel 1 kain/run", text)
            self.assertIn("reel 0 kain/walk", text)
            filtered = self.run_command(animatio.cmd_nodes, big=str(path), entry="g",
                                        match="cl_b")
            self.assertNotIn("cl_a", filtered)
            states = self.run_command(animatio.cmd_states, big=str(path), entry="g")
        self.assertIn("play sq_a [cl_b(kain/walk)]", states)
        self.assertIn("start state 1 st_b on layer 1", states)
        self.assertIn("if condition 0x402", states)

    def test_missing_entry(self):
        entry = big.Entry("animatio", "g", 0, 0, 0, graph_bytes(), symbols=(0, 1, 2))
        container = big.build(["a", "b", "c"], [entry])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.big"
            path.write_bytes(container)
            with self.assertRaises(animatio.AnimatioError):
                self.run_command(animatio.cmd_nodes, big=str(path), entry="missing")


if __name__ == "__main__":
    unittest.main()
