#!/usr/bin/env python3
"""Align native Windows oracle inputs with camera frames and state rows.

The camera hook records the frame number and the monotonic/wall timestamp of
every `present` call. This tool maps each `inputs.jsonl` event to the nearest
captured frame so a route can be replayed and compared by frame or tick. It
also summarizes the state columns and the phases recorded by the capture
runner.

A Wine capture has no camera data. Its inputs stay unaligned and the report
lists the inputs and phases with their time offset in seconds.
"""
from __future__ import annotations

import argparse
import bisect
import csv
import json
from pathlib import Path
from statistics import mean, median


def load_inputs(path: Path) -> tuple[list[dict], int]:
    events: list[dict] = []
    invalid = 0
    if not path.is_file():
        return events, invalid
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                invalid += 1
                continue
            if isinstance(event, dict):
                events.append(event)
            else:
                invalid += 1
    return events, invalid


def load_presents(path: Path) -> tuple[list[dict], int]:
    presents: list[dict] = []
    invalid = 0
    if not path.is_file():
        return presents, invalid
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                invalid += 1
                continue
            if not isinstance(event, dict):
                invalid += 1
                continue
            if event.get("type") != "present":
                continue
            presents.append(
                {
                    "frame": event.get("frame"),
                    "ts": event.get("ts"),
                    "dt": event.get("dt"),
                }
            )
    return presents, invalid


def load_state(path: Path) -> tuple[list[dict], list[str]]:
    rows: list[dict] = []
    fields: list[str] = []
    if not path.is_file():
        return rows, fields
    with path.open(newline="", encoding="utf-8", errors="replace") as handle:
        reader = csv.DictReader(handle)
        fields = list(reader.fieldnames or [])
        rows = [dict(row) for row in reader]
    return rows, fields


class FrameIndex:
    def __init__(self, presents: list[dict], tolerance_ms: float = 250.0):
        self.presents = sorted(
            (item for item in presents if isinstance(item.get("ts"), (int, float))),
            key=lambda item: item["ts"],
        )
        self.stamps = [item["ts"] for item in self.presents]
        self.tolerance_ms = tolerance_ms

    def nearest(self, stamp):
        if not self.stamps or not isinstance(stamp, (int, float)):
            return None
        if stamp < self.stamps[0] - self.tolerance_ms:
            return None
        if stamp > self.stamps[-1] + self.tolerance_ms:
            return None
        index = bisect.bisect_left(self.stamps, stamp)
        if index == 0:
            return self.presents[0]
        if index >= len(self.presents):
            return self.presents[-1]
        before = self.presents[index - 1]
        after = self.presents[index]
        if stamp - before["ts"] <= after["ts"] - stamp:
            return before
        return after

    def coverage(self) -> dict:
        if not self.stamps:
            return {}
        return {
            "first_ts": self.stamps[0],
            "last_ts": self.stamps[-1],
            "span_ms": self.stamps[-1] - self.stamps[0],
        }


def frame_stats(presents: list[dict]) -> dict:
    stamps = sorted(
        item["ts"] for item in presents if isinstance(item.get("ts"), (int, float))
    )
    intervals = [b - a for a, b in zip(stamps, stamps[1:]) if b > a]
    result = {
        "count": len(presents),
        "duration_ms": (stamps[-1] - stamps[0]) if len(stamps) > 1 else 0,
    }
    if intervals:
        ordered = sorted(intervals)
        average = mean(intervals)
        middle = median(intervals)
        result.update(
            {
                "mean_ms": round(average, 3),
                "median_ms": round(middle, 3),
                "p95_ms": ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))],
                "fps_mean": round(1000.0 / average, 2) if average else 0.0,
                "fps_median": round(1000.0 / middle, 2) if middle else 0.0,
            }
        )
    return result


def annotate_inputs(events: list[dict], index: FrameIndex) -> list[dict]:
    annotated: list[dict] = []
    for event in events:
        item = dict(event)
        present = index.nearest(event.get("epoch_ms"))
        item["frame"] = present.get("frame") if present else None
        annotated.append(item)
    return annotated


def phase_summary(events: list[dict]) -> dict:
    phases: dict[str, dict] = {}
    for event in events:
        name = event.get("phase")
        if not name and event.get("event") == "phase":
            name = event.get("key")
        if not name:
            continue
        item = phases.setdefault(
            str(name),
            {
                "events": 0,
                "first_frame": event.get("frame"),
                "first_epoch_ms": event.get("epoch_ms"),
            },
        )
        item["events"] += 1
        if item["first_frame"] is None and event.get("frame") is not None:
            item["first_frame"] = event.get("frame")
    return phases


def summarize(root: Path) -> dict:
    inputs, invalid_inputs = load_inputs(root / "inputs.jsonl")
    presents, invalid_camera = load_presents(root / "camera_d3d8.jsonl")
    state_rows, state_fields = load_state(root / "state.csv")
    index = FrameIndex(presents)
    annotated = annotate_inputs(inputs, index)
    aligned = sum(1 for event in annotated if event.get("frame") is not None)
    state_frames = [
        int(row["frame"])
        for row in state_rows
        if str(row.get("frame", "")).strip().lstrip("-").isdigit()
    ]
    return {
        "capture": root.name,
        "frames": frame_stats(presents),
        "camera_coverage": index.coverage(),
        "inputs": {
            "count": len(annotated),
            "first_epoch_ms": min(
                (e["epoch_ms"] for e in annotated if isinstance(e.get("epoch_ms"), (int, float))),
                default=None,
            ),
            "invalid": invalid_inputs,
            "aligned": aligned,
            "unaligned": len(annotated) - aligned,
            "events": annotated,
        },
        "phases": phase_summary(annotated),
        "camera_invalid": invalid_camera,
        "state": {
            "rows": len(state_rows),
            "fields": state_fields,
            "first_frame": min(state_frames) if state_frames else None,
            "last_frame": max(state_frames) if state_frames else None,
        },
    }


def print_summary(report: dict) -> None:
    frames = report["frames"]
    inputs = report["inputs"]
    print(f"capture: {report['capture']}")
    if frames["count"]:
        print(
            f"frames:  {frames['count']} present events, "
            f"{frames['duration_ms']} ms"
        )
    else:
        print("frames:  no camera data")
    if "fps_mean" in frames:
        print(
            f"present: {frames['fps_mean']} fps mean, "
            f"{frames['fps_median']} fps median, p95={frames['p95_ms']} ms"
        )
    line = f"inputs:  {inputs['count']} events, {inputs['invalid']} invalid"
    if frames["count"]:
        line += f", {inputs['aligned']} aligned, {inputs['unaligned']} outside camera"
    print(line)
    for name, phase in report["phases"].items():
        if phase["first_frame"] is not None:
            where = f"frame {phase['first_frame']}"
        elif phase["first_epoch_ms"] is not None and inputs["first_epoch_ms"] is not None:
            where = f"+{(phase['first_epoch_ms'] - inputs['first_epoch_ms']) / 1000:.1f} s"
        else:
            where = "unknown time"
        print(f"phase:   {name} at {where} ({phase['events']} events)")
    state = report["state"]
    if state["rows"]:
        print(
            f"state:   {state['rows']} rows, frames "
            f"{state['first_frame']}..{state['last_frame']}"
        )
    else:
        print("state:   missing")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Align oracle capture inputs with camera frames")
    parser.add_argument("capture", help="downloaded capture directory")
    parser.add_argument("--json", action="store_true", help="print the full JSON report")
    parser.add_argument("--out", help="write the full JSON report to this path")
    args = parser.parse_args(argv)

    root = Path(args.capture)
    if not root.is_dir():
        print(f"not a directory: {root}")
        return 2
    report = summarize(root)
    if args.out:
        output = Path(args.out)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"wrote {output}")
    if args.json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    elif not args.out:
        print_summary(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
