import os
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import bpackage  # noqa: E402
from bpackage import PackageError  # noqa: E402

SYMBOLS = [
    "creatures",
    "critter",
    "monsterscript",
    "default",
    "idlestate",
    "health",
    "start",
    "takepain",
    "message",
    "object",
    "enemy",
    "sense",
    "sight",
]
INDEX = {name: index for index, name in enumerate(SYMBOLS)}


class Writer:
    """Writes a payload with the alignment of the package writer."""

    def __init__(self):
        self.data = bytearray()

    def align(self, size):
        while len(self.data) % size:
            self.data.append(0)

    def u8(self, value):
        self.data.append(value)

    def u16(self, value):
        self.align(2)
        self.data += struct.pack("<H", value)

    def u32(self, value):
        self.align(4)
        self.data += struct.pack("<I", value)

    def name(self, text):
        self.u16(INDEX[text])

    def type(self, type_):
        self.u16(type_[0])
        if type_[0] in (9, 10, 11):
            self.name(type_[1])
        elif type_[0] == 6:
            self.type(type_[1])
            self.u32(type_[2])
        elif type_[0] == 7:
            self.type(type_[1])

    def chunk(self, content, padding=0):
        self.u32(len(content) + padding)
        self.data += content
        self.data += b"\0" * padding


def declaration(method_slot=3, body_method=1, body_state=0, omit_code_count=False):
    w = Writer()
    w.name("critter")
    w.u16(0)    # kind
    w.u16(41)   # data size
    w.u16(16)   # slot count
    w.u16(2)    # states
    w.u16(1)    # enums
    w.u16(1)    # fields
    w.u16(2)    # methods
    w.u16(2)    # bodies
    w.name("default")
    w.name("idlestate")
    w.name("sense")
    w.u16(0)
    w.u16(2)
    w.name("sight")
    w.name("enemy")
    w.u32(0)
    w.u32(1)
    w.name("health")
    w.u16(1)
    w.u32(4)
    w.type((3,))
    w.u16(1)
    w.u32(0x3F800000)
    w.name("start")
    w.u8(0)
    w.u8(0)
    w.u16(0)
    w.type((0,))
    w.u16(0)
    w.name("takepain")
    w.u8(1)
    w.u8(2)
    w.u16(method_slot)
    w.type((2,))
    w.u16(1)
    w.type((2,))
    w.u32(8)
    w.u16(0)
    w.u16(0)
    w.u16(body_state)
    w.u16(0)
    w.u16(0)
    w.u16(0)
    if not omit_code_count:
        w.u32(3)
        w.u32(0x04000000)
        w.u32(0x21000001)
        w.u32(0x5C000000)
    w.u16(body_method)
    w.u16(1)    # native
    w.u16(0)
    w.u16(0)
    w.u16(0)
    w.u16(0)
    return bytes(w.data)


def link(slot_state=0, slot_slot=0, reference_kind=0, reference_body=0, reference_word=0):
    w = Writer()
    w.u16(1)
    w.name("monsterscript")
    w.u16(0)
    w.u16(1)
    w.name("monsterscript")
    w.u16(slot_state)
    w.u16(slot_slot)
    w.name("critter")
    w.u16(0)
    w.u16(0xFFFF)
    w.u16(reference_body)
    w.u16(reference_kind)
    w.u16(reference_word)
    w.name("message")
    w.u16(2)
    w.u16(1)
    w.name("creatures")
    w.u16(4)
    w.u16(2)
    w.name("critter")
    w.u16(1)
    w.u16(5)
    w.u16(2)
    w.name("monsterscript")
    w.name("critter")
    w.u16(0)
    w.u16(0xFFFF)
    w.u16(0xFFFF)
    return bytes(w.data)


def build_package(declaration_bytes=None, link_bytes=None, version=1, trailer=0,
                  extra=b"", declaration_padding=0):
    w = Writer()
    w.u16(version)
    w.u16(1)
    w.name("creatures")
    w.u16(1)
    w.chunk(declaration_bytes if declaration_bytes is not None else declaration(),
            declaration_padding)
    w.chunk(link_bytes if link_bytes is not None else link())
    w.u32(trailer)
    w.data += extra
    return bytes(w.data)


class ParsePackageTest(unittest.TestCase):
    def test_parse_minimal_package(self):
        package = bpackage.parse(build_package(), SYMBOLS)
        self.assertEqual(package["version"], 1)
        self.assertEqual(package["imports"], ["creatures"])
        self.assertEqual(len(package["classes"]), 1)
        cls = package["classes"][0]
        self.assertEqual(cls["name"], "critter")
        self.assertEqual(cls["kind"], 0)
        self.assertEqual(cls["data_size"], 41)
        self.assertEqual(cls["slot_count"], 16)
        self.assertEqual(cls["states"], ["default", "idlestate"])
        self.assertEqual(cls["parent"], "monsterscript")
        self.assertEqual(cls["state_owners"], [None, "monsterscript"])
        self.assertEqual(cls["enums"], [
            {"name": "sense", "word": 0, "values": [("sight", 0), ("enemy", 1)]},
        ])
        self.assertEqual(cls["fields"], [{
            "name": "health",
            "flags": 1,
            "offset": 4,
            "type": (3,),
            "default": {"flag": 1, "value": 0x3F800000},
        }])
        self.assertEqual(cls["methods"][0], {
            "name": "start",
            "byte4": 0,
            "byte5": 0,
            "slot": 0,
            "result": (0,),
            "params": [],
        })
        self.assertEqual(cls["methods"][1], {
            "name": "takepain",
            "byte4": 1,
            "byte5": 2,
            "slot": 3,
            "result": (2,),
            "params": [((2,), 8)],
        })
        self.assertEqual(cls["bodies"][0], {
            "method": 0,
            "flags": 0,
            "native": False,
            "state": 0,
            "locals_size": 0,
            "stack_size": 0,
            "locals": [],
            "code": [0x04000000, 0x21000001, 0x5C000000],
        })
        self.assertEqual(cls["bodies"][1], {
            "method": 1,
            "flags": 1,
            "native": True,
            "state": 0,
            "locals_size": 0,
            "stack_size": 0,
            "locals": [],
            "code": [],
        })
        self.assertEqual(cls["slots"], [{"state": 0, "slot": 0, "class": "critter", "body": 0}])
        self.assertEqual(cls["references"][0], [
            {"kind": 0, "word": 0, "name": "message"},
            {"kind": 2, "word": 1, "name": "creatures"},
            {"kind": 4, "word": 2, "name": "critter", "index": 1},
            {"kind": 5, "word": 2, "name": "monsterscript", "second": "critter", "index": 0},
        ])

    def test_alignment(self):
        data = bytes([0xAA, 0x00]) + struct.pack("<H", 0x1234) + struct.pack("<I", 0x12345678)
        reader = bpackage.Reader(data)
        self.assertEqual(reader.u8(), 0xAA)
        self.assertEqual(reader.u16(), 0x1234)
        self.assertEqual(reader.u32(), 0x12345678)

    def test_chunk_padding(self):
        bpackage.parse(build_package(declaration_padding=2), SYMBOLS)
        with self.assertRaises(PackageError):
            bpackage.parse(build_package(declaration_padding=4), SYMBOLS)

    def test_field_defaults(self):
        w = Writer()
        w.name("critter")
        w.u16(0)
        w.u16(0)
        w.u16(0)
        w.u16(2)
        w.u16(0)
        w.u16(5)
        w.u16(0)
        w.u16(0)
        w.name("default")
        w.name("idlestate")
        for name, type_, default in (
            ("message", (4,), lambda: (w.u16(1), w.name("message"))),
            ("enemy", (4,), lambda: w.u16(0)),
            ("sense", (6, (2,), 4), lambda: w.u16(0)),
            ("object", (9, "critter"), lambda: (w.u16(1), w.chunk(b"\x01\x02\x03"))),
            ("sight", (9, "critter"), lambda: w.u16(0)),
        ):
            w.name(name)
            w.u16(0)
            w.u32(0)
            w.type(type_)
            default()
        empty_link = Writer()
        empty_link.u16(0)
        empty_link.u16(0)
        empty_link.u16(0)
        empty_link.u16(0xFFFF)
        empty_link.u16(0xFFFF)
        package = bpackage.parse(
            build_package(declaration_bytes=bytes(w.data), link_bytes=bytes(empty_link.data)),
            SYMBOLS,
        )
        fields = package["classes"][0]["fields"]
        self.assertEqual(fields[0]["default"], {"flag": 1, "name": "message"})
        self.assertEqual(fields[1]["default"], {"flag": 0})
        self.assertEqual(fields[2]["default"], {"flag": 0})
        self.assertEqual(fields[2]["type"], (6, (2,), 4))
        self.assertEqual(fields[3]["default"], {"flag": 1, "object": b"\x01\x02\x03"})
        self.assertEqual(fields[4]["default"], {"flag": 0})

    def test_rejects_version(self):
        with self.assertRaises(PackageError):
            bpackage.parse(build_package(version=2), SYMBOLS)

    def test_rejects_name_index(self):
        with self.assertRaises(PackageError):
            bpackage.parse(build_package(), [])

    def test_rejects_chunk_outside_the_data(self):
        with self.assertRaises(PackageError):
            bpackage.parse(build_package()[:-8], SYMBOLS)

    def test_rejects_reference_kind(self):
        with self.assertRaises(PackageError):
            bpackage.parse(build_package(link_bytes=link(reference_kind=6)), SYMBOLS)

    def test_rejects_body_method(self):
        with self.assertRaises(PackageError):
            bpackage.parse(build_package(declaration_bytes=declaration(body_method=2)), SYMBOLS)

    def test_rejects_body_state(self):
        with self.assertRaises(PackageError):
            bpackage.parse(build_package(declaration_bytes=declaration(body_state=2)), SYMBOLS)

    def test_rejects_method_slot(self):
        with self.assertRaises(PackageError):
            bpackage.parse(build_package(declaration_bytes=declaration(method_slot=16)), SYMBOLS)

    def test_rejects_slot_state(self):
        with self.assertRaises(PackageError):
            bpackage.parse(build_package(link_bytes=link(slot_state=2)), SYMBOLS)

    def test_rejects_slot_out_of_range(self):
        with self.assertRaises(PackageError):
            bpackage.parse(build_package(link_bytes=link(slot_slot=16)), SYMBOLS)

    def test_rejects_reference_body(self):
        with self.assertRaises(PackageError):
            bpackage.parse(build_package(link_bytes=link(reference_body=2)), SYMBOLS)

    def test_rejects_reference_word(self):
        with self.assertRaises(PackageError):
            bpackage.parse(build_package(link_bytes=link(reference_word=3)), SYMBOLS)

    def test_rejects_truncated_code(self):
        with self.assertRaises(PackageError):
            bpackage.parse(
                build_package(declaration_bytes=declaration(omit_code_count=True)),
                SYMBOLS,
            )

    def test_rejects_trailer(self):
        with self.assertRaises(PackageError):
            bpackage.parse(build_package(trailer=1), SYMBOLS)

    def test_rejects_bytes_after_the_trailer(self):
        with self.assertRaises(PackageError):
            bpackage.parse(build_package(extra=b"\x00"), SYMBOLS)


class DisassembleTest(unittest.TestCase):
    @staticmethod
    def synthetic_class(code, references=()):
        return {
            "name": "critter",
            "states": ["default", "idlestate"],
            "methods": [{"name": "start"}, {"name": "chained"}],
            "bodies": [{
                "method": 0,
                "flags": 0,
                "native": False,
                "state": 0,
                "locals_size": 2,
                "stack_size": 4,
                "locals": [],
                "code": code,
            }],
            "references": {0: list(references)},
        }

    def test_instruction_words(self):
        self.assertEqual(len(bpackage.MNEMONICS), 97)
        for op in range(len(bpackage.MNEMONICS)):
            expected = 3 if op in bpackage.THREE_WORD else 2 if op in bpackage.TWO_WORD else 1
            self.assertEqual(bpackage.instruction_words(op << 24), expected, hex(op))
        with self.assertRaises(PackageError):
            bpackage.instruction_words(0x61 << 24)

    def test_instructions_split(self):
        code = [0x02000000, 0xDEADBEEF, 0x03FFFFFF, 0x49000000, 0x00000001, 0x00000002,
                0x5C000000]
        parsed = list(bpackage.instructions(code))
        self.assertEqual([instruction["index"] for instruction in parsed], [0, 2, 3, 6])
        self.assertEqual([instruction["op"] for instruction in parsed], [0x02, 0x03, 0x49, 0x5C])
        self.assertEqual(parsed[0]["words"], [0xDEADBEEF])
        self.assertEqual(parsed[1]["simm"], -1)
        self.assertEqual(parsed[2]["words"], [1, 2])

    def test_instructions_reject_truncated(self):
        with self.assertRaises(PackageError):
            list(bpackage.instructions([0x02000000]))
        with self.assertRaises(PackageError):
            list(bpackage.instructions([0x61000000]))

    def test_disassemble_operands(self):
        code = [
            0x00000007,
            0x03FFFFFD,
            0x02000000, 0xDEADBEEF,
            0x04000000,
            0x45000002,
            0x48000000, 0x00000000,
            0x4A000001, 0x00000000,
            0x49000000, 0x00000000, 0x00000000,
            0x5F000000,
            0x5C000000,
        ]
        references = [
            {"kind": 0, "word": 4, "name": "message"},
            {"kind": 4, "word": 7, "name": "critter", "index": 1},
            {"kind": 3, "word": 9, "name": "monsterscript", "index": 2},
            {"kind": 5, "word": 11, "name": "monsterscript", "second": "critter", "index": 0},
        ]
        cls = self.synthetic_class(code, references)
        self.assertEqual(bpackage.disassemble(cls, 0), [
            "0000  mark 7",
            "0001  push_int -3",
            "0002  push_word 0xdeadbeef",
            "0004  push_name message",
            "0005  jump -> 0008",
            "0006  call critter body 1",
            "0008  call_virtual monsterscript method 2 default",
            "0010  call_table tablas monsterscript method 0",
            "0013  cast ?",
            "0014  return",
        ])


class NativesTest(unittest.TestCase):
    def test_type_name(self):
        cases = [
            ((0,), "void"),
            ((1,), "bool"),
            ((2,), "int"),
            ((3,), "float"),
            ((4,), "string"),
            ((5,), "byte"),
            ((6, (2,), 4), "int[4]"),
            ((7, (1,)), "bool&"),
            ((9, "critter"), "critter"),
            ((10, "critter"), "critter ref"),
            ((11, "sense"), "enum sense"),
        ]
        for type_, expected in cases:
            self.assertEqual(bpackage.type_name(type_), expected)

    def test_natives_minimal(self):
        package = bpackage.parse(build_package(), SYMBOLS)
        self.assertEqual(bpackage.natives(package), [{
            "class": "critter",
            "method": "takepain",
            "state": "default",
            "slot": 3,
            "thread": True,
            "result": "int",
            "params": ["int"],
        }])


_EXPECTED_OPCODES = {
    0x00: 666607, 0x01: 13, 0x02: 37908, 0x03: 359168, 0x04: 30193,
    0x05: 4683, 0x06: 55739, 0x07: 284418, 0x08: 58063, 0x09: 24765,
    0x0A: 112818, 0x0B: 260237, 0x0C: 10854, 0x0E: 3456, 0x0F: 162,
    0x1C: 19114, 0x1D: 54457, 0x1E: 8605, 0x1F: 324, 0x21: 9084,
    0x27: 2430, 0x28: 1652, 0x29: 3933, 0x2D: 4918, 0x2E: 4821,
    0x2F: 14627, 0x30: 6686, 0x31: 6362, 0x32: 648, 0x33: 324,
    0x34: 1296, 0x35: 3570, 0x36: 162, 0x38: 7776, 0x39: 5850,
    0x3A: 460, 0x3B: 1270, 0x3C: 324, 0x3E: 229773, 0x45: 12652,
    0x46: 58222, 0x47: 170456, 0x48: 204696, 0x49: 649, 0x4A: 17508,
    0x4B: 2465, 0x4C: 12858, 0x4D: 6881, 0x4E: 2125, 0x4F: 17615,
    0x50: 26930, 0x51: 492, 0x52: 162, 0x55: 2592, 0x56: 4374,
    0x59: 4348, 0x5A: 2054, 0x5B: 6402, 0x5C: 125505, 0x5D: 2747,
    0x5E: 41045, 0x5F: 42306, 0x60: 3609,
}

_PACKAGES = None
_SUMMARY = None


def game_packages():
    global _PACKAGES
    if _PACKAGES is None:
        root = Path(os.environ["BO2NX_ASSETS_DIR"]) / "media/gogames/bo2"
        _PACKAGES = []
        for path in sorted(root.rglob("*.big")):
            _PACKAGES.extend(bpackage.load_packages(path))
    return _PACKAGES


def game_summary():
    global _SUMMARY
    if _SUMMARY is not None:
        return _SUMMARY
    opcodes = {}
    total = 0
    jumps = 0
    bad_jumps = []
    coded_bodies = 0
    bad_returns = []
    reference_counts = [0] * 6
    bad_references = []
    reference_opcodes = {
        0: {0x04}, 1: {0x5F}, 2: {0x21},
        3: {0x4A, 0x4C, 0x50, 0x52, 0x56},
        4: {0x47, 0x48, 0x4B, 0x4D, 0x4E, 0x51},
        5: {0x49, 0x4F, 0x55},
    }
    for package_name, package in game_packages():
        for cls in package["classes"]:
            for body_index, body in enumerate(cls["bodies"]):
                code = body["code"]
                if not code:
                    continue
                coded_bodies += 1
                if code[-1] >> 24 != 0x5C:
                    bad_returns.append((package_name, cls["name"], body_index))
                parsed = list(bpackage.instructions(code))
                starts = {instruction["index"] for instruction in parsed}
                for instruction in parsed:
                    op = instruction["op"]
                    opcodes[op] = opcodes.get(op, 0) + 1
                    total += 1
                    if op in bpackage.JUMPS:
                        jumps += 1
                        destination = (instruction["index"] + len(instruction["words"]) + 1
                                       + instruction["simm"])
                        if destination != len(code) and destination not in starts:
                            bad_jumps.append(
                                (package_name, cls["name"], body_index,
                                 instruction["index"], destination)
                            )
                for reference in cls["references"].get(body_index, []):
                    kind = reference["kind"]
                    reference_counts[kind] += 1
                    op = code[reference["word"] - (1 if kind >= 2 else 0)] >> 24
                    if op not in reference_opcodes[kind]:
                        bad_references.append((kind, op))
    _SUMMARY = {
        "opcodes": opcodes,
        "total": total,
        "jumps": jumps,
        "bad_jumps": bad_jumps,
        "coded_bodies": coded_bodies,
        "bad_returns": bad_returns,
        "reference_counts": reference_counts,
        "bad_references": bad_references,
    }
    return _SUMMARY



@unittest.skipUnless(os.environ.get("BO2NX_ASSETS_DIR"), "BO2NX_ASSETS_DIR is unset")
class GamePackageTest(unittest.TestCase):
    @staticmethod
    def root():
        return Path(os.environ["BO2NX_ASSETS_DIR"]) / "media/gogames/bo2"

    def test_sl01_packages(self):
        path = self.root() / "resources/levels/slums/sl01.big"
        packages = dict(bpackage.load_packages(path))
        self.assertEqual(sorted(packages), ["creatures", "native", "sounds"])
        self.assertEqual(bpackage.package_totals(packages["native"]), {
            "classes": 58, "methods": 588, "bodies": 556, "natives": 505,
            "words": 557, "fields": 55, "enums": 25,
        })
        self.assertEqual(packages["native"]["imports"], [])
        creatures = packages["creatures"]
        self.assertEqual(creatures["imports"], ["native"])
        self.assertEqual(bpackage.package_totals(creatures), {
            "classes": 340, "methods": 625, "bodies": 656, "natives": 0,
            "words": 20195, "fields": 1085, "enums": 1,
        })
        critter = creatures["classes"][0]
        self.assertEqual(critter["name"], "critter")
        self.assertEqual(critter["parent"], "monsterscript")
        self.assertEqual(critter["data_size"], 41)
        self.assertEqual(critter["slot_count"], 16)
        self.assertEqual(critter["states"], ["default", "idlestate"])
        sounds = packages["sounds"]
        self.assertEqual(sounds["imports"], ["native"])
        self.assertEqual(bpackage.package_totals(sounds), {
            "classes": 20, "methods": 47, "bodies": 41, "natives": 0,
            "words": 1598, "fields": 96, "enums": 0,
        })

    def test_all_packages(self):
        containers = sorted(self.root().rglob("*.big"))
        self.assertEqual(len(containers), 179)
        packages = 0
        names = {}
        totals = {"classes": 0, "methods": 0, "bodies": 0, "natives": 0,
                  "words": 0, "fields": 0, "enums": 0}
        for name, package in game_packages():
            packages += 1
            names[name] = names.get(name, 0) + 1
            for key, value in bpackage.package_totals(package).items():
                totals[key] += value
            for cls in package["classes"]:
                self.assertEqual(cls["states"][0], "default")
                self.assertEqual(cls["methods"][0]["name"], "start")
                self.assertEqual(cls["methods"][0]["slot"], 0)
        self.assertEqual(packages, 480)
        self.assertEqual(names, {"native": 168, "creatures": 162, "sounds": 149, "camera": 1})
        self.assertEqual(totals, {
            "classes": 67805, "methods": 207039, "bodies": 205790,
            "natives": 84840, "words": 3603894, "fields": 199314, "enums": 4362,
        })


@unittest.skipUnless(os.environ.get("BO2NX_ASSETS_DIR"), "BO2NX_ASSETS_DIR is unset")
class DisassemblyGameTest(unittest.TestCase):
    def test_opcode_counts(self):
        summary = game_summary()
        self.assertEqual(summary["total"], 3066243)
        self.assertEqual(summary["opcodes"], _EXPECTED_OPCODES)

    def test_jumps_land_on_instructions(self):
        summary = game_summary()
        self.assertEqual(summary["jumps"], 80885)
        self.assertEqual(summary["bad_jumps"], [])

    def test_bodies_end_with_return(self):
        summary = game_summary()
        self.assertEqual(summary["coded_bodies"], 120950)
        self.assertEqual(summary["bad_returns"], [])

    def test_references_match_opcodes(self):
        summary = game_summary()
        self.assertEqual(summary["reference_counts"], [30193, 42306, 9084, 61832, 387115, 20856])
        self.assertEqual(summary["bad_references"], [])


@unittest.skipUnless(os.environ.get("BO2NX_ASSETS_DIR"), "BO2NX_ASSETS_DIR is unset")
class NativesGameTest(unittest.TestCase):
    def test_native_interface(self):
        path = (Path(os.environ["BO2NX_ASSETS_DIR"])
                / "media/gogames/bo2/resources/levels/slums/sl01.big")
        packages = dict(bpackage.load_packages(path))
        native = bpackage.natives(packages["native"])
        self.assertEqual(len(native), 505)
        self.assertEqual(len({entry["class"] for entry in native}), 50)
        all_native = [package for name, package in game_packages() if name == "native"]
        self.assertEqual(len(all_native), 168)
        expected = bpackage.natives(all_native[0])
        for package in all_native[1:]:
            self.assertEqual(bpackage.natives(package), expected)


if __name__ == "__main__":
    unittest.main()
