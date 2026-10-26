import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import compare_frames as frames  # noqa: E402


def image(width, height, pixel):
    return frames.Image(width, height, bytes(pixel) * (width * height))


class CompareFramesTest(unittest.TestCase):
    def test_identical_frames(self):
        left = image(2, 2, (1, 2, 3))
        report = frames.compare(left, left)
        self.assertEqual(report["rmse"], 0.0)
        self.assertEqual(report["changed_pixels"], 0)

    def test_metrics_and_box(self):
        left = image(3, 2, (0, 0, 0))
        right = frames.Image(3, 2, bytes([0, 0, 0, 10, 20, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]))
        report = frames.compare(left, right)
        self.assertEqual(report["max_difference"], 30)
        self.assertEqual(report["changed_pixels"], 1)
        self.assertEqual(report["bounding_box"], [1, 0, 1, 0])

    def test_region_and_tiles(self):
        left = image(4, 4, (0, 0, 0))
        right = frames.Image(4, 4, bytes([10, 0, 0]) * 16)
        region = frames.compare(left, right, (1, 1, 2, 2))
        self.assertEqual(region["pixels"], 4)
        tiles = frames.tile_metrics(left, right, 2)
        self.assertEqual(len(tiles), 4)
        self.assertEqual(tiles[0]["tile"], [0, 0])

    def test_side_by_side_and_diff(self):
        left = image(2, 1, (0, 0, 0))
        right = image(2, 1, (10, 0, 0))
        side = frames.side_by_side(left, right)
        self.assertEqual(side.width, 6)
        diff = frames.difference(left, right, 2)
        self.assertEqual(diff.pixels[0], 20)

    def test_cli_writes_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            left = root / "left.ppm"
            right = root / "right.ppm"
            frames.write_ppm(left, image(2, 2, (1, 2, 3)))
            frames.write_ppm(right, image(2, 2, (1, 2, 4)))
            output = root / "gate"
            with contextlib.redirect_stdout(io.StringIO()):
                sys.argv = [
                    "compare_frames.py", str(left), str(right), "--out", str(output),
                    "--tiles", "2", "--region", "all:0,0,2,2",
                ]
                self.assertEqual(frames.main(), 0)
            report = json.loads(Path(f"{output}-metrics.json").read_text())
            self.assertEqual(report["whole"]["changed_pixels"], 4)
            self.assertEqual(len(report["tiles"]), 4)
            self.assertIn("all", report["regions"])
            self.assertTrue(Path(f"{output}-side-by-side.ppm").is_file())
            self.assertTrue(Path(f"{output}-diff.ppm").is_file())


if __name__ == "__main__":
    unittest.main()
