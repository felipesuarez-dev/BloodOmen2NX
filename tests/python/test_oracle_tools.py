import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "oracle"))

import align  # type: ignore  # noqa: E402
import oracle_client  # type: ignore  # noqa: E402
import validate_capture  # type: ignore  # noqa: E402


def write_ppm(path: Path, width: int = 2, height: int = 2) -> None:
    header = f"P6\n{width} {height}\n255\n".encode("ascii")
    path.write_bytes(header + b"\x00" * (width * height * 3))


def write_sums(root: Path) -> None:
    lines = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.name == "SHA256SUMS":
            continue
        relative = path.relative_to(root).as_posix()
        lines.append(f"{validate_capture.sha256_file(path)}  {relative}")
    (root / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_capture(root: Path, with_state: bool = True) -> None:
    manifest = {
        "bo2_sha256": validate_capture.REFERENCE_BO2_SHA256,
        "resolution": [800, 600],
        "patch_applied": False,
    }
    (root / "game_manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    (root / "notes.md").write_text("route\n", encoding="utf-8")
    (root / "inputs.jsonl").write_text(
        "\n".join(
            [
                json.dumps({"epoch_ms": 1000, "event": "phase", "key": "menu"}),
                json.dumps({"epoch_ms": 1015, "event": "down", "key": "w"}),
                json.dumps({"epoch_ms": 1021, "event": "up", "key": "w"}),
            ]
        )
        + "\n",
        encoding="utf-8",
    )
    if with_state:
        (root / "state.csv").write_text(
            "frame,epoch_ms,mono_ms,cam_x,cam_y,cam_z,cam_yaw_deg,cam_pitch_deg,player_x\n"
            "1,1000,1,0,0,0,0,0,0\n"
            "2,1010,2,1,0,0,0,0,0\n"
            "3,1020,3,2,0,0,0,0,0\n",
            encoding="utf-8",
        )
    (root / "camera_d3d8.jsonl").write_text(
        "\n".join(
            [
                json.dumps({"type": "present", "frame": 1, "ts": 1000, "dt": 0}),
                json.dumps({"type": "transform", "state": 2, "frame": 1, "ts": 1000, "m": [1] * 16}),
                json.dumps({"type": "present", "frame": 2, "ts": 1010, "dt": 10}),
                json.dumps({"type": "present", "frame": 3, "ts": 1020, "dt": 10}),
            ]
        )
        + "\n",
        encoding="utf-8",
    )
    frames = root / "frames_ppm"
    frames.mkdir()
    write_ppm(frames / "01_spawn.ppm")
    screenshots = root / "screenshots"
    screenshots.mkdir()
    (screenshots / "01_spawn.png").write_bytes(b"\x89PNG\r\n\x1a\n" + b"\x00" * 16)
    (root / "video.mkv").write_bytes(b"\x1aE\xdf\xa3" + b"\x00" * 8)
    saves = root / "saves"
    saves.mkdir()
    (saves / "slot1.sav").write_bytes(b"save")
    write_sums(root)


class SafeRelativeTest(unittest.TestCase):
    def test_normalizes_relative_paths(self):
        self.assertEqual(
            oracle_client.safe_relative("screenshots/01_spawn.png"),
            "screenshots/01_spawn.png",
        )
        self.assertEqual(
            oracle_client.safe_relative("screenshots\\01_spawn.png"),
            "screenshots/01_spawn.png",
        )

    def test_rejects_unsafe_paths(self):
        for value in ("../secret", "/etc/passwd", "C:\\secret", "a/../../b", ""):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    oracle_client.safe_relative(value)


class ValidateCaptureTest(unittest.TestCase):
    def test_final_capture_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "capture_test"
            root.mkdir()
            build_capture(root)
            report = validate_capture.validate_capture(root)
            self.assertTrue(report["ok"], report["errors"])
            self.assertEqual(report["counts"]["presents"], 3)
            self.assertEqual(report["counts"]["state_rows"], 3)

    def test_missing_state_fails_final_and_passes_partial(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "capture_test"
            root.mkdir()
            build_capture(root, with_state=False)
            report = validate_capture.validate_capture(root)
            self.assertFalse(report["ok"])
            self.assertTrue(any("state.csv" in error for error in report["errors"]))
            partial = validate_capture.validate_capture(root, partial=True)
            self.assertTrue(partial["ok"], partial["errors"])

    def test_manifest_hash_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "capture_test"
            root.mkdir()
            build_capture(root)
            manifest = json.loads((root / "game_manifest.json").read_text())
            manifest["bo2_sha256"] = "0" * 64
            (root / "game_manifest.json").write_text(json.dumps(manifest))
            write_sums(root)
            report = validate_capture.validate_capture(root)
            self.assertFalse(report["ok"])
            self.assertTrue(any("hash mismatch" in error for error in report["errors"]))

    def test_sha256sums_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "capture_test"
            root.mkdir()
            build_capture(root)
            (root / "notes.md").write_text("tampered\n", encoding="utf-8")
            report = validate_capture.validate_capture(root)
            self.assertFalse(report["ok"])
            self.assertTrue(any("SHA256SUMS mismatch" in error for error in report["errors"]))

    def test_camera_coverage_gap_warns(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "capture_test"
            root.mkdir()
            build_capture(root)
            with (root / "inputs.jsonl").open("a", encoding="utf-8") as handle:
                handle.write(json.dumps({"epoch_ms": 5000, "event": "phase", "key": "late"}) + "\n")
            write_sums(root)
            report = validate_capture.validate_capture(root)
            self.assertTrue(
                any("after the last camera present" in warning for warning in report["warnings"])
            )


class AlignTest(unittest.TestCase):
    def test_inputs_map_to_nearest_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "capture_test"
            root.mkdir()
            build_capture(root)
            report = align.summarize(root)
            self.assertEqual(report["frames"]["count"], 3)
            self.assertEqual(report["frames"]["median_ms"], 10)
            self.assertEqual(report["inputs"]["count"], 3)
            self.assertEqual(report["inputs"]["events"][1]["frame"], 2)
            self.assertEqual(report["inputs"]["events"][2]["frame"], 3)
            self.assertEqual(report["phases"]["menu"]["first_frame"], 1)
            self.assertEqual(report["state"]["rows"], 3)
            self.assertEqual(report["state"]["first_frame"], 1)

    def test_events_after_camera_are_unaligned(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "capture_test"
            root.mkdir()
            build_capture(root)
            with (root / "inputs.jsonl").open("a", encoding="utf-8") as handle:
                handle.write(json.dumps({"epoch_ms": 5000, "event": "phase", "key": "late"}) + "\n")
            report = align.summarize(root)
            self.assertEqual(report["inputs"]["unaligned"], 1)
            self.assertIsNone(report["inputs"]["events"][-1]["frame"])
            self.assertEqual(report["camera_coverage"]["last_ts"], 1020)


if __name__ == "__main__":
    unittest.main()
