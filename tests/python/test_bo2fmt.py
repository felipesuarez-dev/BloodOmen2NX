import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import big  # noqa: E402
import bo2fmt  # noqa: E402


class Bo2fmtTest(unittest.TestCase):
    def test_big_unpack_repack_round_trip(self):
        entries = [
            big.Entry("model", "a/b", 0, 3, 0, b"abc"),
            big.Entry("texture", "c", 0, 5, 0, b"DDSxx", (7, 8)),
        ]
        data = big.build(["a/b", "c", "unused"], entries, name_padding=b"\xff")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.big"
            source.write_bytes(data)
            unpacked = root / "unpacked"
            packed = root / "output.big"
            with contextlib.redirect_stdout(io.StringIO()):
                bo2fmt.unpack_big(source, unpacked)
            manifest = json.loads((unpacked / bo2fmt.MANIFEST).read_text())
            self.assertEqual(manifest["format"], "bo2.big")
            self.assertEqual(manifest["name_padding"], "ff")
            self.assertEqual(len(manifest["entries"]), 2)
            with contextlib.redirect_stdout(io.StringIO()):
                bo2fmt.repack_big(unpacked, packed)
            self.assertEqual(packed.read_bytes(), data)

    def test_big_unpack_preserves_symbols(self):
        entries = [big.Entry("texture", "c", 3, 4, 0, b"DDS!", (9, 10, 11))]
        data = big.build(["c"], entries)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.big"
            source.write_bytes(data)
            unpacked = root / "unpacked"
            with contextlib.redirect_stdout(io.StringIO()):
                bo2fmt.unpack_big(source, unpacked)
            manifest = json.loads((unpacked / bo2fmt.MANIFEST).read_text())
            self.assertEqual(manifest["entries"][0]["symbols"], [9, 10, 11])
            self.assertEqual(manifest["entries"][0]["language"], 3)

    def test_detect(self):
        self.assertEqual(bo2fmt.detect(b"goefile\0" + b"\0" * 32), "big")
        self.assertEqual(bo2fmt.detect(b"\1\0\0\0" + b"\0" * 32), "rkv")
        self.assertEqual(bo2fmt.detect(b"nope"), "unknown")


if __name__ == "__main__":
    unittest.main()
