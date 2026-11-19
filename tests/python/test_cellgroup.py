import os
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import big  # noqa: E402
import cellgroup  # noqa: E402
from cellgroup import CellGroupError  # noqa: E402

SYMBOLS = ["default", "RenderPriority", "FogColor", "name_one", "name_two", "texture_one"]


class Writer:
    def __init__(self):
        self.data = bytearray()

    def u8(self, value):
        self.data.append(value)

    def u16(self, value):
        self.data += struct.pack("<H", value)

    def u32(self, value):
        self.data += struct.pack("<I", value)

    def f32(self, value):
        self.data += struct.pack("<f", value)


def chunk(name, payload):
    out = bytearray(name.encode("latin1").ljust(8, b"\0"))
    out += struct.pack("<II", 16 + len(payload), 0)
    out += payload
    return bytes(out)


def model_block(name, chunks):
    out = bytearray()
    out += struct.pack("<I", 105)
    out += name.encode("latin1") + b"\0"
    out += struct.pack("<I", 16 + sum(len(item) for item in chunks))
    out += struct.pack("<I", 0)
    for item in chunks:
        out += item
    return bytes(out)


def cell_group(models, tail=()):
    out = bytearray(8)
    payload = bytearray(struct.pack("<I", len(models)))
    for model in models:
        payload += model
    out += chunk("cellmdls", payload)
    for item in tail:
        out += item
    return bytes(out)


def attribute_table(records):
    out = bytearray(b"default\0")
    body = bytearray()
    for record in records:
        body += record
    out += struct.pack("<III", 20 + len(body), 1, 2)
    out += body
    return bytes(out)


class ModelBlockTest(unittest.TestCase):
    def test_model_blocks(self):
        rmd = struct.pack("<I", 2) + b"default\0" + struct.pack("<II", 16, 0)
        data = cell_group([
            model_block("default", [chunk("rmd", rmd),
                                    chunk("mdlattr", attribute_table([]))]),
            model_block("another", [chunk("collmodc", b"\x01\x02\x03\x04")]),
        ])
        blocks = cellgroup.model_blocks(data)
        self.assertEqual(len(blocks), 2)
        self.assertEqual(blocks[0]["index"], 0)
        self.assertEqual(blocks[0]["name"], "default")
        self.assertIn("rmd", blocks[0]["chunks"])
        self.assertIn("mdlattr", blocks[0]["chunks"])
        self.assertEqual(blocks[1]["name"], "another")
        self.assertEqual(blocks[1]["chunks"]["collmodc"], b"\x01\x02\x03\x04")

    def test_model_blocks_without_models(self):
        data = bytearray(8)
        data += chunk("cellmark", struct.pack("<I", 0))
        self.assertEqual(cellgroup.model_blocks(bytes(data)), [])

    def test_rmd_chunks(self):
        subchunks = chunk("texture", struct.pack("<II", 0, 0))
        record = b"default\0" + struct.pack("<II", 16 + len(subchunks), 0) + subchunks
        rmd = struct.pack("<I", 2) + record
        block = {"chunks": {"rmd": rmd}}
        self.assertIn("texture", cellgroup.rmd_chunks(block))

    def test_tail_chunks(self):
        data = cell_group([], [chunk("cellmark", struct.pack("<I", 0)),
                               chunk("attrs", b"abcd")])
        tail = cellgroup.tail_chunks(data)
        self.assertEqual(sorted(tail), ["attrs", "cellmark"])


class AttributeTest(unittest.TestCase):
    def test_attributes(self):
        scalar_float = struct.pack("<BBHf", 0, 0, 1, 40.0)
        scalar_byte = struct.pack("<BBH", 3, 0, 3) + bytes([7, 0xFF, 0xFF, 0xFF])
        scalar_symbol = struct.pack("<BBH", 7, 0, 3) + struct.pack("<HH", 4, 0xFFFF)
        array_byte = (struct.pack("<BBHI", 3, 1, 2, 3) + bytes([10, 20, 30])
                      + b"\0")
        table = attribute_table([scalar_float, scalar_byte, scalar_symbol, array_byte])
        records = cellgroup.attributes(table, SYMBOLS)
        self.assertEqual(records[0], {"name": "RenderPriority", "type": 0, "array": False,
                                      "value": 40.0})
        self.assertEqual(records[1]["name"], "name_one")
        self.assertEqual(records[1]["value"], 7)
        self.assertEqual(records[2]["value"], "name_two")
        self.assertEqual(records[3]["name"], "FogColor")
        self.assertTrue(records[3]["array"])
        self.assertEqual(records[3]["value"], [10, 20, 30])

    def test_rejects_bad_tables(self):
        with self.assertRaises(CellGroupError):
            cellgroup.attributes(b"default\0", SYMBOLS)
        table = attribute_table([struct.pack("<BBH", 5, 0, 1) + struct.pack("<I", 0)])
        with self.assertRaises(CellGroupError):
            cellgroup.attributes(table, SYMBOLS)


class TextureTest(unittest.TestCase):
    def test_material_textures(self):
        chunk_data = bytearray(struct.pack("<II", 2, 2))
        chunk_data += struct.pack("<I", 3)
        for index in (3, 5, 3):
            chunk_data += struct.pack("<H", index)
        chunk_data += struct.pack("<H", 0xFFFF)
        chunk_data += struct.pack("<I", 0)
        materials = cellgroup.material_textures(bytes(chunk_data), SYMBOLS)
        self.assertEqual(materials, {2: ["name_one", "texture_one", "name_one"], 3: []})

    def test_material_controls(self):
        controls = struct.pack("<I", 2) + struct.pack("<III", 1, 2, 3) + struct.pack("<III", 4, 5, 6)
        self.assertEqual(cellgroup.material_controls(controls), [(1, 2, 3), (4, 5, 6)])
        with self.assertRaises(CellGroupError):
            cellgroup.material_controls(controls[:-4])

    def test_dds_format(self):
        data = bytearray(128)
        data[:4] = b"DDS "
        data[84:88] = b"DXT3"
        self.assertEqual(cellgroup.dds_format(bytes(data)), "DXT3")
        data[84:88] = b"\0\0\0\0"
        self.assertEqual(cellgroup.dds_format(bytes(data)), "RGBA")


class MarkerTest(unittest.TestCase):
    def test_markers(self):
        record = bytearray(struct.pack("<IiiII", 4, 2, 1, 3, 4))
        record += struct.pack("<3f", 1.0, 2.0, 3.0)
        record += struct.pack("<3f", 0.0, 0.0, 1.0)
        record += struct.pack("<IIf", 1, 7, 0.5)
        payload = struct.pack("<I", 1) + bytes(record)
        data = cell_group([], [chunk("cellmark", payload)])
        markers = cellgroup.markers(data, SYMBOLS)
        self.assertEqual(len(markers), 1)
        marker = markers[0]
        self.assertEqual(marker["model"], 2)
        self.assertEqual(marker["clip"], 1)
        self.assertEqual(marker["name"], "name_one")
        self.assertEqual(marker["class"], "name_two")
        self.assertEqual(marker["position"], [1.0, 2.0, 3.0])
        self.assertEqual(marker["normal"], [0.0, 0.0, 1.0])
        self.assertEqual(marker["flag"], 1)
        self.assertEqual(marker["value"], 7)
        self.assertAlmostEqual(marker["number"], 0.5)

    def test_rejects_a_bad_version(self):
        record = bytearray(struct.pack("<IiiII", 3, 0, -1, 3, 4))
        record += b"\0" * 36
        payload = struct.pack("<I", 1) + bytes(record)
        with self.assertRaises(CellGroupError):
            cellgroup.markers(cell_group([], [chunk("cellmark", payload)]), SYMBOLS)


@unittest.skipUnless(os.environ.get("BO2NX_ASSETS_DIR"), "BO2NX_ASSETS_DIR is unset")
class GameCellGroupTest(unittest.TestCase):
    @staticmethod
    def root():
        return Path(os.environ["BO2NX_ASSETS_DIR"]) / "media/gogames/bo2"

    def test_ui_hud_models(self):
        entry, symbols = cellgroup.load_cell_group(self.root() / "code.big", "ui/hud")
        blocks = cellgroup.model_blocks(entry.data)
        self.assertEqual(len(blocks), 42)
        markers = {marker["name"]: marker for marker in
                   cellgroup.markers(entry.data, symbols)}
        icon = markers["ui_hud_darkgift_icon"]
        self.assertEqual(icon["model"], 7)
        block = blocks[icon["model"]]
        self.assertAlmostEqual(cellgroup.model_priority(block, symbols), 70.0)
        subchunks = cellgroup.rmd_chunks(block)
        materials = cellgroup.material_textures(subchunks["texture"], symbols)
        self.assertEqual(sorted(materials), [0, 1])
        self.assertEqual(len(materials[0]), 9)
        self.assertEqual(materials[0][0], "ui/hud/icon_none")
        self.assertEqual(materials[0][-1], "ui/hud/icon_slord")
        self.assertEqual(materials[1], [])
        main = blocks[markers["_main"]["model"]]
        self.assertAlmostEqual(cellgroup.model_priority(main, symbols), 60.0)
        main_textures = cellgroup.material_textures(
            cellgroup.rmd_chunks(main)["texture"], symbols)
        self.assertEqual(main_textures[0], ["ui/hud/main3"])
        glass = blocks[markers["_glass"]["model"]]
        self.assertAlmostEqual(cellgroup.model_priority(glass, symbols), 80.0)
        glass_textures = cellgroup.material_textures(
            cellgroup.rmd_chunks(glass)["texture"], symbols)
        self.assertEqual(glass_textures[0], ["ui/hud/glass"])

    def test_sl01_models(self):
        entry, symbols = cellgroup.load_cell_group(
            self.root() / "resources/levels/slums/sl01.big", "slums/sl01")
        blocks = cellgroup.model_blocks(entry.data)
        self.assertEqual(len(blocks), 664)
        self.assertEqual(sum(1 for block in blocks if "rmd" in block["chunks"]), 608)
        records = 0
        textures = 0
        for block in blocks:
            subchunks = cellgroup.rmd_chunks(block)
            if "texture" not in subchunks:
                continue
            materials = cellgroup.material_textures(subchunks["texture"], symbols)
            records += len(materials)
            textures += sum(len(listed) for listed in materials.values())
        self.assertEqual(records, 1084)
        self.assertEqual(textures, 1112)


if __name__ == "__main__":
    unittest.main()
