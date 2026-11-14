import os
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import big  # noqa: E402
import font  # noqa: E402
from big import Entry  # noqa: E402

NAMES = ["default", "fonts/revised"]


def make_font(upper_case_only, glyphs):
    body = b"".join(
        bytes([code]) + b"\xff\xff\xff" + struct.pack("<4f", x, y, width, height)
        for code, x, y, width, height in glyphs
    )
    data = (
        b"texfont\0"
        + struct.pack("<II", 40 + len(body), 0)
        + struct.pack("<H", 0)
        + b"\xff\xff"
        + struct.pack("<4f", 20.0, 2.0, 5.0, 8.0)
        + bytes([1 if upper_case_only else 0, 0xFF])
        + struct.pack("<H", len(glyphs))
        + body
    )
    return Entry("texfont", "main/main", 0, len(data), 0, data, (1,))


class ParseTexfontTest(unittest.TestCase):
    def test_reads_the_header_and_the_glyphs(self):
        entry = make_font(False, [(ord("a"), 0, 0, 12, 20), (ord("A"), 105, 105, 12, 20),
                                  (ord("S"), 63, 63, 10, 20)])
        parsed = font.parse_texfont(NAMES, entry)
        self.assertEqual(parsed["texture"], "fonts/revised")
        self.assertEqual(parsed["header"], (20.0, 2.0, 5.0, 8.0))
        self.assertFalse(parsed["upper_case_only"])
        self.assertEqual(len(parsed["glyphs"]), 3)
        self.assertEqual(parsed["glyphs"][0], (ord("a"), 0.0, 0.0, 12.0, 20.0))
        self.assertEqual(parsed["glyphs"][1], (ord("A"), 105.0, 105.0, 12.0, 20.0))

    def test_upper_case_only_adds_the_missing_lower_case(self):
        entry = make_font(True, [(ord("A"), 105, 105, 12, 20)])
        parsed = font.parse_texfont(NAMES, entry)
        self.assertTrue(parsed["upper_case_only"])
        self.assertEqual(len(parsed["glyphs"]), 2)
        self.assertEqual(parsed["glyphs"][1], (ord("a"), 105.0, 105.0, 12.0, 20.0))

    def test_upper_case_only_keeps_a_lower_case_of_its_own(self):
        entry = make_font(True, [(ord("A"), 105, 105, 12, 20), (ord("a"), 1, 2, 3, 4)])
        parsed = font.parse_texfont(NAMES, entry)
        self.assertEqual(len(parsed["glyphs"]), 2)
        self.assertEqual(parsed["glyphs"][1], (ord("a"), 1.0, 2.0, 3.0, 4.0))

    def test_rejects_a_truncated_entry(self):
        entry = make_font(False, [(ord("a"), 0, 0, 12, 20)])
        entry.data = entry.data[:-1]
        with self.assertRaises(font.FontError):
            font.parse_texfont(NAMES, entry)

    def test_rejects_a_bad_size(self):
        entry = make_font(False, [])
        data = bytearray(entry.data)
        struct.pack_into("<I", data, 8, 12)
        entry.data = bytes(data)
        with self.assertRaises(font.FontError):
            font.parse_texfont(NAMES, entry)


@unittest.skipUnless(os.environ.get("BO2NX_ASSETS_DIR"), "BO2NX_ASSETS_DIR is unset")
class GameFontTest(unittest.TestCase):
    def test_game_font(self):
        path = (Path(os.environ["BO2NX_ASSETS_DIR"]) / "media/gogames/bo2/code.big")
        names, entries = big.load(path)
        entry = next(
            candidate for candidate in entries
            if candidate.type == "texfont" and candidate.name == "main/main"
        )
        self.assertEqual(len(entry.data), 1780)
        parsed = font.parse_texfont(names, entry)
        self.assertEqual(parsed["texture"], "fonts/revised")
        self.assertEqual(parsed["header"], (20.0, 2.0, 5.0, 8.0))
        self.assertFalse(parsed["upper_case_only"])
        self.assertEqual(len(parsed["glyphs"]), 87)
        rectangles = {code: (x, y, width, height)
                      for code, x, y, width, height in parsed["glyphs"]}
        self.assertEqual(rectangles[ord("a")], (0.0, 0.0, 12.0, 20.0))
        self.assertEqual(rectangles[ord("A")], (105.0, 105.0, 12.0, 20.0))
        self.assertEqual(rectangles[ord("T")], (231.0, 126.0, 12.0, 20.0))
        self.assertEqual(rectangles[ord("0")], (42.0, 42.0, 13.0, 20.0))
        self.assertEqual(rectangles[0xC8], (105.0, 84.0, 20.0, 20.0))
        self.assertEqual(rectangles[ord("S")], (63.0, 63.0, 10.0, 20.0))
        self.assertEqual(rectangles[ord("?")], (63.0, 63.0, 10.0, 20.0))
        self.assertNotIn(ord(" "), rectangles)


if __name__ == "__main__":
    unittest.main()
