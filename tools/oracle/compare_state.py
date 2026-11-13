#!/usr/bin/env python3
"""Compare the port replay state with the oracle state.csv, phase by phase.

The oracle rows are sampler reads, not game ticks, and the input timestamps
are when xdotool returned, so a one- or two-row offset is expected. The tool
searches that offset and reports the residual after the best alignment; a
growing residual is a real state divergence, a small one at a nonzero offset
is timing.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from pathlib import Path


def load_state(path: Path) -> list[dict]:
    with path.open(newline="", encoding="utf-8", errors="replace") as handle:
        return list(csv.DictReader(handle))


def number(row: dict, key: str) -> float | None:
    value = row.get(key)
    if value in (None, ""):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def distance(a: dict, b: dict, prefix: str) -> float | None:
    ax, ay, az = (number(a, f"{prefix}{axis}") for axis in ("x", "y", "z"))
    bx, by, bz = (number(b, f"{prefix}{axis}") for axis in ("x", "y", "z"))
    if ax is None or ay is None or az is None or bx is None or by is None or bz is None:
        return None
    return math.sqrt((ax - bx) ** 2 + (ay - by) ** 2 + (az - bz) ** 2)


def wrap_degrees(value: float) -> float:
    return math.remainder(value, 360.0)


def compare_states(
    oracle_rows: list[dict], port_rows: list[dict], tolerance: float = 1.0, shift: int = 0
) -> dict:
    phases: dict[str, dict] = {}
    first_divergence = None
    matched = 0
    for index, oracle in enumerate(oracle_rows):
        port_index = index + shift
        if port_index < 0 or port_index >= len(port_rows):
            continue
        port = port_rows[port_index]
        matched += 1
        phase = oracle.get("phase") or ""
        item = phases.setdefault(
            phase,
            {
                "rows": 0,
                "player_max": 0.0,
                "player_sum": 0.0,
                "yaw_max": 0.0,
                "camera_max": 0.0,
                "camera_rows": 0,
                "velocity_max": 0.0,
                "velocity_rows": 0,
                "anim_rows": 0,
                "anim_match": 0,
                "player_start": None,
                "player_end": None,
            },
        )
        item["rows"] += 1
        player_distance = distance(oracle, port, "player_")
        if player_distance is not None:
            item["player_max"] = max(item["player_max"], player_distance)
            item["player_sum"] += player_distance
            if item["player_start"] is None:
                item["player_start"] = {
                    "oracle": [number(oracle, f"player_{axis}") for axis in ("x", "y", "z")],
                    "port": [number(port, f"player_{axis}") for axis in ("x", "y", "z")],
                    "error": player_distance,
                }
            item["player_end"] = {
                "oracle": [number(oracle, f"player_{axis}") for axis in ("x", "y", "z")],
                "port": [number(port, f"player_{axis}") for axis in ("x", "y", "z")],
                "error": player_distance,
            }
            if first_divergence is None and player_distance > tolerance:
                first_divergence = {
                    "frame": oracle.get("frame"),
                    "phase": phase,
                    "error": round(player_distance, 3),
                    "oracle": item["player_end"]["oracle"],
                    "port": item["player_end"]["port"],
                }
        oracle_yaw = number(oracle, "player_yaw_deg")
        port_yaw = number(port, "player_yaw_deg")
        if oracle_yaw is not None and port_yaw is not None:
            item["yaw_max"] = max(item["yaw_max"], abs(wrap_degrees(oracle_yaw - port_yaw)))
        camera_distance = distance(oracle, port, "cam_")
        if camera_distance is not None:
            item["camera_max"] = max(item["camera_max"], camera_distance)
            item["camera_rows"] += 1
        velocity_distance = distance(oracle, port, "vel_")
        if velocity_distance is not None:
            item["velocity_max"] = max(item["velocity_max"], velocity_distance)
            item["velocity_rows"] += 1
        # `anim` is the animation state index (KainState+0x458); a capture
        # without it leaves the column empty and the phase is not scored.
        oracle_anim = number(oracle, "anim")
        port_anim = number(port, "anim")
        if oracle_anim is not None and port_anim is not None:
            item["anim_rows"] += 1
            if oracle_anim == port_anim:
                item["anim_match"] += 1
    for item in phases.values():
        if item["rows"]:
            item["player_mean"] = round(item["player_sum"] / item["rows"], 3)
        else:
            item["player_mean"] = 0.0
        item["player_max"] = round(item["player_max"], 3)
        item["yaw_max"] = round(item["yaw_max"], 3)
        item["camera_max"] = round(item["camera_max"], 3)
        item["velocity_max"] = round(item["velocity_max"], 3)
        del item["player_sum"]
    return {
        "shift": shift,
        "matched_rows": matched,
        "oracle_rows": len(oracle_rows),
        "port_rows": len(port_rows),
        "tolerance": tolerance,
        "first_divergence": first_divergence,
        "phases": phases,
    }


def report_score(report: dict) -> tuple[float, float, int]:
    phases = report["phases"].values()
    mean = sum(item["player_mean"] for item in phases)
    maximum = sum(item["player_max"] for item in phases)
    return (mean, maximum, abs(report["shift"]))


def best_report(oracle_rows: list[dict], port_rows: list[dict], max_shift: int,
                tolerance: float) -> tuple[dict, dict[int, dict]]:
    reports = {
        shift: compare_states(oracle_rows, port_rows, tolerance, shift)
        for shift in range(-max_shift, max_shift + 1)
    }
    best_shift = min(reports, key=lambda shift: report_score(reports[shift]))
    report = reports[best_shift]
    report["best_shift"] = best_shift
    report["shifts"] = {
        shift: {
            "player_mean": round(
                sum(item["player_mean"] for item in item_report["phases"].values()), 3
            ),
            "player_max": round(
                sum(item["player_max"] for item in item_report["phases"].values()), 3
            ),
        }
        for shift, item_report in reports.items()
    }
    return report, reports


def print_report(report: dict) -> None:
    print(
        f"rows: oracle {report['oracle_rows']}, port {report['port_rows']}, "
        f"matched {report['matched_rows']}; best shift {report['best_shift']} rows"
    )
    print(f"{'phase':14} {'rows':>5} {'player max':>11} {'player mean':>12} "
          f"{'yaw max':>8} {'cam max':>9} {'vel max':>9} {'anim':>10}")
    anim_rows = 0
    anim_match = 0
    for phase, item in report["phases"].items():
        camera = f"{item['camera_max']:.3f}" if item["camera_rows"] else "-"
        velocity = f"{item['velocity_max']:.3f}" if item["velocity_rows"] else "-"
        anim = f"{item['anim_match']}/{item['anim_rows']}" if item["anim_rows"] else "-"
        anim_rows += item["anim_rows"]
        anim_match += item["anim_match"]
        print(
            f"{phase:14} {item['rows']:5d} {item['player_max']:11.3f} "
            f"{item['player_mean']:12.3f} {item['yaw_max']:8.3f} "
            f"{camera:>9} {velocity:>9} {anim:>10}"
        )
    if anim_rows:
        print(f"animation state: {anim_match}/{anim_rows} rows match "
              f"({100.0 * anim_match / anim_rows:.1f}%)")
    divergence = report["first_divergence"]
    if divergence is None:
        print(f"no player divergence above {report['tolerance']} u")
    else:
        print(
            f"first divergence: frame {divergence['frame']} ({divergence['phase']}) "
            f"{divergence['error']} u"
        )
    print("shift search: " + ", ".join(
        f"{shift}: mean {data['player_mean']:.3f}/max {data['player_max']:.3f}"
        for shift, data in sorted(report["shifts"].items())
    ))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Compare oracle and port state CSVs")
    parser.add_argument("oracle", help="oracle state.csv")
    parser.add_argument("port", help="port state.csv from bo2nx --replay")
    parser.add_argument("--tolerance", type=float, default=1.0, help="player error in u")
    parser.add_argument("--max-shift", type=int, default=2, help="rows to search around 0")
    parser.add_argument("--json", action="store_true", help="print the full JSON report")
    args = parser.parse_args(argv)
    try:
        oracle = load_state(Path(args.oracle))
        port = load_state(Path(args.port))
    except OSError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    report, _ = best_report(oracle, port, max(0, args.max_shift), args.tolerance)
    if args.json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    else:
        print_report(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
