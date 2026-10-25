#!/usr/bin/env python3
"""Regenerate the README progress block.

Inputs:
  re/FUNCTION_MAP.csv       address,name,subsystem,status,notes
  tools/gen/progress_data.py

Statuses counted as done: translated, verified.
Statuses excluded from the denominator: external.
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
README = ROOT / "README.md"
FUNCTION_MAP = ROOT / "re" / "FUNCTION_MAP.csv"
START = "<!-- progress:start -->"
END = "<!-- progress:end -->"

WEIGHTS = {"functions": 0.60, "levels": 0.30, "platform": 0.10}


def load_functions() -> dict[str, int]:
    counts = {"todo": 0, "wip": 0, "translated": 0, "verified": 0, "external": 0}
    if not FUNCTION_MAP.exists():
        return counts
    with FUNCTION_MAP.open(newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            status = (row.get("status") or "").strip().lower()
            if status in counts:
                counts[status] += 1
    return counts


def load_progress_data():
    sys.path.insert(0, str(ROOT / "tools" / "gen"))
    import progress_data  # noqa: PLC0415

    return progress_data.LEVELS, progress_data.PLATFORM_FEATURES


def pct(score: float) -> str:
    return f"{score * 100:.2f}%"


def build_block() -> str:
    counts = load_functions()
    levels, features = load_progress_data()

    in_scope = sum(counts.values()) - counts["external"]
    done = counts["translated"] + counts["verified"]
    fn_score = done / in_scope if in_scope else 0.0

    total_levels = len(levels)
    done_levels = sum(1 for _, ok in levels if ok)
    level_score = done_levels / total_levels if total_levels else 0.0

    feature_score = 0.0
    for _, state in features:
        feature_score += {"done": 1.0, "partial": 0.5}.get(state, 0.0)
    feature_score = feature_score / len(features) if features else 0.0

    overall = (
        WEIGHTS["functions"] * fn_score
        + WEIGHTS["levels"] * level_score
        + WEIGHTS["platform"] * feature_score
    )

    level_text = f"{done_levels} / {total_levels}" if total_levels else "0 / TBD"
    feature_text = f"{sum(1 for _, s in features if s == 'done')} / {len(features)}"

    return "\n".join(
        [
            START,
            f"**Overall progress: {overall * 100:.1f}%**",
            "",
            "| Component | Weight | Progress | Score |",
            "|---|---:|---:|---:|",
            f"| Functions translated / in-scope | 60% | {done} / {in_scope} | {pct(fn_score)} |",
            f"| Levels completable | 30% | {level_text} | {pct(level_score)} |",
            f"| Platform features | 10% | {feature_text} | {pct(feature_score)} |",
            "",
            f"Function map: {in_scope + counts['external']} rows. "
            "Levels and platform features are defined in "
            "[`tools/gen/progress_data.py`](tools/gen/progress_data.py).",
            END,
        ]
    )


def main() -> int:
    text = README.read_text(encoding="utf-8")
    if START not in text or END not in text:
        print("README progress markers not found", file=sys.stderr)
        return 2
    head, rest = text.split(START, 1)
    _, tail = rest.split(END, 1)
    new_text = head + build_block() + tail
    if "--check" in sys.argv:
        if new_text != text:
            print("README progress block is stale; run tools/gen/progress.py", file=sys.stderr)
            return 1
        print("progress block: up to date")
        return 0
    README.write_text(new_text, encoding="utf-8")
    print("progress block updated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
