import contextlib
import csv
import io
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "oracle"))

import align  # type: ignore  # noqa: E402
import oracle_client  # type: ignore  # noqa: E402
import validate_capture  # type: ignore  # noqa: E402
import wine_capture  # type: ignore  # noqa: E402


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


def build_capture(
    root: Path,
    with_state: bool = True,
    with_camera: bool = True,
    source: str | None = None,
) -> None:
    manifest = {
        "bo2_sha256": validate_capture.REFERENCE_BO2_SHA256,
        "resolution": [800, 600],
        "patch_applied": False,
    }
    if source:
        manifest["source"] = source
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
    if with_camera:
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


class WineCaptureValidationTest(unittest.TestCase):
    def test_wine_capture_without_camera_or_state_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "wine_test"
            root.mkdir()
            build_capture(root, with_state=False, with_camera=False, source="wine")
            report = validate_capture.validate_capture(root)
            self.assertTrue(report["ok"], report["errors"])
            self.assertEqual(report["manifest"]["source"], "wine")
            self.assertFalse(
                any("camera" in warning or "state" in warning for warning in report["warnings"])
            )

    def test_wine_capture_still_needs_video_and_saves(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "wine_test"
            root.mkdir()
            build_capture(root, with_state=False, with_camera=False, source="wine")
            (root / "video.mkv").unlink()
            shutil.rmtree(root / "saves")
            write_sums(root)
            report = validate_capture.validate_capture(root)
            self.assertFalse(report["ok"])
            self.assertTrue(any("video.mkv" in error for error in report["errors"]))
            self.assertTrue(any("saves/" in error for error in report["errors"]))

    def test_same_files_without_wine_source_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "capture_test"
            root.mkdir()
            build_capture(root, with_state=False, with_camera=False)
            report = validate_capture.validate_capture(root)
            self.assertFalse(report["ok"])
            self.assertEqual(report["manifest"]["source"], "windows")
            self.assertTrue(any("camera_d3d8.jsonl" in error for error in report["errors"]))
            self.assertTrue(any("state.csv" in error for error in report["errors"]))

    def test_wine_state_csv_from_memory_sampling_passes_cleanly(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "wine_test"
            root.mkdir()
            build_capture(root, with_camera=False, source="wine")
            columns = wine_capture.STATE_COLUMNS
            row = {"frame": 0, "epoch_ms": 1000, "mono_ms": 1, "phase": "run", "player_x": "-8.8"}
            with (root / "state.csv").open("w", newline="", encoding="utf-8") as handle:
                writer = csv.DictWriter(handle, columns, lineterminator="\n")
                writer.writeheader()
                writer.writerow(row)
            write_sums(root)
            report = validate_capture.validate_capture(root)
            self.assertTrue(report["ok"], report["errors"])
            self.assertFalse(
                any("state" in warning or "camera" in warning for warning in report["warnings"])
            )
            self.assertEqual(report["counts"]["state_rows"], 1)
            self.assertEqual(report["state_fields"], columns)

    def test_wine_source_skips_camera_coverage_warnings(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "wine_test"
            root.mkdir()
            build_capture(root, source="wine")
            with (root / "inputs.jsonl").open("a", encoding="utf-8") as handle:
                handle.write(json.dumps({"epoch_ms": 5000, "event": "phase", "key": "late"}) + "\n")
            write_sums(root)
            report = validate_capture.validate_capture(root)
            self.assertTrue(report["ok"], report["errors"])
            self.assertFalse(any("camera" in warning for warning in report["warnings"]))


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

    def test_capture_without_camera_reports_inputs_and_phases(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "wine_test"
            root.mkdir()
            build_capture(root, with_state=False, with_camera=False, source="wine")
            report = align.summarize(root)
            self.assertEqual(report["frames"]["count"], 0)
            self.assertEqual(report["inputs"]["count"], 3)
            self.assertEqual(report["inputs"]["aligned"], 0)
            self.assertIn("menu", report["phases"])
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                align.print_summary(report)
            text = output.getvalue()
            self.assertIn("no camera data", text)
            self.assertIn("phase:   menu at +0.0 s", text)
            self.assertNotIn("outside camera", text)


class RouteParserTest(unittest.TestCase):
    def test_every_step_in_arity_parses_with_that_many_arguments(self):
        samples = {
            "wait": "wait 1",
            "key": "key a",
            "hold": "hold a 1",
            "click": "click 1",
            "mousehold": "mousehold 1 1",
            "shot": "shot a",
            "phase": "phase a",
        }
        self.assertEqual(set(samples), set(wine_capture.ARITY))
        for verb, line in samples.items():
            with self.subTest(verb=verb):
                (step,) = wine_capture.parse_route(line)
                self.assertEqual(len(step.args), wine_capture.ARITY[verb])

    def test_parses_every_step(self):
        text = (
            "# comment\n"
            "\n"
            "phase spawn_idle\n"
            "wait 4000  # boot\n"
            "key Return\n"
            "hold shift+w 2000\n"
            "click 3\n"
            "mousehold 1 500\n"
            "shot 01_spawn\n"
        )
        steps = [(step.verb, step.args) for step in wine_capture.parse_route(text)]
        self.assertEqual(
            steps,
            [
                ("phase", ("spawn_idle",)),
                ("wait", (4000,)),
                ("key", ("Return",)),
                ("hold", (("shift", "w"), 2000)),
                ("click", (3,)),
                ("mousehold", (1, 500)),
                ("shot", ("01_spawn",)),
            ],
        )

    def test_hold_keys_may_carry_a_press_delay(self):
        (step,) = wine_capture.parse_route("hold w+x@1000 2500")
        self.assertEqual(step.args, (("w", "x@1000"), 2500))
        self.assertEqual([wine_capture._key_delay(key) for key in step.args[0]], [("w", 0), ("x", 1000)])

    def test_rejects_bad_steps_with_line_number(self):
        for text in (
            "jump 1",
            "wait",
            "wait soon",
            "wait -5",
            "hold w",
            "hold +w 100",
            "hold w+x@3000 2500",
            "hold w@500+x 1000",
            "hold w+x@ 1000",
            "hold w+x@soon 1000",
            "click 4",
            "shot ../escape",
        ):
            with self.subTest(text=text):
                with self.assertRaisesRegex(ValueError, "route line 2"):
                    wine_capture.parse_route("phase ok\n" + text)

    def test_bundled_route_parses(self):
        route = Path(__file__).resolve().parents[2] / "tools" / "oracle" / "routes" / "sl01_basic.txt"
        steps = wine_capture.parse_route(route.read_text(encoding="utf-8"))
        shots = [step.args[0] for step in steps if step.verb == "shot"]
        self.assertIn("01_spawn", shots)
        self.assertEqual(steps[-1].verb, "phase")


def solid_frame(value: int) -> bytearray:
    return bytearray([value]) * (800 * 600 * 3)


def paint(frame: bytearray, x0: int, x1: int, y0: int, y1: int, value: int) -> None:
    for y in range(y0, y1):
        start = (y * 800 + x0) * 3
        frame[start : start + (x1 - x0) * 3] = bytes([value]) * ((x1 - x0) * 3)


class MenuDetectionTest(unittest.TestCase):
    def test_black_frame_is_not_the_menu(self):
        self.assertFalse(wine_capture.looks_like_menu(bytes(solid_frame(0))))

    def test_dialog_on_black_is_not_the_menu(self):
        frame = solid_frame(0)
        paint(frame, 280, 520, 240, 360, 240)  # small light box in the center
        self.assertFalse(wine_capture.looks_like_menu(bytes(frame)))

    def test_textured_edges_are_the_menu(self):
        frame = solid_frame(0)
        paint(frame, 0, 120, 0, 600, 40)
        paint(frame, 680, 800, 0, 600, 40)
        self.assertTrue(wine_capture.looks_like_menu(bytes(frame)))

    def test_one_lit_edge_is_not_enough(self):
        frame = solid_frame(0)
        paint(frame, 0, 120, 0, 600, 40)
        self.assertFalse(wine_capture.looks_like_menu(bytes(frame)))

    def test_frame_change(self):
        frame = bytes(solid_frame(40))
        self.assertEqual(wine_capture.frame_change(frame, frame), 0)
        self.assertGreater(wine_capture.frame_change(bytes(solid_frame(0)), frame), 1.0)

    def test_frame_bytes_drops_the_ppm_header(self):
        pixels = bytes(range(256)) * (800 * 600 * 3 // 256)
        raw = b"P6\n800 600\n255\n" + pixels
        self.assertEqual(wine_capture.frame_bytes(raw), pixels)


if __name__ == "__main__":
    unittest.main()
