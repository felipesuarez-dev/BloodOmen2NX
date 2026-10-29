#!/usr/bin/env python3
"""Validate an oracle capture.

The tool is read-only: it inspects a capture directory and reports whether
the artifacts needed for a real oracle comparison are present and coherent.
It never contacts the Windows host.

A capture whose `game_manifest.json` has `"source": "wine"` comes from the
Wine harness (`wine_capture.py`). That harness has no D3D hook, so it does
not produce `camera_d3d8.jsonl`, and `state.csv` exists only when it sampled
the game's memory; neither is required and the camera coverage checks are
skipped. A `state.csv` that is present is checked like any other, and its
empty `cam_*` columns are not an error. Windows captures are unchanged.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path
from statistics import mean, median

from oracle_client import safe_relative

REFERENCE_BO2_SHA256 = (
    "91758398a17de9c9f974a8a102762a534fb523d40fa6868a5e5a4f0483b2753c"
)
CAMERA_FIELDS = {
    "frame",
    "epoch_ms",
    "mono_ms",
    "cam_x",
    "cam_y",
    "cam_z",
    "cam_yaw_deg",
    "cam_pitch_deg",
}
PLAYER_FIELDS = {
    "player_x",
    "player_y",
    "player_z",
    "vel_x",
    "vel_y",
    "vel_z",
    "grounded",
    "health",
    "mana",
    "weapon",
    "anim",
    "tick",
}
REQUIRED_FILES = (
    "game_manifest.json",
    "notes.md",
    "SHA256SUMS",
    "inputs.jsonl",
    "camera_d3d8.jsonl",
)
FINAL_FILES = ("state.csv", "video.mkv")
WINE_SOURCE = "wine"
WINE_OPTIONAL_FILES = ("camera_d3d8.jsonl", "state.csv")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def load_manifest(root: Path) -> dict:
    path = root / "game_manifest.json"
    if not path.is_file():
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return value if isinstance(value, dict) else {}


def manifest_hash(manifest: dict) -> str:
    for key in ("bo2_sha256", "bo2_exe_sha256", "exe_sha256"):
        value = manifest.get(key)
        if isinstance(value, str) and value.strip():
            return value.strip().lower()
    game = manifest.get("game")
    if isinstance(game, dict):
        for key in ("bo2_sha256", "sha256", "exe_sha256"):
            value = game.get(key)
            if isinstance(value, str) and value.strip():
                return value.strip().lower()
    return ""


def manifest_resolution(manifest: dict) -> str:
    value = manifest.get("resolution")
    if isinstance(value, (list, tuple)) and len(value) == 2:
        return f"{value[0]}x{value[1]}"
    if isinstance(value, str) and value.strip():
        return value.strip()
    width = manifest.get("screen_width") or manifest.get("width")
    height = manifest.get("screen_height") or manifest.get("height")
    if width and height:
        return f"{width}x{height}"
    return ""


def manifest_source(manifest: dict) -> str:
    value = manifest.get("source")
    return value.strip().lower() if isinstance(value, str) and value.strip() else "windows"


def manifest_patch_applied(manifest: dict) -> bool:
    value = manifest.get("patch_applied")
    if value is None:
        return False
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"1", "true", "yes"}


def parse_sums(path: Path) -> list[tuple[str, str]]:
    entries: list[tuple[str, str]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        digest, name = parts[0].lower(), parts[1].strip()
        if name.startswith("*"):
            name = name[1:]
        entries.append((digest, name.replace("\\", "/")))
    return entries


def scan_inputs(path: Path) -> dict:
    result = {"count": 0, "invalid": 0, "first_epoch_ms": None, "last_epoch_ms": None}
    if not path.is_file():
        return result
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                result["invalid"] += 1
                continue
            result["count"] += 1
            stamp = event.get("epoch_ms")
            if isinstance(stamp, (int, float)):
                if result["first_epoch_ms"] is None or stamp < result["first_epoch_ms"]:
                    result["first_epoch_ms"] = stamp
                if result["last_epoch_ms"] is None or stamp > result["last_epoch_ms"]:
                    result["last_epoch_ms"] = stamp
    return result


def scan_camera(path: Path) -> dict:
    result = {
        "lines": 0,
        "invalid": 0,
        "types": Counter(),
        "presents": 0,
        "transform_states": set(),
        "timestamp": [],
    }
    if not path.is_file():
        return result
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                result["invalid"] += 1
                continue
            result["lines"] += 1
            kind = event.get("type")
            result["types"][kind] += 1
            if kind == "present":
                result["presents"] += 1
                stamp = event.get("ts")
                if isinstance(stamp, (int, float)):
                    result["timestamp"].append(stamp)
            elif kind == "transform":
                result["transform_states"].add(event.get("state"))
    return result


def camera_fps(camera: dict) -> dict:
    stamps = sorted(camera["timestamp"])
    intervals = [b - a for a, b in zip(stamps, stamps[1:]) if b > a]
    if not intervals:
        return {}
    ordered = sorted(intervals)
    p95 = ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))]
    average = mean(intervals)
    middle = median(intervals)
    return {
        "intervals": len(intervals),
        "mean_ms": round(average, 3),
        "median_ms": round(middle, 3),
        "p95_ms": p95,
        "fps_mean": round(1000.0 / average, 2) if average else 0.0,
        "fps_median": round(1000.0 / middle, 2) if middle else 0.0,
    }


def scan_state(path: Path) -> dict:
    result = {"rows": 0, "fields": [], "missing_camera": [], "missing_player": []}
    if not path.is_file():
        return result
    with path.open(newline="", encoding="utf-8", errors="replace") as handle:
        reader = csv.DictReader(handle)
        result["fields"] = list(reader.fieldnames or [])
        for _ in reader:
            result["rows"] += 1
    fields = set(result["fields"])
    result["missing_camera"] = sorted(CAMERA_FIELDS - fields)
    result["missing_player"] = sorted(PLAYER_FIELDS - fields)
    return result


def ppm_size(path: Path) -> tuple[int, int, int] | None:
    data = path.read_bytes()[:512]
    if not data.startswith(b"P6"):
        return None
    index = 2
    values: list[int] = []
    while len(values) < 3:
        while index < len(data) and data[index : index + 1].isspace():
            index += 1
        if index >= len(data):
            return None
        if data[index : index + 1] == b"#":
            while index < len(data) and data[index : index + 1] != b"\n":
                index += 1
            continue
        start = index
        while index < len(data) and data[index : index + 1].isdigit():
            index += 1
        if start == index:
            return None
        values.append(int(data[start:index]))
    return values[0], values[1], values[2]


def scan_frames(root: Path) -> dict:
    directory = root / "frames_ppm"
    result = {"count": 0, "invalid": 0, "sizes": []}
    if not directory.is_dir():
        return result
    for path in sorted(directory.glob("*.ppm")):
        size = ppm_size(path)
        if size is None:
            result["invalid"] += 1
            continue
        result["count"] += 1
        result["sizes"].append(size)
    return result


def scan_screenshots(root: Path) -> dict:
    directory = root / "screenshots"
    result = {"count": 0, "invalid": 0}
    if not directory.is_dir():
        return result
    for path in sorted(directory.glob("*.png")):
        if path.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
            result["invalid"] += 1
            continue
        result["count"] += 1
    return result


def scan_video(root: Path) -> dict:
    path = root / "video.mkv"
    if not path.is_file():
        return {"present": False, "bytes": 0, "mkv": False}
    head = path.read_bytes()[:4]
    return {
        "present": True,
        "bytes": path.stat().st_size,
        "mkv": head == b"\x1aE\xdf\xa3",
    }


def scan_saves(root: Path) -> int:
    directory = root / "saves"
    if not directory.is_dir():
        return 0
    return sum(1 for path in directory.rglob("*") if path.is_file())


def verify_sums(root: Path, entries: list[tuple[str, str]]) -> dict:
    result = {"entries": len(entries), "verified": 0, "missing": [], "mismatch": []}
    prefix = root.name + "/"
    for digest, name in entries:
        relative = name[len(prefix) :] if name.startswith(prefix) else name
        try:
            relative = safe_relative(relative)
        except ValueError:
            result["mismatch"].append(name)
            continue
        path = root / relative
        if not path.is_file():
            result["missing"].append(relative)
            continue
        if sha256_file(path) != digest:
            result["mismatch"].append(relative)
            continue
        result["verified"] += 1
    return result


def validate_capture(root: Path, partial: bool = False) -> dict:
    errors: list[str] = []
    warnings: list[str] = []
    manifest = load_manifest(root)
    source = manifest_source(manifest)
    wine = source == WINE_SOURCE

    required: list[str] = list(REQUIRED_FILES)
    if not partial:
        required.extend(FINAL_FILES)
    if wine:
        required = [name for name in required if name not in WINE_OPTIONAL_FILES]
    for name in required:
        if not (root / name).is_file():
            if partial and name in FINAL_FILES:
                warnings.append(f"missing final artifact: {name}")
            else:
                errors.append(f"missing required file: {name}")

    if not manifest:
        errors.append("game_manifest.json is empty or invalid")
    else:
        digest = manifest_hash(manifest)
        if not digest:
            errors.append("game_manifest.json has no bo2.exe hash")
        elif digest != REFERENCE_BO2_SHA256:
            errors.append(f"bo2.exe hash mismatch: {digest}")
        if manifest_patch_applied(manifest):
            errors.append("game_manifest.json says the executable is patched")
        resolution = manifest_resolution(manifest)
        if resolution and resolution != "800x600":
            warnings.append(f"capture resolution is {resolution}, expected 800x600")
        if resolution == "":
            warnings.append("game_manifest.json has no resolution")

    sums_path = root / "SHA256SUMS"
    sums = verify_sums(root, parse_sums(sums_path)) if sums_path.is_file() else {
        "entries": 0,
        "verified": 0,
        "missing": [],
        "mismatch": [],
    }
    if sums_path.is_file() and sums["entries"] == 0:
        errors.append("SHA256SUMS has no entries")
    for name in sums["mismatch"]:
        errors.append(f"SHA256SUMS mismatch or unsafe path: {name}")
    if sums["missing"]:
        warnings.append(f"SHA256SUMS lists {len(sums['missing'])} file(s) not downloaded")

    inputs = scan_inputs(root / "inputs.jsonl")
    if (root / "inputs.jsonl").is_file():
        if inputs["count"] == 0:
            errors.append("inputs.jsonl has no events")
        if inputs["invalid"]:
            warnings.append(f"inputs.jsonl has {inputs['invalid']} invalid line(s)")

    camera = scan_camera(root / "camera_d3d8.jsonl")
    if (root / "camera_d3d8.jsonl").is_file():
        if camera["lines"] == 0:
            errors.append("camera_d3d8.jsonl has no events")
        if camera["presents"] == 0:
            errors.append("camera_d3d8.jsonl has no present events")
        if camera["invalid"]:
            warnings.append(f"camera_d3d8.jsonl has {camera['invalid']} invalid line(s)")
    fps = camera_fps(camera)
    camera_start = min(camera["timestamp"]) if camera["timestamp"] else None
    camera_end = max(camera["timestamp"]) if camera["timestamp"] else None
    if (
        not wine
        and camera_start is not None
        and inputs["first_epoch_ms"] is not None
        and inputs["first_epoch_ms"] < camera_start - 1000
    ):
        warnings.append("inputs start before the first camera present")
    if (
        not wine
        and camera_end is not None
        and inputs["last_epoch_ms"] is not None
        and inputs["last_epoch_ms"] > camera_end + 1000
    ):
        gap = round((inputs["last_epoch_ms"] - camera_end) / 1000.0, 1)
        warnings.append(
            f"inputs continue {gap} s after the last camera present; "
            "gameplay may not be covered by camera data"
        )

    state = scan_state(root / "state.csv")
    if (root / "state.csv").is_file():
        if state["rows"] == 0:
            errors.append("state.csv has no rows")
        if state["missing_camera"]:
            errors.append(
                "state.csv is missing camera columns: "
                + ", ".join(state["missing_camera"])
            )
        if state["missing_player"]:
            warnings.append(
                "state.csv has no verified player columns: "
                + ", ".join(state["missing_player"])
            )

    frames = scan_frames(root)
    screenshots = scan_screenshots(root)
    video = scan_video(root)
    saves = scan_saves(root)

    if screenshots["count"] == 0:
        errors.append("screenshots/ has no valid PNG files")
    if screenshots["invalid"]:
        warnings.append(f"screenshots/ has {screenshots['invalid']} invalid file(s)")
    if frames["count"] == 0:
        if partial:
            warnings.append("frames_ppm/ has no valid PPM frames")
        else:
            errors.append("frames_ppm/ has no valid PPM frames")
    if frames["invalid"]:
        warnings.append(f"frames_ppm/ has {frames['invalid']} invalid file(s)")
    if any(size[0] != 800 or size[1] != 600 for size in frames["sizes"]):
        warnings.append("some PPM frames are not 800x600")
    if video["present"] and not video["mkv"]:
        warnings.append("video.mkv does not start with an EBML signature")
    if saves == 0:
        if partial:
            warnings.append("saves/ has no save files")
        else:
            errors.append("saves/ has no save files")

    report = {
        "capture": root.name,
        "ok": not errors,
        "partial": partial,
        "errors": errors,
        "warnings": warnings,
        "manifest": {
            "source": source,
            "bo2_sha256": manifest_hash(manifest),
            "resolution": manifest_resolution(manifest),
            "patch_applied": manifest_patch_applied(manifest),
        },
        "counts": {
            "inputs": inputs["count"],
            "inputs_invalid": inputs["invalid"],
            "state_rows": state["rows"],
            "camera_lines": camera["lines"],
            "camera_invalid": camera["invalid"],
            "presents": camera["presents"],
            "ppm_frames": frames["count"],
            "screenshots": screenshots["count"],
            "saves": saves,
            "video_bytes": video["bytes"],
        },
        "state_fields": state["fields"],
        "transform_states": sorted(camera["transform_states"], key=str),
        "fps": fps,
        "camera_coverage": {
            "first_ts": camera_start,
            "last_ts": camera_end,
            "presents": camera["presents"],
        },
        "sha256sums": sums,
    }
    return report


def print_report(report: dict) -> None:
    print(f"capture: {report['capture']}")
    print(f"ok:      {report['ok']}")
    manifest = report["manifest"]
    print(f"source:  {manifest['source']}")
    print(f"bo2:     {manifest['bo2_sha256'] or '<missing>'}")
    print(f"mode:    {'partial' if report['partial'] else 'final'}")
    print(f"res:     {manifest['resolution'] or '<missing>'}")
    counts = report["counts"]
    print(
        "counts:  "
        f"inputs={counts['inputs']} state={counts['state_rows']} "
        f"camera={counts['camera_lines']} present={counts['presents']} "
        f"ppm={counts['ppm_frames']} png={counts['screenshots']} "
        f"saves={counts['saves']} video={counts['video_bytes']}"
    )
    if report["fps"]:
        fps = report["fps"]
        print(
            f"present: {fps['fps_mean']} fps mean, {fps['fps_median']} fps median, "
            f"p95={fps['p95_ms']} ms"
        )
    for warning in report["warnings"]:
        print(f"warning: {warning}")
    for error in report["errors"]:
        print(f"error:   {error}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate an oracle capture (Windows or Wine)")
    parser.add_argument("capture", help="downloaded capture directory")
    parser.add_argument("--partial", action="store_true", help="allow state/frames/video/saves to be missing")
    parser.add_argument("--json", action="store_true", help="print the full JSON report")
    args = parser.parse_args(argv)

    root = Path(args.capture)
    if not root.is_dir():
        print(f"not a directory: {root}", file=sys.stderr)
        return 2
    report = validate_capture(root, partial=args.partial)
    if args.json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    else:
        print_report(report)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
