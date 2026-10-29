import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import big  # noqa: E402
import textures  # noqa: E402

try:
    import PIL  # noqa: F401
except ImportError:
    PIL = None


def rgba_dds(width, height, pixel=b"\x10\x20\x30\xff"):
    header = bytearray(128)
    header[0:4] = b"DDS "
    struct.pack_into("<IIIII", header, 4, 124, 0x100F, height, width, width * 4)
    struct.pack_into("<I", header, 28, 1)
    # DDPF_RGB | DDPF_ALPHAPIXELS, 32 bits, BGRA masks
    struct.pack_into("<IIIIIIII", header, 76, 32, 0x41, 0, 32,
                     0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    struct.pack_into("<I", header, 108, 0x1000)
    return bytes(header) + pixel * (width * height)


def container(entries):
    return big.build([name for _, name, _ in entries],
                     [big.Entry(kind, name, 0, 0, 0, data) for kind, name, data in entries])


class TexturesTest(unittest.TestCase):
    def test_dds_info(self):
        info = textures.dds_info(rgba_dds(4, 2))
        self.assertEqual(info, {"width": 4, "height": 2, "mips": 1, "format": "rgb32"})

    def test_dds_info_rejects_other_payloads(self):
        with self.assertRaises(textures.TextureError):
            textures.dds_info(b"\0" * 128)

    def test_safe_target_rejects_traversal(self):
        out = Path("/tmp/out")
        self.assertIsNone(textures.safe_target(out, "../escape"))
        self.assertEqual(textures.safe_target(out, "Hud/curser"), out / "Hud" / "curser.png")

    def test_safe_target_rejects_empty_name(self):
        self.assertIsNone(textures.safe_target(Path("/tmp/out"), ""))

    def test_safe_target_suffixes_duplicates(self):
        out = Path("/tmp/out")
        used = set()
        first = textures.safe_target(out, "Hud/curser", used)
        second = textures.safe_target(out, "Hud/curser", used)
        third = textures.safe_target(out, "hud/CURSER", used)
        self.assertEqual(first, out / "Hud" / "curser.png")
        self.assertEqual(second, out / "Hud" / "curser~1.png")
        self.assertEqual(third, out / "hud" / "CURSER~2.png")

    def test_texture_entries_filters_type_and_name(self):
        data = container([
            ("texture", "Hud/curser", rgba_dds(2, 2)),
            ("texture", "walls/stone", rgba_dds(2, 2)),
            ("sound", "Hud/click", b"x"),
        ])
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "level.big"
            source.write_bytes(data)
            names = [e.name for e in textures.texture_entries(source, "hud")]
        self.assertEqual(names, ["Hud/curser"])

    @unittest.skipIf(PIL is None, "Pillow is not installed")
    def test_export_writes_png_and_index(self):
        data = container([("texture", "Hud/curser", rgba_dds(4, 4))])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "level.big"
            source.write_bytes(data)
            out = root / "out"
            args = type("Args", (), {"container": str(source), "out": str(out), "match": ""})
            self.assertEqual(textures.cmd_export(args), 0)
            png = out / "Hud" / "curser.png"
            self.assertEqual(png.read_bytes()[:8], b"\x89PNG\r\n\x1a\n")
            index = json.loads((out / "index.json").read_text())
            self.assertEqual(len(index), 1)
            self.assertEqual(index[0]["png"], "Hud/curser.png")
            self.assertEqual(index[0]["format"], "rgb32")
            from PIL import Image
            with Image.open(png) as image:
                self.assertEqual(image.getpixel((0, 0)), (0x30, 0x20, 0x10, 0xFF))

    @unittest.skipIf(PIL is None, "Pillow is not installed")
    def test_export_keeps_duplicate_names(self):
        data = container([
            ("texture", "Hud/curser", rgba_dds(2, 2, b"\x00\x00\xff\xff")),
            ("texture", "Hud/curser", rgba_dds(2, 2, b"\x00\xff\x00\xff")),
        ])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "level.big"
            source.write_bytes(data)
            out = root / "out"
            args = type("Args", (), {"container": str(source), "out": str(out), "match": ""})
            self.assertEqual(textures.cmd_export(args), 0)
            index = json.loads((out / "index.json").read_text())
            self.assertEqual([r["png"] for r in index], ["Hud/curser.png", "Hud/curser~1.png"])
            from PIL import Image
            with Image.open(out / "Hud" / "curser~1.png") as image:
                self.assertEqual(image.getpixel((0, 0)), (0x00, 0xFF, 0x00, 0xFF))


if __name__ == "__main__":
    unittest.main()
