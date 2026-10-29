#!/usr/bin/env python3
"""Regenerate the generated parts of the READMEs.

README.md is the Spanish README and README.en.md the English one. Each has two
generated regions, rewritten from the same numbers:

  <!-- progress:start --> ... <!-- progress:end -->   the progress block
  <!-- badges:start -->   ... <!-- badges:end -->     the badge definitions

Inputs:
  re/FUNCTION_MAP.csv       address,name,subsystem,status,notes
  tools/gen/progress_data.py

Statuses counted as done: translated, verified.
Statuses excluded from the denominator: external.

    python3 tools/gen/progress.py          rewrite both files
    python3 tools/gen/progress.py --check  fail when either file is stale
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[2]
FUNCTION_MAP = ROOT / "re" / "FUNCTION_MAP.csv"
PROGRESS = ("<!-- progress:start -->", "<!-- progress:end -->")
BADGES = ("<!-- badges:start -->", "<!-- badges:end -->")

WEIGHTS = {"functions": 0.60, "levels": 0.30, "platform": 0.10}

# One data source, one template per language.
TEXT = {
    "en": {
        "file": "README.en.md",
        "overall": "Overall progress",
        "columns": ("Component", "Weight", "Progress", "Score"),
        "functions": "Functions translated / in scope",
        "levels": "Levels completable (campaign chapters)",
        "platform": "Platform features",
        "partial": "partial",
        "map": (
            "Function map: {rows} rows: {translated} translated, {wip} in progress, "
            "{todo} to do, {external} external (not game code)."
        ),
        "defs": (
            "The chapter list is in [`docs/LEVELS.md`](docs/LEVELS.md). Chapter flags and "
            "platform features are in [`tools/gen/progress_data.py`](tools/gen/progress_data.py)."
        ),
        "phase": "phase",
        "phase_state": "in progress",
        "badge_overall": "overall",
        "badge_functions": "functions",
        "badge_levels": "levels",
    },
    "es": {
        "file": "README.md",
        "overall": "Progreso global",
        "columns": ("Componente", "Peso", "Avance", "Puntaje"),
        "functions": "Funciones traducidas / en alcance",
        "levels": "Niveles completables (capítulos de la campaña)",
        "platform": "Características de plataforma",
        "partial": "parcial",
        "map": (
            "Mapa de funciones: {rows} filas: {translated} traducidas, {wip} en curso, "
            "{todo} pendientes, {external} externas (no son código del juego)."
        ),
        "defs": (
            "La lista de capítulos está en [`docs/LEVELS.md`](docs/LEVELS.md). Los capítulos "
            "completables y las características de plataforma se definen en "
            "[`tools/gen/progress_data.py`](tools/gen/progress_data.py)."
        ),
        "phase": "fase",
        "phase_state": "en curso",
        "badge_overall": "global",
        "badge_functions": "funciones",
        "badge_levels": "niveles",
    },
}


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

    return progress_data.LEVELS, progress_data.PLATFORM_FEATURES, progress_data.PHASE


def pct(score: float) -> str:
    return f"{score * 100:.2f}%"


def shield(label: str, message: str, color: str) -> str:
    """A shields.io static badge URL: '-' and '_' are doubled, the rest percent-encoded."""

    def escape(text: str) -> str:
        return quote(text.replace("-", "--").replace("_", "__"), safe="")

    return f"https://img.shields.io/badge/{escape(label)}-{escape(message)}-{color}"


def measure() -> dict:
    counts = load_functions()
    levels, features, phase = load_progress_data()

    in_scope = sum(counts.values()) - counts["external"]
    done = counts["translated"] + counts["verified"]
    fn_score = done / in_scope if in_scope else 0.0

    total_levels = len(levels)
    done_levels = sum(1 for _, ok in levels if ok)
    level_score = done_levels / total_levels if total_levels else 0.0

    feature_done = sum(1 for _, state in features if state == "done")
    feature_partial = sum(1 for _, state in features if state == "partial")
    feature_score = (feature_done + 0.5 * feature_partial) / len(features) if features else 0.0

    overall = (
        WEIGHTS["functions"] * fn_score
        + WEIGHTS["levels"] * level_score
        + WEIGHTS["platform"] * feature_score
    )
    return {
        "counts": counts,
        "in_scope": in_scope,
        "done": done,
        "fn_score": fn_score,
        "total_levels": total_levels,
        "done_levels": done_levels,
        "level_score": level_score,
        "feature_done": feature_done,
        "feature_partial": feature_partial,
        "feature_total": len(features),
        "feature_score": feature_score,
        "overall": overall,
        "phase": phase,
    }


def build_progress(lang: str, m: dict) -> str:
    t = TEXT[lang]
    counts = m["counts"]
    level_text = f"{m['done_levels']} / {m['total_levels']}" if m["total_levels"] else "0 / 0"
    feature_text = f"{m['feature_done']} / {m['feature_total']}"
    if m["feature_partial"]:
        feature_text += f" + {m['feature_partial']} {t['partial']}"
    columns = t["columns"]
    return "\n".join(
        [
            PROGRESS[0],
            f"**{t['overall']}: {m['overall'] * 100:.1f}%**",
            "",
            f"| {columns[0]} | {columns[1]} | {columns[2]} | {columns[3]} |",
            "|---|---:|---:|---:|",
            f"| {t['functions']} | 60% | {m['done']} / {m['in_scope']} | {pct(m['fn_score'])} |",
            f"| {t['levels']} | 30% | {level_text} | {pct(m['level_score'])} |",
            f"| {t['platform']} | 10% | {feature_text} | {pct(m['feature_score'])} |",
            "",
            t["map"].format(
                rows=sum(counts.values()),
                translated=counts["translated"] + counts["verified"],
                wip=counts["wip"],
                todo=counts["todo"],
                external=counts["external"],
            ),
            "",
            t["defs"],
            PROGRESS[1],
        ]
    )


def build_badges(lang: str, m: dict) -> str:
    t = TEXT[lang]
    return "\n".join(
        [
            BADGES[0],
            f"[status-badge]: {shield(t['phase'], m['phase'] + ' ' + t['phase_state'], 'orange')}",
            f"[overall-badge]: {shield(t['badge_overall'], f'{m['overall'] * 100:.1f}%', 'yellow')}",
            f"[fn-badge]: {shield(t['badge_functions'], f'{m['done']} / {m['in_scope']}', 'lightgrey')}",
            f"[levels-badge]: {shield(t['badge_levels'], f'{m['done_levels']} / {m['total_levels']}', 'lightgrey')}",
            BADGES[1],
        ]
    )


def replace_region(text: str, markers: tuple[str, str], block: str) -> str | None:
    start, end = markers
    if start not in text or end not in text:
        return None
    head, rest = text.split(start, 1)
    _, tail = rest.split(end, 1)
    return head + block + tail


def render(lang: str, text: str, m: dict) -> str | None:
    updated = replace_region(text, PROGRESS, build_progress(lang, m))
    if updated is None:
        return None
    return replace_region(updated, BADGES, build_badges(lang, m))


def main() -> int:
    check = "--check" in sys.argv
    m = measure()
    status = 0
    for lang, t in TEXT.items():
        path = ROOT / t["file"]
        if not path.exists():
            print(f"{t['file']} not found", file=sys.stderr)
            status = 2
            continue
        text = path.read_text(encoding="utf-8")
        new_text = render(lang, text, m)
        if new_text is None:
            print(f"{t['file']}: progress or badge markers not found", file=sys.stderr)
            status = 2
            continue
        if check:
            if new_text != text:
                print(f"{t['file']}: generated regions are stale; run tools/gen/progress.py", file=sys.stderr)
                status = status or 1
            else:
                print(f"{t['file']}: up to date")
        else:
            path.write_text(new_text, encoding="utf-8")
            print(f"{t['file']}: updated")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
