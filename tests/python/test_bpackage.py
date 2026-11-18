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
        for path in containers:
            for name, package in bpackage.load_packages(path):
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


if __name__ == "__main__":
    unittest.main()
