#!/usr/bin/env python3
"""Convert an oracle capture into the per-tick input CSV for bo2nx --replay.

The oracle state.csv samples the player every 20 ms. This tool reads the same
timeline and the capture's inputs.jsonl, then writes one row per state sample
with the held keys mapped to the port's PlayerInput. The first row also
carries the spawn position and yaw sampled by the oracle.
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

KEYS = {"w", "s", "a", "d", "shift", "rmouse", "lmouse", "x", "space", "c"}
DOWN_EVENTS = {"down", "mouse_down"}
UP_EVENTS = {"up", "mouse_up"}
HEADER = (
    "frame",
    "dt",
    "phase",
    "move_x",
    "move_y",
    "run",
    "jump",
    "attack",
    "start_x",
    "start_y",
    "start_z",
    "start_yaw_deg",
)


def event_time(event: dict) -> float | None:
    for key in ("mono_ms", "epoch_ms"):
        value = event.get(key)
        if isinstance(value, (int, float)):
            return float(value)
    return None


def state_time(row: dict) -> float | None:
    for key in ("mono_ms", "epoch_ms"):
        value = row.get(key)
        if value in (None, ""):
            continue
        try:
            return float(value)
        except (TypeError, ValueError):
            continue
    return None


def load_inputs(path: Path) -> list[dict]:
    events: list[dict] = []
    if not path.is_file():
        return events
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(event, dict):
                events.append(event)
    return events


def build_replay(capture: Path) -> list[dict]:
    state_path = capture / "state.csv"
    inputs_path = capture / "inputs.jsonl"
    if not state_path.is_file():
        raise FileNotFoundError(f"missing {state_path}")
    with state_path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError(f"{state_path} has no rows")
    events = []
    for event in load_inputs(inputs_path):
        key = event.get("key")
        kind = event.get("event")
        if key not in KEYS or kind not in DOWN_EVENTS | UP_EVENTS:
            continue
        stamp = event_time(event)
        if stamp is None:
            continue
        events.append((stamp, str(key), kind in DOWN_EVENTS))
    events.sort(key=lambda item: item[0])

    first = rows[0]
    start_x = float(first.get("player_x") or 0.0)
    start_y = float(first.get("player_y") or 0.0)
    start_z = float(first.get("player_z") or 0.0)
    start_yaw = float(first.get("player_yaw_deg") or 0.0)
    held: set[str] = set()
    event_index = 0
    previous_time: float | None = None
    replay: list[dict] = []
    for index, row in enumerate(rows):
        stamp = state_time(row)
        if stamp is None:
            continue
        while event_index < len(events) and events[event_index][0] <= stamp:
            _, key, down = events[event_index]
            if down:
                held.add(key)
            else:
                held.discard(key)
            event_index += 1
        dt = 0.0 if previous_time is None else max(0.0, (stamp - previous_time) / 1000.0)
        previous_time = stamp
        move_x = (1 if "d" in held else 0) - (1 if "a" in held else 0)
        move_y = (1 if "w" in held else 0) - (1 if "s" in held else 0)
        replay.append(
            {
                "frame": row.get("frame") or str(index),
                "dt": f"{dt:.6f}",
                "phase": row.get("phase") or "",
                "move_x": move_x,
                "move_y": move_y,
                "run": 1 if "shift" in held else 0,
                "jump": 1 if ("rmouse" in held or "x" in held) else 0,
                "attack": 1 if ("lmouse" in held or "space" in held or "c" in held) else 0,
                "start_x": f"{start_x:.6f}",
                "start_y": f"{start_y:.6f}",
                "start_z": f"{start_z:.6f}",
                "start_yaw_deg": f"{start_yaw:.6f}",
            }
        )
    return replay


def write_replay(capture: Path, out: Path) -> int:
    replay = build_replay(capture)
    if not replay:
        raise ValueError(f"{capture} produced no replay ticks")
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=HEADER)
        writer.writeheader()
        writer.writerows(replay)
    return len(replay)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Prepare an oracle replay input CSV")
    parser.add_argument("capture", help="capture directory with state.csv and inputs.jsonl")
    parser.add_argument("--out", default="replay_inputs.csv", help="output CSV")
    args = parser.parse_args(argv)
    try:
        count = write_replay(Path(args.capture), Path(args.out))
    except (FileNotFoundError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"{count} ticks -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
