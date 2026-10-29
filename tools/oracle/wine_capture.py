#!/usr/bin/env python3
"""Run one scripted capture of the pristine bo2.exe under Wine.

Starts a private Xvfb, launches the game on wined3d, replays a route file
with xdotool and records inputs, screenshots, PPM frames and video in the
same layout as the native Windows capture. The Wine harness has no D3D
hook, so it writes no `camera_d3d8.jsonl`. While the route runs it samples
Kain's state from the game's memory (`memstate.py`) into `state.csv`.

Prepare the prefix first with `scripts/oracle/wine_setup.sh`. Route grammar
and limits are in `docs/ORACLE_WINE.md`.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import pwd
import re
import select
import shutil
import signal
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import NamedTuple

from memstate import (Kain, Memory, ProcessMemory, find_follow_camera, find_kain, is_follow_camera,
                      is_kain, read_camera, read_instances, read_state)
from validate_capture import REFERENCE_BO2_SHA256, sha256_file

ROOT = Path(__file__).resolve().parents[2]
CAPTURES = ROOT / "external" / "captures"
DEFAULT_PREFIX = Path.home() / ".local" / "share" / "bo2-wine"
GAME_EXE = "C:\\BO2Oracle\\bo2.exe"
TOOLS = ("Xvfb", "xdotool", "ffmpeg", "import", "convert", "wine", "wineserver")
WIDTH, HEIGHT = 800, 600
BOOT_TIMEOUT_S = 180
STABLE_COMPARISONS = 3  # consecutive unchanged 2 s comparisons before the menu counts as ready
TAP_MS = 80  # `key` and `click` hold time; the Windows capture used 73-85 ms
BUTTONS = {1: "lmouse", 2: "mmouse", 3: "rmouse"}
KEY_NAMES = {"return": "enter", "escape": "esc"}
NAME_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
# Arguments each route step takes.
ARITY = {"wait": 1, "key": 1, "hold": 2, "click": 1, "mousehold": 2, "shot": 1, "phase": 1}
SAMPLE_PERIOD_S = 0.02  # state sampling period
SEARCH_PERIOD_S = 0.5  # how often to look for Kain until he exists
CAMERA_SEARCH_PERIOD_S = 2.0  # the camera search reads the whole heap; keep it rare
# The Windows `state.csv` header, then the columns only the memory sampler fills.
STATE_COLUMNS = (
    ["frame", "epoch_ms", "mono_ms", "fps_inst", "phase", "cam_x", "cam_y", "cam_z",
     "cam_yaw_deg", "cam_pitch_deg", "player_x", "player_y", "player_z", "vel_x", "vel_y",
     "vel_z", "grounded", "health", "mana", "weapon", "anim", "tick"]
    + [f"view_m{i:02d}" for i in range(16)]
    + [f"proj_m{i:02d}" for i in range(16)]
    + ["player_yaw_deg", "health_max", "lore", "on_platform"]
)


class Step(NamedTuple):
    verb: str
    args: tuple
    text: str


def parse_route(text: str) -> list[Step]:
    """Parse a route file. Raises ValueError with the line number on bad input."""
    steps: list[Step] = []
    for number, raw in enumerate(text.splitlines(), 1):
        words = raw.split("#", 1)[0].split()
        if not words:
            continue
        verb, rest = words[0], words[1:]
        try:
            steps.append(Step(verb, _parse_args(verb, rest), " ".join(words)))
        except ValueError as error:
            raise ValueError(f"route line {number}: {error}") from None
    return steps


def _parse_args(verb: str, rest: list[str]) -> tuple:
    if verb not in ARITY:
        raise ValueError(f"unknown step '{verb}'")
    if len(rest) != ARITY[verb]:
        raise ValueError(f"'{verb}' takes {ARITY[verb]} argument(s), got {len(rest)}")
    if verb == "wait":
        return (_millis(rest[0]),)
    if verb == "key":
        return (rest[0],)
    if verb == "hold":
        keys = tuple(rest[0].split("+"))
        if not all(keys):
            raise ValueError(f"bad key combination '{rest[0]}'")
        duration = _millis(rest[1])
        delays = [_key_delay(key)[1] for key in keys]
        if delays != sorted(delays) or delays[-1] > duration:
            raise ValueError(f"key delays in '{rest[0]}' must not decrease and must fit in {duration} ms")
        return (keys, duration)
    if verb == "click":
        return (_button(rest[0]),)
    if verb == "mousehold":
        return (_button(rest[0]), _millis(rest[1]))
    if not NAME_RE.match(rest[0]):
        raise ValueError(f"bad name '{rest[0]}'")
    return (rest[0],)


def _key_delay(token: str) -> tuple[str, int]:
    """Split `key@ms` into the key and the delay after the start of the hold (0 without `@`)."""
    key, separator, delay = token.partition("@")
    if not key or (separator and not delay.isdigit()):
        raise ValueError(f"bad key '{token}'")
    return key, int(delay) if separator else 0


def _millis(value: str) -> int:
    if not value.isdigit():
        raise ValueError(f"expected milliseconds, got '{value}'")
    return int(value)


def _button(value: str) -> int:
    if value not in {"1", "2", "3"}:
        raise ValueError(f"mouse button must be 1, 2 or 3, got '{value}'")
    return int(value)


def now_ms() -> int:
    return int(time.time() * 1000)


def mono_ms() -> int:
    return int(time.monotonic() * 1000)


def log(message: str) -> None:
    print(f"[wine_capture] {message}", flush=True)


def frame_bytes(raw: bytes) -> bytes:
    """Pixel data of a binary 800x600 PPM, without the header."""
    return raw[-WIDTH * HEIGHT * 3 :]


def frame_change(previous: bytes, current: bytes) -> float:
    """Mean absolute difference per sampled byte between two frames."""
    sample = current[::61]
    return sum(abs(a - b) for a, b in zip(sample, previous[::61])) / len(sample)


def looks_like_menu(frame: bytes) -> bool:
    """True if the left and right edges of the frame are textured, not black.

    The main menu draws a carved frame around the screen. A black screen or a
    dialog on black (the startup warning or the video configuration dialog)
    leaves both edge strips dark.
    """
    lit = total = 0
    for y in range(0, HEIGHT, 7):
        for x in (*range(0, 100, 5), *range(WIDTH - 100, WIDTH, 5)):
            i = (y * WIDTH + x) * 3
            total += 1
            lit += max(frame[i : i + 3]) >= 16
    return lit / total >= 0.9


def state_row(frame: int, epoch_ms: int, mono_ms: int, phase: str, state: dict,
              camera: dict | None = None) -> dict:
    """One `state.csv` row. Columns that are not sampled are left out and written empty."""

    def number(value: float) -> str:
        return f"{value:.6f}"

    row = {"frame": frame, "epoch_ms": epoch_ms, "mono_ms": mono_ms, "phase": phase,
           "health": number(state["health"]), "health_max": number(state["health_max"]),
           "lore": number(state["lore"]), "on_platform": state["on_platform"],
           "anim": state.get("anim", ""),
           "player_yaw_deg": number(math.degrees(state["yaw"]))}
    for axis, value in zip("xyz", state["pos"]):
        row[f"player_{axis}"] = number(value)
    for axis, value in zip("xyz", state["vel"]):
        row[f"vel_{axis}"] = number(value)
    if camera is not None:
        for axis, value in zip("xyz", camera["eye"]):
            row[f"cam_{axis}"] = number(value)
        row["cam_yaw_deg"] = number(camera["yaw_deg"])
        row["cam_pitch_deg"] = number(camera["pitch_deg"])
    return row


class StateSampler(threading.Thread):
    """Sample Kain's state every SAMPLE_PERIOD_S until stopped.

    Kain does not exist until the level has loaded, so the sampler looks for
    him until he does. Freed heap objects stay mapped and keep reading fine,
    so every sample checks again that Kain and the camera are still the live
    objects. A failed check or a pointer that stops reading sends the sampler
    back to the search. Permission errors, a game exit and unexpected
    exceptions stop it for good with `error` and `stopped` set; the rows
    sampled before that are kept.
    """

    def __init__(self, mem: Memory, phase, dump_phase: str | None = None):
        super().__init__(daemon=True)
        self.mem = mem
        self.phase = phase  # callable returning the current route phase
        self.dump_phase = dump_phase  # route phase whose first sample dumps every instance
        self.instances: list[dict] | None = None
        self.stopping = threading.Event()
        self.rows: list[dict] = []
        self.kain: Kain | None = None  # current Kain, None while searching
        self.found: Kain | None = None  # last Kain found, for the manifest
        self.camera: int | None = None  # FollowingCreatureCameraImpl, None while searching
        self.camera_found: int | None = None  # last camera found, for the manifest
        self.camera_search_at = 0.0  # monotonic time of the next camera search
        self.error: Exception | None = None
        self.stopped: str | None = None  # why sampling stopped, for the manifest

    def run(self) -> None:
        next_tick = time.monotonic()
        while not self.stopping.is_set():
            epoch, mono = now_ms(), mono_ms()
            try:
                self.sample(epoch, mono)
            except (PermissionError, ProcessLookupError) as error:
                self.fail(error, f"cannot read the game's memory ({error})")
                return
            except OSError:
                if not self.mem.alive():
                    self.fail(ProcessLookupError("game exited"), "game exited")
                    return
                self.kain = None  # stale pointers: search again
            except Exception as error:  # a bug must not end the thread silently
                self.fail(error, f"unexpected {type(error).__name__}: {error}")
                return
            if self.kain is None:
                self.stopping.wait(SEARCH_PERIOD_S)
                next_tick = time.monotonic()
                continue
            next_tick += SAMPLE_PERIOD_S
            delay = next_tick - time.monotonic()
            if delay > 0:
                self.stopping.wait(delay)
            else:
                next_tick = time.monotonic()  # behind schedule: do not burst

    def fail(self, error: Exception, reason: str) -> None:
        """Record why sampling stopped and log it once."""
        self.error = error
        self.stopped = reason
        log(f"warning: state sampling stopped: {reason}")

    def sample(self, epoch: int, mono: int) -> None:
        """Append one row if Kain exists. Raises OSError when a pointer cannot be read."""
        if self.dump_phase and self.instances is None and self.phase() == self.dump_phase:
            self.instances = read_instances(self.mem)
        if self.kain is not None and not is_kain(self.mem, self.kain):
            self.kain = None  # freed or replaced, for example by a reload
        if self.kain is None:
            self.kain = find_kain(self.mem)
            self.found = self.kain or self.found
        if self.kain is not None:
            state = read_state(self.mem, self.kain)
            camera = self.sample_camera()
            self.rows.append(state_row(len(self.rows), epoch, mono, self.phase(), state, camera))

    def sample_camera(self) -> dict | None:
        """Read the camera; a missing, freed or foreign camera leaves the camera columns empty."""
        instance = self.kain.instance
        if self.camera is not None:
            try:
                if not is_follow_camera(self.mem, self.camera, instance):
                    self.camera = None  # freed, or no longer following Kain
            except OSError:
                self.camera = None
        if self.camera is None and time.monotonic() >= self.camera_search_at:
            started = time.monotonic()
            self.camera = find_follow_camera(self.mem, instance)
            self.camera_found = self.camera or self.camera_found
            self.camera_search_at = time.monotonic() + CAMERA_SEARCH_PERIOD_S
            log(f"camera search took {(time.monotonic() - started) * 1000:.0f} ms, "
                f"{'found' if self.camera else 'not found'}")
        if self.camera is None:
            return None
        try:
            return read_camera(self.mem, self.camera)
        except OSError:
            self.camera = None  # stale pointer: search again later
            return None

    def stop(self) -> None:
        """Stop the thread and close the memory handle. Safe to call more than once."""
        if self.stopping.is_set():
            return
        self.stopping.set()
        self.join(5)
        self.mem.close()


class Capture:
    def __init__(self, out: Path, prefix: Path, route_name: str, bo2_sha256: str,
                 sample_state: bool = True, dump_phase: str | None = None):
        self.out = out
        self.prefix = prefix
        self.route_name = route_name
        self.bo2_sha256 = bo2_sha256
        self.sample_state = sample_state
        self.dump_phase = dump_phase
        self.display = 0
        self.env: dict[str, str] = {}
        self.xvfb: subprocess.Popen | None = None
        self.wine: subprocess.Popen | None = None
        self.ffmpeg: subprocess.Popen | None = None
        self.inputs = None
        self.video_start_ms = 0
        self.phases: list[str] = []
        self.phase = ""  # current route phase
        self.sampler: StateSampler | None = None
        self.problems: list[str] = []  # failures found while stopping; they make the run fail

    # --- lifecycle -------------------------------------------------------

    def start(self, boot_wait: float | None) -> None:
        for sub in ("screenshots", "frames_ppm", "logs"):
            (self.out / sub).mkdir(parents=True, exist_ok=True)
        self.inputs = (self.out / "inputs.jsonl").open("a", encoding="utf-8")
        self.record("phase", key="capture_start", capture_id=self.out.name)
        self.start_xvfb()
        self.start_game()
        self.wait_for_menu(boot_wait)
        self.xdo("mousemove", str(WIDTH // 2), str(HEIGHT // 2))
        self.start_video()

    def start_xvfb(self) -> None:
        # Xvfb picks a free display and reports its number on this pipe.
        read_end, write_end = os.pipe()
        command = [
            "Xvfb", "-displayfd", str(write_end), "-screen", "0", f"{WIDTH}x{HEIGHT}x24",
            "+extension", "GLX", "+extension", "RANDR", "-nolisten", "tcp",
        ]
        log(" ".join(command))
        self.xvfb = subprocess.Popen(
            command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True, pass_fds=(write_end,),
        )
        os.close(write_end)
        try:
            self.display = self._read_display(read_end)
        finally:
            os.close(read_end)
        self.env = dict(os.environ)
        self.env.update(
            DISPLAY=f":{self.display}",
            WINEPREFIX=str(self.prefix),
            WINEARCH="win32",
            WINEDLLOVERRIDES="mscoree,mshtml=;d3d8=b",
        )
        self.env.setdefault("WINEDEBUG", "-all")
        log(f"display :{self.display}")

    def _read_display(self, fd: int) -> int:
        data = b""
        deadline = time.monotonic() + 30
        while b"\n" not in data:
            if time.monotonic() > deadline:
                raise RuntimeError("Xvfb did not report a display within 30 s")
            if select.select([fd], [], [], 0.2)[0]:
                chunk = os.read(fd, 32)
                if not chunk:
                    raise RuntimeError("Xvfb exited before reporting a display")
                data += chunk
        return int(data.split()[0])

    def start_game(self) -> None:
        # Wine takes the current directory from $PWD, so keep the symlinked path.
        directory = str(self.prefix / "drive_c" / "BO2Oracle")
        env = dict(self.env, PWD=directory)
        log(f"wine {GAME_EXE}")
        wine_log = (self.out / "logs" / "wine.log").open("wb")
        self.wine = subprocess.Popen(
            ["wine", GAME_EXE], cwd=directory, env=env,
            stdout=wine_log, stderr=subprocess.STDOUT, start_new_session=True,
        )

    def grab(self) -> bytes:
        raw = subprocess.run(
            ["import", "-silent", "-window", "root", "-depth", "8", "ppm:-"],
            env=self.env, capture_output=True, check=True,
        ).stdout
        return frame_bytes(raw)

    def wait_for_menu(self, boot_wait: float | None) -> None:
        if boot_wait is not None:
            log(f"boot wait {boot_wait:g} s")
            self.sleep(int(boot_wait * 1000))
            return
        # The menu is a static, textured frame. Dialogs and black screens are
        # static too, so the stable frame must also pass looks_like_menu().
        log("waiting for the main menu")
        started, previous, stable = time.monotonic(), None, 0
        while time.monotonic() - started < BOOT_TIMEOUT_S:
            self.sleep(2000)
            current = self.grab()
            unchanged = previous is not None and frame_change(previous, current) < 1.0
            stable = stable + 1 if unchanged and looks_like_menu(current) else 0
            previous = current
            if stable >= STABLE_COMPARISONS:
                log(f"main menu after {time.monotonic() - started:.0f} s")
                return
        self.screen_to(self.out / "logs" / "boot_timeout.png")
        raise RuntimeError(
            f"no stable main menu within {BOOT_TIMEOUT_S} s (a dialog or a black screen "
            "may be showing); see logs/boot_timeout.png and logs/wine.log"
        )

    def start_video(self) -> None:
        command = [
            "ffmpeg", "-hide_banner", "-loglevel", "warning", "-f", "x11grab",
            "-draw_mouse", "0", "-framerate", "30", "-video_size", f"{WIDTH}x{HEIGHT}",
            "-i", f":{self.display}", "-c:v", "libx264", "-preset", "ultrafast",
            "-crf", "18", "-pix_fmt", "yuv420p", "-y", str(self.out / "video.mkv"),
        ]
        self.video_start_ms = now_ms()
        ffmpeg_log = (self.out / "logs" / "ffmpeg.log").open("wb")
        self.ffmpeg = subprocess.Popen(
            command, stdin=subprocess.DEVNULL, stdout=ffmpeg_log, stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        time.sleep(0.5)
        if self.ffmpeg.poll() is not None:
            raise RuntimeError("ffmpeg exited at start; see logs/ffmpeg.log")
        video = {"start_epoch_ms": self.video_start_ms, "client": [0, 0, WIDTH, HEIGHT]}
        (self.out / "video_start.json").write_text(json.dumps(video, indent=2), encoding="utf-8")

    def start_state(self) -> None:
        """Start sampling Kain's state. The game process is the one `wine` started."""
        if not self.sample_state:
            return
        try:
            mem = ProcessMemory(self.wine.pid)
        except OSError as error:
            log(f"warning: cannot open the game's memory ({error}); no state.csv")
            return
        self.sampler = StateSampler(mem, lambda: self.phase, self.dump_phase)
        self.sampler.start()

    def stop(self) -> None:
        """Stop the sampler, ffmpeg, Wine and Xvfb. Safe to call more than once.

        Signals are ignored from here on so a second Ctrl+C or a hangup cannot
        abort the cleanup halfway.
        """
        for number in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            signal.signal(number, signal.SIG_IGN)
        if self.sampler:
            self.sampler.stop()
        if self.ffmpeg:
            if self.ffmpeg.poll() is not None:
                self.problems.append(
                    f"ffmpeg exited early with code {self.ffmpeg.returncode}; "
                    "video.mkv is truncated (see logs/ffmpeg.log)"
                )
            elif not self._wait(self.ffmpeg, 15, signal.SIGINT):  # SIGINT lets ffmpeg write the trailer
                self.problems.append("ffmpeg did not stop within 15 s; video.mkv is truncated")
        if self.env:
            for flag in ("-k", "-w"):
                try:
                    subprocess.run(["wineserver", flag], env=self.env, timeout=30,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                except (OSError, subprocess.TimeoutExpired):
                    pass
        if self.wine:
            self._wait(self.wine, 10)
        if self.xvfb:
            self._wait(self.xvfb, 10)
        if self.inputs and not self.inputs.closed:
            self.inputs.close()

    @staticmethod
    def _wait(process: subprocess.Popen, seconds: int, stop: int = signal.SIGTERM) -> bool:
        """Signal a process and wait; kill it on timeout. True if it exited in time."""
        if process.poll() is not None:
            return True
        process.send_signal(stop)
        try:
            process.wait(seconds)
            return True
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            return False

    # --- route steps -----------------------------------------------------

    def record(self, event: str, **fields) -> None:
        item = {"epoch_ms": now_ms(), "mono_ms": mono_ms(), "event": event}
        item.update(fields)
        if event == "phase":
            self.phase = fields["key"]
            if self.phase not in self.phases:
                self.phases.append(self.phase)
        self.inputs.write(json.dumps(item) + "\n")
        self.inputs.flush()

    def xdo(self, *args: str) -> None:
        subprocess.run(["xdotool", *args], env=self.env, check=True)

    def sleep(self, ms: int) -> None:
        deadline = time.monotonic() + ms / 1000.0
        while True:
            if self.wine and self.wine.poll() is not None:
                raise RuntimeError(f"wine exited with code {self.wine.returncode}; see logs/wine.log")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            time.sleep(min(0.25, remaining))

    def press(self, keys: tuple[str, ...], ms: int) -> None:
        """Hold the keys for `ms`, pressing them in order and releasing them in reverse.

        A key written `name@delay` goes down `delay` ms after the first one, so
        `w+x@1000` presses x while w has been held for a second.
        """
        started: dict[str, int] = {}
        begin = time.monotonic()
        names = []
        for token in keys:
            key, delay = _key_delay(token)
            names.append(key)
            self.sleep(max(0, delay - int((time.monotonic() - begin) * 1000)))
            self.xdo("keydown", key)
            started[key] = mono_ms()
            self.record("down", key=_key_name(key))
        self.sleep(max(0, ms - int((time.monotonic() - begin) * 1000)))
        for key in reversed(names):
            self.xdo("keyup", key)
            self.record("up", key=_key_name(key), duration_ms=mono_ms() - started[key])

    def mouse(self, button: int, ms: int) -> None:
        name = BUTTONS[button]
        self.xdo("mousedown", str(button))
        began = mono_ms()
        self.record("mouse_down", key=name)
        self.sleep(ms)
        self.xdo("mouseup", str(button))
        self.record("mouse_up", key=name, duration_ms=mono_ms() - began)

    def screen_to(self, png: Path) -> None:
        subprocess.run(["import", "-silent", "-window", "root", str(png)], env=self.env, check=True)

    def shot(self, name: str) -> None:
        png = self.out / "screenshots" / f"{name}.png"
        ppm = self.out / "frames_ppm" / f"{name}.ppm"
        self.record("screenshot", name=png.name)
        self.screen_to(png)
        subprocess.run(["convert", str(png), "-depth", "8", str(ppm)], check=True)

    def run(self, steps: list[Step]) -> None:
        for step in steps:
            log(step.text)
            if step.verb == "wait":
                self.sleep(step.args[0])
            elif step.verb == "key":
                self.press((step.args[0],), TAP_MS)
            elif step.verb == "hold":
                self.press(*step.args)
            elif step.verb == "click":
                self.mouse(step.args[0], TAP_MS)
            elif step.verb == "mousehold":
                self.mouse(*step.args)
            elif step.verb == "shot":
                self.shot(step.args[0])
            elif step.verb == "phase":
                self.record("phase", key=step.args[0])

    # --- artifacts -------------------------------------------------------

    def finalize(self, route_path: Path) -> None:
        shutil.copyfile(route_path, self.out / "route.txt")
        # Wine names the user directory after the unix user (`id -un`).
        user = pwd.getpwuid(os.getuid()).pw_name
        saves = self.prefix / "drive_c" / "users" / user / "Documents" / "Blood Omen 2"
        if saves.is_dir():
            shutil.copytree(saves, self.out / "saves", dirs_exist_ok=True)
        wine_version = subprocess.run(
            ["wine", "--version"], env=self.env, capture_output=True, text=True
        ).stdout.strip()
        manifest = {
            "capture_id": self.out.name,
            "created_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "source": "wine",
            "bo2_sha256": self.bo2_sha256,
            "patch_applied": False,
            "wine_version": wine_version,
            "renderer": "wined3d/llvmpipe",
            "resolution": f"{WIDTH}x{HEIGHT}",
            "video_mode": "800x600 D3DFMT_X8R8G8B8, fullscreen (set by wine_setup.sh)",
            "route": self.route_name,
        }
        self.write_state(manifest)
        (self.out / "game_manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        notes = notes_text(manifest, self.phases, self.video_start_ms)
        (self.out / "notes.md").write_text(notes, encoding="utf-8")
        write_sums(self.out)

    def write_state(self, manifest: dict) -> None:
        """Write `state.csv` and the manifest summary, including the rows sampled before an error."""
        sampler = self.sampler
        if sampler is None:
            return  # sampling disabled, or the memory could not be opened
        if sampler.instances is not None:
            (self.out / "instances.json").write_text(json.dumps(sampler.instances, indent=1), encoding="utf-8")
            manifest["instances"] = {"phase": sampler.dump_phase, "count": len(sampler.instances)}
        rows = sampler.rows
        if not rows:
            if sampler.error is None:
                log("warning: Kain was never found; no state.csv")
            return  # an error was already logged by the sampler
        with (self.out / "state.csv").open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, STATE_COLUMNS, lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
        span_ms = rows[-1]["mono_ms"] - rows[0]["mono_ms"]
        state = manifest["state"] = {
            "rows": len(rows),
            "period_ms": round(SAMPLE_PERIOD_S * 1000),
            "mean_period_ms": round(span_ms / (len(rows) - 1), 2) if len(rows) > 1 else None,
            "kain_instance": f"{sampler.found.instance:#x}",
            "kain_behavior": f"{sampler.found.behavior:#x}",
            "follow_camera": f"{sampler.camera_found:#x}" if sampler.camera_found else None,
            "camera_rows": sum(1 for row in rows if "cam_x" in row),
            "offsets": "docs/ORACLE_WINE.md, Game state",
        }
        if sampler.stopped:
            state["stopped"] = sampler.stopped


def _key_name(keysym: str) -> str:
    name = keysym.lower()
    return KEY_NAMES.get(name, name)


def state_notes(state: dict | None) -> list[str]:
    if state is None:
        return ["- No `state.csv`: sampling was disabled, failed or never found Kain."]
    notes = [
        f"- `state.csv` is sampled from the game's memory every {state['period_ms']} ms: Kain's "
        "position, yaw, velocity, health and lore, and the follow camera's eye and heading. "
        f"{state['camera_rows']} of {state['rows']} rows have a camera pose"
        + ("." if state["camera_rows"] == state["rows"] else "; `cam_*` is empty in the rest.")
        + " Columns that are not sampled stay empty.",
    ]
    if "stopped" in state:
        notes.append(f"- Sampling stopped early ({state['stopped']}); later rows are missing.")
    return notes


def notes_text(manifest: dict, phases: list[str], video_start_ms: int) -> str:
    return "\n".join(
        [
            "# Wine oracle capture - Blood Omen 2 PC (Spanish GoldMaster)",
            "",
            f"- Capture: `{manifest['capture_id']}`",
            f"- Date: {manifest['created_utc']}",
            f"- bo2.exe SHA-256: `{manifest['bo2_sha256']}` (reference build)",
            "- Patch: none; pristine executable.",
            f"- Runtime: {manifest['wine_version']}, {manifest['renderer']}, "
            f"Xvfb {manifest['resolution']}x24 (32-bit X8R8G8B8 mode).",
            f"- Route: `{manifest['route']}` (copied as `route.txt`).",
            f"- Input phases: {', '.join(phases)}",
            f"- Video: `video.mkv`, x11grab {manifest['resolution']} at 30 fps; "
            f"started at epoch {video_start_ms} ms (about 0.3 s uncertainty).",
            "",
            "## Limits",
            "- No D3D hook: `camera_d3d8.jsonl` is not produced.",
            *state_notes(manifest.get("state")),
            "- llvmpipe frame timing is not comparable with the Windows capture; compare frame content.",
            "- Input events are stamped when xdotool returns, not when the game reads them.",
            "",
        ]
    )


def write_sums(root: Path) -> None:
    lines = []
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS":
            lines.append(f"{sha256_file(path)}  {path.relative_to(root).as_posix()}")
    (root / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Capture one scripted Wine run of bo2.exe")
    parser.add_argument("route", help="route file, e.g. tools/oracle/routes/sl01_basic.txt")
    parser.add_argument("--out", help="output directory (default external/captures/wine_YYYYMMDD_HHMM)")
    parser.add_argument("--boot-wait", type=float, metavar="SECONDS",
                        help="wait this long for the main menu instead of checking for the menu")
    parser.add_argument("--no-state", action="store_true",
                        help="do not sample the game's memory into state.csv")
    parser.add_argument("--dump-instances", metavar="PHASE",
                        help="write instances.json: every game instance with its draw fields, "
                             "read once when the route enters PHASE")
    args = parser.parse_args(argv)

    if args.dump_instances and args.no_state:
        print("wine_capture: --dump-instances needs the state sampler; drop --no-state", file=sys.stderr)
        return 2
    route_path = Path(args.route)
    try:
        steps = parse_route(route_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        print(f"wine_capture: {error}", file=sys.stderr)
        return 2
    missing = [tool for tool in TOOLS if shutil.which(tool) is None]
    if missing:
        print(f"wine_capture: missing tools: {', '.join(missing)} (see docs/SETUP.md)", file=sys.stderr)
        return 2
    prefix = Path(os.environ.get("WINEPREFIX", DEFAULT_PREFIX))
    exe = prefix / "drive_c" / "BO2Oracle" / "bo2.exe"
    if not exe.is_file():
        print(f"wine_capture: no game in {prefix}; run scripts/oracle/wine_setup.sh", file=sys.stderr)
        return 2
    digest = sha256_file(exe)
    if digest != REFERENCE_BO2_SHA256:
        print(f"wine_capture: {exe} is not the reference bo2.exe ({digest})", file=sys.stderr)
        return 2
    out = Path(args.out) if args.out else CAPTURES / datetime.now().strftime("wine_%Y%m%d_%H%M")
    if out.exists() and any(out.iterdir()):
        print(f"wine_capture: {out} is not empty", file=sys.stderr)
        return 2

    for number in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(number, lambda received, _frame: sys.exit(128 + received))
    capture = Capture(out, prefix, route_path.name, digest, sample_state=not args.no_state,
                      dump_phase=args.dump_instances)
    status = 0
    try:
        capture.start(args.boot_wait)
        capture.start_state()
        capture.run(steps)
    except (RuntimeError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        print(f"wine_capture: {error}", file=sys.stderr)
        status = 1
    except KeyboardInterrupt:
        print("wine_capture: interrupted", file=sys.stderr)
        status = 130
    finally:
        capture.stop()
    for problem in capture.problems:
        print(f"wine_capture: {problem}", file=sys.stderr)
        status = status or 1
    if status:
        return status
    capture.finalize(route_path)
    log(f"done: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
