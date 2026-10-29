import csv
import ctypes
import errno
import io
import math
import os
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "oracle"))

import memstate  # type: ignore  # noqa: E402
import wine_capture  # type: ignore  # noqa: E402

# Header of the native Windows state.csv (capture_20261028_0150).
WINDOWS_HEADER = (
    "frame,epoch_ms,mono_ms,fps_inst,phase,cam_x,cam_y,cam_z,cam_yaw_deg,cam_pitch_deg,"
    "player_x,player_y,player_z,vel_x,vel_y,vel_z,grounded,health,mana,weapon,anim,tick,"
    + ",".join(f"view_m{i:02d}" for i in range(16))
    + ","
    + ",".join(f"proj_m{i:02d}" for i in range(16))
)

HEAD = 0x1000
NODES = (0x2000, 0x2100, 0x2200)
INSTANCES = (0xDEAD0000, 0x3400, 0x3800)  # the first one is not mapped
BEHAVIOR_OTHER, BEHAVIOR_KAIN, BEHAVIOR_KAIN2 = 0x4000, 0x4800, 0x4C00
STATS, HEALTH, LORE = 0x5000, 0x5100, 0x5200
OTHER_VTABLE = 0x00841000
KAIN = memstate.Kain(INSTANCES[1], BEHAVIOR_KAIN)


class FakeMemory(memstate.Memory):
    """Sparse memory: unmapped bytes raise OSError, like /proc/<pid>/mem."""

    def __init__(self):
        self.data: dict[int, int] = {}
        self.ranges: list[tuple[int, int]] = []
        self.closed = False
        self.gone = False  # the process exited: reads fail and alive() is False

    def regions(self) -> list[tuple[int, int]]:
        return self.ranges

    def alive(self) -> bool:
        return not self.gone

    def put(self, address: int, raw: bytes) -> None:
        for offset, value in enumerate(raw):
            self.data[address + offset] = value

    def u32s(self, address: int, *values: int) -> None:
        self.put(address, struct.pack(f"<{len(values)}I", *values))

    def f32s_at(self, address: int, *values: float) -> None:
        self.put(address, struct.pack(f"<{len(values)}f", *values))

    def read(self, address: int, size: int) -> bytes:
        if self.gone:
            raise OSError(errno.EIO, "process gone")
        try:
            return bytes(self.data[address + i] for i in range(size))
        except KeyError:
            raise OSError(errno.EIO, f"unmapped {address:#x}") from None

    def close(self) -> None:
        self.closed = True


def fill_kain(mem: FakeMemory, instance: int, behavior: int, x: float = -8.75) -> None:
    """Make `instance` a Kain with `behavior`. The pointer to the behavior is written last."""
    mem.u32s(behavior, memstate.KAIN_VTABLE)
    mem.f32s_at(instance + memstate.POSITION, x, 57.125, 208.0)
    mem.f32s_at(instance + memstate.YAW, 1.0)
    mem.f32s_at(behavior + memstate.VELOCITY, 1.5, -2.5, 3.5)
    mem.u32s(behavior + memstate.ON_PLATFORM, 1)
    mem.u32s(behavior + memstate.STATS, STATS)
    mem.u32s(instance + memstate.BEHAVIOR, behavior)


def build_world(kain: bool = True) -> FakeMemory:
    """A 3-node instance list; the second instance is Kain."""
    mem = FakeMemory()
    mem.u32s(memstate.OBJECT_LIST, HEAD)
    chain = (HEAD, *NODES, HEAD)
    mem.u32s(HEAD, chain[1], NODES[-1], 0)
    for index, node in enumerate(NODES):
        mem.u32s(node, chain[index + 2], chain[index], INSTANCES[index])
    mem.u32s(INSTANCES[2] + memstate.BEHAVIOR, BEHAVIOR_OTHER)
    mem.u32s(BEHAVIOR_OTHER, OTHER_VTABLE)
    # The stats are shared by every Kain.
    mem.u32s(STATS + memstate.HEALTH, HEALTH)
    mem.u32s(STATS + memstate.LORE, LORE)
    mem.f32s_at(HEALTH + memstate.HEALTH_CURRENT, 75.0)
    mem.f32s_at(HEALTH + memstate.HEALTH_MAX, 100.0)
    mem.f32s_at(LORE + memstate.LORE_CURRENT, 12.5)
    fill_kain(mem, INSTANCES[1], BEHAVIOR_KAIN)
    if not kain:
        mem.u32s(INSTANCES[1] + memstate.BEHAVIOR, BEHAVIOR_OTHER)
        mem.u32s(BEHAVIOR_KAIN, OTHER_VTABLE)
    return mem


HEAP = (0x9000, 0x9200)  # one writable region holding three GameCamera candidates
FOLLOW, FOLLOW_OTHER = 0x7000, 0x7400


def add_camera(mem: FakeMemory, kind: int = 4) -> None:
    """GameCamera candidates: a bad follow pointer, a camera that follows another creature, then Kain's."""
    mem.put(HEAP[0], bytes(HEAP[1] - HEAP[0]))
    mem.ranges = [HEAP]
    for game_camera, follow in ((0x9040, 0xDEAD0000), (0x9080, FOLLOW_OTHER), (0x9100, FOLLOW)):
        mem.u32s(game_camera + memstate.GAME_CAMERA_VTABLE2_OFFSET, memstate.GAME_CAMERA_VTABLE2)
        mem.u32s(game_camera + memstate.FOLLOW_CAMERA, follow)
    for follow, target in ((FOLLOW, INSTANCES[1]), (FOLLOW_OTHER, INSTANCES[2])):
        mem.u32s(follow, memstate.FOLLOW_CAMERA_VTABLE)
        mem.u32s(follow + memstate.FOLLOW_TARGET, target)
    matrix = [0.0, 0.8, -0.6, 0.0,  0.0, 0.0, 0.0, 0.0,  0.0, 0.0, 1.0, 0.0,  1.0, 2.0, 3.0, 0.0]
    mem.f32s_at(FOLLOW + memstate.CAMERA_MATRIX, *matrix)
    mem.u32s(FOLLOW + memstate.CAMERA_MATRIX + memstate.MATRIX_TYPE, kind)


class QuietTestCase(unittest.TestCase):
    """Replaces the capture log, so tests stay quiet and can check what was logged."""

    def setUp(self):
        patcher = mock.patch.object(wine_capture, "log")
        self.log = patcher.start()
        self.addCleanup(patcher.stop)

    def logged(self, text: str) -> list[str]:
        return [call.args[0] for call in self.log.call_args_list if text in call.args[0]]

    def wait_for(self, condition, seconds: float = 5.0) -> None:
        deadline = time.monotonic() + seconds
        while not condition() and time.monotonic() < deadline:
            time.sleep(0.005)
        self.assertTrue(condition())


class FollowCameraTest(unittest.TestCase):
    def test_finds_the_follow_camera_past_a_decoy(self):
        mem = build_world()
        add_camera(mem)
        self.assertEqual(memstate.find_follow_camera(mem, INSTANCES[1]), FOLLOW)

    def test_picks_the_camera_that_follows_the_given_instance(self):
        mem = build_world()
        add_camera(mem)
        self.assertEqual(memstate.find_follow_camera(mem, INSTANCES[2]), FOLLOW_OTHER)
        self.assertIsNone(memstate.find_follow_camera(mem, 0x1234))

    def test_none_without_regions(self):
        self.assertIsNone(memstate.find_follow_camera(build_world(), INSTANCES[1]))

    def test_skips_an_unreadable_region(self):
        mem = build_world()
        add_camera(mem)
        mem.ranges = [(0xA000, 0xA100), HEAP]  # the first range is not mapped
        self.assertEqual(memstate.find_follow_camera(mem, INSTANCES[1]), FOLLOW)

    def test_reads_eye_yaw_and_pitch(self):
        mem = build_world()
        add_camera(mem)
        camera = memstate.read_camera(mem, FOLLOW)
        self.assertEqual(camera["eye"], (1.0, 2.0, 3.0))
        self.assertAlmostEqual(camera["yaw_deg"], 90.0, places=4)
        self.assertAlmostEqual(camera["pitch_deg"], -36.8699, places=3)

    def test_untranslated_matrix_has_no_pose(self):
        mem = build_world()
        add_camera(mem, kind=1)
        self.assertIsNone(memstate.read_camera(mem, FOLLOW))

    def test_nan_forward_has_no_pose(self):
        mem = build_world()
        add_camera(mem)
        mem.f32s_at(FOLLOW + memstate.CAMERA_MATRIX, 0.0, math.nan, -0.6)
        self.assertIsNone(memstate.read_camera(mem, FOLLOW))

    def test_infinite_eye_has_no_pose(self):
        mem = build_world()
        add_camera(mem)
        mem.f32s_at(FOLLOW + memstate.CAMERA_MATRIX + 12 * 4, math.inf, 2.0, 3.0)
        self.assertIsNone(memstate.read_camera(mem, FOLLOW))

    def test_row_carries_the_camera(self):
        mem = build_world()
        add_camera(mem)
        state = memstate.read_state(mem, memstate.find_kain(mem))
        row = wine_capture.state_row(0, 0, 0, "", state, memstate.read_camera(mem, FOLLOW))
        self.assertEqual((row["cam_x"], row["cam_y"], row["cam_z"]), ("1.000000", "2.000000", "3.000000"))
        self.assertEqual(row["cam_yaw_deg"], "90.000000")


class FindKainTest(unittest.TestCase):
    def test_finds_the_second_instance(self):
        self.assertEqual(memstate.find_kain(build_world()), KAIN)

    def test_none_without_kain(self):
        self.assertIsNone(memstate.find_kain(build_world(kain=False)))

    def test_none_before_the_list_exists(self):
        mem = FakeMemory()
        mem.u32s(memstate.OBJECT_LIST, 0)
        self.assertIsNone(memstate.find_kain(mem))

    def test_none_for_an_empty_list(self):
        mem = FakeMemory()
        mem.u32s(memstate.OBJECT_LIST, HEAD)
        mem.u32s(HEAD, HEAD, HEAD, 0)
        self.assertIsNone(memstate.find_kain(mem))

    def test_is_kain_checks_the_vtable_and_the_behavior_pointer(self):
        mem = build_world()
        self.assertTrue(memstate.is_kain(mem, KAIN))
        mem.u32s(BEHAVIOR_KAIN, OTHER_VTABLE)
        self.assertFalse(memstate.is_kain(mem, KAIN))
        mem.u32s(BEHAVIOR_KAIN, memstate.KAIN_VTABLE)
        mem.u32s(INSTANCES[1] + memstate.BEHAVIOR, BEHAVIOR_OTHER)
        self.assertFalse(memstate.is_kain(mem, KAIN))


class ReadStateTest(unittest.TestCase):
    def test_reads_every_field(self):
        mem = build_world()
        state = memstate.read_state(mem, memstate.find_kain(mem))
        self.assertEqual(
            state,
            {"pos": (-8.75, 57.125, 208.0), "yaw": 1.0, "vel": (1.5, -2.5, 3.5), "on_platform": 1,
             "health": 75.0, "health_max": 100.0, "lore": 12.5},
        )

    def test_broken_pointer_raises_oserror(self):
        mem = build_world()
        mem.u32s(BEHAVIOR_KAIN + memstate.STATS, 0)
        with self.assertRaises(OSError):
            memstate.read_state(mem, KAIN)


@unittest.skipUnless(Path("/proc/self/mem").exists(), "needs Linux /proc")
class ProcessMemoryTest(unittest.TestCase):
    def test_reads_own_memory_and_rejects_unmapped_addresses(self):
        buffer = ctypes.create_string_buffer(struct.pack("<If", 0xCAFEBABE, 2.5))
        mem = memstate.ProcessMemory(os.getpid())
        try:
            address = ctypes.addressof(buffer)
            self.assertEqual(mem.u32(address), 0xCAFEBABE)
            self.assertEqual(mem.f32s(address + 4), (2.5,))
            with self.assertRaises(OSError):
                mem.u32(0)
        finally:
            mem.close()

    def test_alive_follows_the_process(self):
        mem = memstate.ProcessMemory(os.getpid())
        try:
            self.assertTrue(mem.alive())
            child = subprocess.Popen([sys.executable, "-c", "pass"])
            child.wait()  # reaped, so /proc/<pid> is gone
            mem.pid = child.pid
            self.assertFalse(mem.alive())
        finally:
            mem.pid = os.getpid()
            mem.close()


class StateCsvTest(unittest.TestCase):
    def test_header_extends_the_windows_header(self):
        header = ",".join(wine_capture.STATE_COLUMNS)
        self.assertTrue(header.startswith(WINDOWS_HEADER))
        self.assertEqual(header[len(WINDOWS_HEADER) :], ",player_yaw_deg,health_max,lore,on_platform")

    def test_row_fills_only_what_is_sampled(self):
        mem = build_world()
        state = memstate.read_state(mem, memstate.find_kain(mem))
        row = wine_capture.state_row(7, 1000, 25, "run", state)
        out = io.StringIO()
        writer = csv.DictWriter(out, wine_capture.STATE_COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerow(row)
        lines = out.getvalue().splitlines()
        values = dict(zip(lines[0].split(","), lines[1].split(",")))
        self.assertEqual(len(lines[1].split(",")), len(wine_capture.STATE_COLUMNS))
        expected = {
            "frame": "7", "epoch_ms": "1000", "mono_ms": "25", "phase": "run",
            "player_x": "-8.750000", "player_y": "57.125000", "player_z": "208.000000",
            "vel_x": "1.500000", "vel_y": "-2.500000", "vel_z": "3.500000",
            "health": "75.000000", "health_max": "100.000000", "lore": "12.500000",
            "on_platform": "1", "player_yaw_deg": "57.295780",
        }
        for column, value in values.items():
            self.assertEqual(value, expected.get(column, ""), column)


class StateSamplerTest(QuietTestCase):
    def sampler(self, mem: FakeMemory) -> "wine_capture.StateSampler":
        return wine_capture.StateSampler(mem, lambda: "run")

    def test_waits_for_kain_then_samples(self):
        mem = build_world(kain=False)
        sampler = wine_capture.StateSampler(mem, lambda: "spawn_idle")
        with mock.patch.object(wine_capture, "SEARCH_PERIOD_S", 0.01):
            sampler.start()
            time.sleep(0.05)
            self.assertEqual(sampler.rows, [])
            mem.u32s(BEHAVIOR_KAIN, memstate.KAIN_VTABLE)  # the level finished loading
            mem.u32s(INSTANCES[1] + memstate.BEHAVIOR, BEHAVIOR_KAIN)
            self.wait_for(lambda: len(sampler.rows) >= 3)
        sampler.stop()
        sampler.stop()  # idempotent
        self.assertTrue(mem.closed)
        self.assertEqual(sampler.found, KAIN)
        self.assertEqual([row["frame"] for row in sampler.rows[:3]], [0, 1, 2])
        self.assertEqual(sampler.rows[0]["phase"], "spawn_idle")
        self.assertIsNone(sampler.error)

    def test_permission_error_stops_sampling(self):
        class Denied(FakeMemory):
            def read(self, address: int, size: int) -> bytes:
                raise PermissionError(errno.EPERM, "denied")

        sampler = self.sampler(Denied())
        sampler.start()
        sampler.join(5)
        self.assertFalse(sampler.is_alive())
        self.assertIsInstance(sampler.error, PermissionError)
        self.assertEqual(sampler.rows, [])
        self.assertEqual(len(self.logged("warning")), 1)
        sampler.stop()

    def test_unreadable_pointers_pause_sampling_without_stopping_it(self):
        mem = build_world()
        sampler = self.sampler(mem)
        with mock.patch.object(wine_capture, "SEARCH_PERIOD_S", 0.01):
            sampler.start()
            self.wait_for(lambda: len(sampler.rows) >= 1)
            mem.u32s(BEHAVIOR_KAIN + memstate.STATS, 0)  # stats pointer lost
            time.sleep(0.1)
            count = len(sampler.rows)
            time.sleep(0.1)
            self.assertEqual(len(sampler.rows), count)
            mem.u32s(BEHAVIOR_KAIN + memstate.STATS, STATS)
            self.wait_for(lambda: len(sampler.rows) > count)
        sampler.stop()
        self.assertIsNone(sampler.error)

    def test_freed_kain_is_dropped_and_the_new_one_is_found(self):
        mem = build_world()
        sampler = self.sampler(mem)
        with mock.patch.object(wine_capture, "SEARCH_PERIOD_S", 0.01):
            sampler.start()
            self.wait_for(lambda: len(sampler.rows) >= 3)
            mem.u32s(BEHAVIOR_KAIN, OTHER_VTABLE)  # freed on a reload; the memory stays mapped
            time.sleep(0.1)
            count = len(sampler.rows)
            time.sleep(0.1)
            self.assertEqual(len(sampler.rows), count)  # no frozen rows from the old Kain
            fill_kain(mem, INSTANCES[2], BEHAVIOR_KAIN2, x=1.0)  # the reloaded Kain
            self.wait_for(lambda: len(sampler.rows) > count)
        sampler.stop()
        self.assertEqual(sampler.found, memstate.Kain(INSTANCES[2], BEHAVIOR_KAIN2))
        self.assertEqual(sampler.rows[-1]["player_x"], "1.000000")
        self.assertIsNone(sampler.error)

    def test_instance_that_points_to_another_behavior_is_dropped(self):
        mem = build_world()
        sampler = self.sampler(mem)
        with mock.patch.object(wine_capture, "SEARCH_PERIOD_S", 0.01):
            sampler.start()
            self.wait_for(lambda: len(sampler.rows) >= 3)
            mem.u32s(INSTANCES[1] + memstate.BEHAVIOR, BEHAVIOR_OTHER)  # the vtable is still Kain's
            time.sleep(0.1)
            count = len(sampler.rows)
            time.sleep(0.1)
            self.assertEqual(len(sampler.rows), count)
            mem.u32s(INSTANCES[1] + memstate.BEHAVIOR, BEHAVIOR_KAIN)
            self.wait_for(lambda: len(sampler.rows) > count)
        sampler.stop()

    def test_game_exit_stops_sampling_and_keeps_the_rows(self):
        mem = build_world()
        sampler = self.sampler(mem)
        sampler.start()
        self.wait_for(lambda: len(sampler.rows) >= 2)
        mem.gone = True  # reads fail, and the process is gone
        sampler.join(5)
        self.assertFalse(sampler.is_alive())
        self.assertEqual(str(sampler.error), "game exited")
        self.assertEqual(sampler.stopped, "game exited")
        self.assertGreaterEqual(len(sampler.rows), 2)
        self.assertEqual(len(self.logged("warning")), 1)
        self.assertIn("game exited", self.logged("warning")[0])
        sampler.stop()

    def test_unexpected_exception_stops_sampling(self):
        class Broken(FakeMemory):
            def read(self, address: int, size: int) -> bytes:
                raise ValueError("boom")

        sampler = self.sampler(Broken())
        sampler.start()
        sampler.join(5)
        self.assertFalse(sampler.is_alive())
        self.assertIsInstance(sampler.error, ValueError)
        self.assertEqual(sampler.stopped, "unexpected ValueError: boom")
        self.assertEqual(len(self.logged("warning")), 1)
        sampler.stop()


class CameraSamplingTest(QuietTestCase):
    def setUp(self):
        super().setUp()
        self.mem = build_world()
        add_camera(self.mem)
        self.sampler = wine_capture.StateSampler(self.mem, lambda: "run")
        self.sampler.kain = KAIN
        patcher = mock.patch.object(wine_capture, "CAMERA_SEARCH_PERIOD_S", 1000.0)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_finds_the_camera_and_logs_how_long_the_search_took(self):
        camera = self.sampler.sample_camera()
        self.assertEqual(camera["eye"], (1.0, 2.0, 3.0))
        self.assertEqual(self.sampler.camera, FOLLOW)
        self.assertEqual(self.sampler.camera_found, FOLLOW)
        (message,) = self.logged("camera search")
        self.assertRegex(message, r"^camera search took \d+ ms, found$")

    def test_does_not_search_again_before_the_period_has_passed(self):
        with mock.patch.object(wine_capture, "find_follow_camera", return_value=None) as search:
            self.assertIsNone(self.sampler.sample_camera())
            self.assertIsNone(self.sampler.sample_camera())
            search.assert_called_once_with(self.mem, INSTANCES[1])
            self.sampler.camera_search_at = 0.0  # the period has passed
            self.sampler.sample_camera()
            self.assertEqual(search.call_count, 2)
        self.assertEqual(len(self.logged("not found")), 2)

    def test_camera_is_dropped_when_it_stops_following_kain(self):
        self.assertIsNotNone(self.sampler.sample_camera())
        self.mem.u32s(FOLLOW + memstate.FOLLOW_TARGET, INSTANCES[2])  # follows another creature now
        self.assertIsNone(self.sampler.sample_camera())
        self.assertIsNone(self.sampler.camera)
        self.mem.u32s(FOLLOW + memstate.FOLLOW_TARGET, INSTANCES[1])
        self.sampler.camera_search_at = 0.0
        self.assertIsNotNone(self.sampler.sample_camera())

    def test_camera_is_dropped_when_it_is_freed(self):
        self.assertIsNotNone(self.sampler.sample_camera())
        self.mem.u32s(FOLLOW, 0)  # the vtable is gone
        self.assertIsNone(self.sampler.sample_camera())
        self.assertIsNone(self.sampler.camera)


class StateFilesTest(QuietTestCase):
    """`Capture.write_state` and `notes_text` on a sampler that was fed rows."""

    def setUp(self):
        super().setUp()
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.out = Path(tmp.name)
        mem = build_world()
        add_camera(mem)
        self.sampler = wine_capture.StateSampler(mem, lambda: "run")
        state = memstate.read_state(mem, KAIN)
        camera = memstate.read_camera(mem, FOLLOW)
        for index in range(3):  # the last row has no camera pose
            pose = camera if index < 2 else None
            self.sampler.rows.append(
                wine_capture.state_row(index, 1000 + 20 * index, 50 + 20 * index, "run", state, pose)
            )
        self.sampler.found, self.sampler.camera_found = KAIN, FOLLOW
        self.capture = wine_capture.Capture(self.out, self.out, "route.txt", "0" * 64)
        self.capture.sampler = self.sampler
        self.manifest = {
            "capture_id": "wine_test", "created_utc": "2026-10-29T00:00:00Z", "bo2_sha256": "0" * 64,
            "wine_version": "wine-test", "renderer": "wined3d/llvmpipe", "resolution": "800x600",
            "route": "route.txt",
        }

    def written_rows(self) -> list[dict]:
        with (self.out / "state.csv").open(newline="", encoding="utf-8") as handle:
            return list(csv.DictReader(handle))

    def test_write_state_writes_the_rows_and_the_manifest_summary(self):
        self.capture.write_state(self.manifest)
        rows = self.written_rows()
        self.assertEqual([row["frame"] for row in rows], ["0", "1", "2"])
        self.assertEqual(rows[0]["cam_x"], "1.000000")
        self.assertEqual(rows[2]["cam_x"], "")
        self.assertEqual(
            self.manifest["state"],
            {
                "rows": 3, "period_ms": 20, "mean_period_ms": 20.0,
                "kain_instance": f"{INSTANCES[1]:#x}", "kain_behavior": f"{BEHAVIOR_KAIN:#x}",
                "follow_camera": f"{FOLLOW:#x}", "camera_rows": 2,
                "offsets": "docs/ORACLE_WINE.md, Game state",
            },
        )

    def test_write_state_keeps_the_rows_sampled_before_an_error(self):
        self.sampler.error = ProcessLookupError("game exited")
        self.sampler.stopped = "game exited"
        self.capture.write_state(self.manifest)
        self.assertEqual(len(self.written_rows()), 3)
        self.assertEqual(self.manifest["state"]["rows"], 3)
        self.assertEqual(self.manifest["state"]["stopped"], "game exited")

    def test_write_state_writes_nothing_when_an_error_left_no_rows(self):
        self.sampler.rows.clear()
        self.sampler.error = PermissionError(errno.EPERM, "denied")
        self.sampler.stopped = "cannot read the game's memory"
        self.capture.write_state(self.manifest)
        self.assertFalse((self.out / "state.csv").exists())
        self.assertNotIn("state", self.manifest)
        self.assertEqual(self.logged("never found"), [])  # the sampler already warned

    def test_write_state_warns_when_kain_never_appeared(self):
        self.sampler.rows.clear()
        self.capture.write_state(self.manifest)
        self.assertFalse((self.out / "state.csv").exists())
        self.assertNotIn("state", self.manifest)
        self.assertEqual(len(self.logged("never found")), 1)

    def test_write_state_does_nothing_without_a_sampler(self):
        self.capture.sampler = None
        self.capture.write_state(self.manifest)
        self.assertFalse((self.out / "state.csv").exists())
        self.assertNotIn("state", self.manifest)

    def test_notes_describe_what_was_sampled(self):
        self.capture.write_state(self.manifest)
        text = wine_capture.notes_text(self.manifest, ["menu", "run"], 1234)
        self.assertIn("every 20 ms", text)
        self.assertIn("Kain's position, yaw, velocity, health and lore", text)
        self.assertIn("2 of 3 rows have a camera pose", text)
        self.assertIn("`cam_*` is empty in the rest", text)
        self.assertNotIn("not located", text)
        self.assertNotIn("stopped early", text)

    def test_notes_with_a_camera_in_every_row(self):
        self.manifest["state"] = {"rows": 3, "period_ms": 20, "camera_rows": 3}
        text = wine_capture.notes_text(self.manifest, ["menu"], 1234)
        self.assertIn("3 of 3 rows have a camera pose. Columns", text)

    def test_notes_report_an_early_stop(self):
        self.sampler.stopped = "game exited"
        self.capture.write_state(self.manifest)
        text = wine_capture.notes_text(self.manifest, ["menu"], 1234)
        self.assertIn("Sampling stopped early (game exited)", text)

    def test_notes_without_state(self):
        text = wine_capture.notes_text(self.manifest, ["menu"], 1234)
        self.assertIn("No `state.csv`", text)
        self.assertNotIn("every 20 ms", text)


if __name__ == "__main__":
    unittest.main()
