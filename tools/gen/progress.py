#!/usr/bin/env python3
"""Regenerate the generated parts of the READMEs.

README.md is the Spanish README and README.en.md the English one. Each has two
generated regions, rewritten from the same numbers:

  <!-- progress:start --> ... <!-- progress:end -->   the progress block
  <!-- badges:start -->   ... <!-- badges:end -->     the badge definitions

It also writes the progress page served by GitHub Pages from docs/
(docs/index.html in Spanish, docs/en/index.html in English; built by
tools/gen/progress_site.py from the same numbers) and docs/progress.svg, a
treemap of the game's code: one rectangle per game function, sized by its
code bytes (the span from the function to the next row of the map), grouped
by subsystem and coloured by status (green translated, amber in progress).

Inputs:
  re/FUNCTION_MAP.csv       address,name,subsystem,status,notes
  tools/gen/progress_data.py

Statuses counted as done: translated, verified.
Statuses excluded from the denominator: external.

    python3 tools/gen/progress.py          rewrite both files
    python3 tools/gen/progress.py --check  fail when a README or the map is stale
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[2]
FUNCTION_MAP = ROOT / "re" / "FUNCTION_MAP.csv"
TREEMAP = ROOT / "docs" / "progress.svg"
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
        "treemap": (
            "![Map of the game code: one rectangle per function of bo2.exe, sized by its code and grouped by "
            "system; green is translated, amber is in progress](docs/progress.svg)"
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
        "treemap": (
            "![Mapa del código del juego: un rectángulo por cada función de bo2.exe, del tamaño de su "
            "código y agrupado por sistema; verde lo traducido y ámbar lo que está en curso](docs/progress.svg)"
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


def number(lang: str, value: int) -> str:
    """A count for prose and tables: English groups thousands with commas;
    Spanish groups numbers of five digits or more with a space, so a group is
    never read as decimals."""
    if lang == "en":
        return f"{value:,}"
    text = str(value)
    if len(text) < 5:
        return text
    groups = []
    while text:
        groups.insert(0, text[-3:])
        text = text[:-3]
    return " ".join(groups)


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


def load_map_rows() -> list[tuple[int, str, str]]:
    """(address, status, subsystem) of every row of the function map, by address."""
    rows = []
    if not FUNCTION_MAP.exists():
        return rows
    with FUNCTION_MAP.open(newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            rows.append((int(row["address"], 16), (row.get("status") or "").strip().lower(),
                         (row.get("subsystem") or "unknown").strip() or "unknown"))
    rows.sort()
    return rows


def treemap_functions(rows: list[tuple[int, str, str]]) -> list[dict]:
    """The game functions with their code bytes (the span to the next row of
    the map), status and subsystem; external rows only end a span."""
    out = []
    for (address, status, subsystem), following in zip(rows, rows[1:] + [(rows[-1][0] + 16, "", "")] if rows else []):
        if status == "external":
            continue
        out.append({"address": address, "bytes": max(following[0] - address, 1), "status": status,
                    "subsystem": subsystem})
    return out


def squarify(sizes: list[float], x: float, y: float, w: float, h: float) -> list[tuple[float, float, float, float]]:
    """Squarified treemap (Bruls, Huizing, van Wijk) of `sizes`, in order."""
    total = sum(sizes)
    if not sizes or total <= 0:
        return []
    scale = w * h / total
    areas = [size * scale for size in sizes]
    rects: list[tuple[float, float, float, float]] = []

    def worst(row: list[float], side: float) -> float:
        s = sum(row)
        return max(max(side * side * r / (s * s), (s * s) / (side * side * r)) for r in row)

    index = 0
    while index < len(areas):
        side = min(w, h)
        row = [areas[index]]
        index += 1
        while index < len(areas) and worst(row + [areas[index]], side) <= worst(row, side):
            row.append(areas[index])
            index += 1
        row_total = sum(row)
        if w >= h:
            column = row_total / h
            cursor = y
            for area in row:
                rects.append((x, cursor, column, area / column))
                cursor += area / column
            x += column
            w -= column
        else:
            line = row_total / w
            cursor = x
            for area in row:
                rects.append((cursor, y, area / line, line))
                cursor += area / line
            y += line
            h -= line
    return rects


def mix(low: tuple[int, int, int], high: tuple[int, int, int], t: float) -> str:
    t = min(max(t, 0.0), 1.0)
    return "#%02x%02x%02x" % tuple(round(a + (b - a) * t) for a, b in zip(low, high))


def build_treemap(rows: list[tuple[int, str, str]]) -> str:
    """The SVG of docs/progress.svg: one rectangle per game function, sized by
    its code bytes and grouped by subsystem (the largest first), coloured by
    status. Deterministic for a given map. Each colour is one path so the file
    stays small; functions too small to see are left to their group's fill."""
    width, height, top, bottom = 960, 480, 34, 30
    functions = treemap_functions(rows)
    grey, edge, amber, green = "#2d333b", "#0d1117", "#b07d1a", "#3fb950"
    groups: dict[str, list[dict]] = {}
    for function in functions:
        groups.setdefault(function["subsystem"], []).append(function)
    names = sorted(groups, key=lambda n: (-sum(f["bytes"] for f in groups[n]), n))
    total = sum(f["bytes"] for f in functions)
    done = sum(f["bytes"] for f in functions if f["status"] in ("translated", "verified"))
    wip = sum(f["bytes"] for f in functions if f["status"] == "wip")
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" font-family="Segoe UI, Helvetica, Arial, sans-serif">',
        f'<rect width="{width}" height="{height}" fill="{edge}"/>',
        f'<text x="8" y="22" fill="#e6edf3" font-size="14">bo2.exe game code, one rectangle per function, grouped '
        f"by system: {done / total * 100:.2f}% of the bytes translated, {wip / total * 100:.2f}% in progress</text>"
        if total else '<text x="8" y="22" fill="#e6edf3" font-size="14">no game functions</text>',
    ]
    paths = {"todo": [], "wip": [], "done": []}
    labels = []
    outlines = []
    group_rects = squarify([sum(f["bytes"] for f in groups[n]) for n in names], 0, top, width, height - top - bottom)
    for name, (gx, gy, gw, gh) in zip(names, group_rects):
        members = groups[name]
        group_done = sum(f["bytes"] for f in members if f["status"] in ("translated", "verified"))
        group_wip = sum(f["bytes"] for f in members if f["status"] == "wip")
        group_bytes = sum(f["bytes"] for f in members)
        outlines.append(
            f'<rect x="{gx:.1f}" y="{gy:.1f}" width="{gw:.1f}" height="{gh:.1f}" fill="{grey}" stroke="{edge}" '
            f'stroke-width="2"><title>{name}: {len(members)} functions, {group_bytes} bytes, '
            f"{group_done / group_bytes * 100:.1f}% translated, {group_wip / group_bytes * 100:.1f}% in progress"
            f"</title></rect>")
        # A large group is split into regions of 64 KiB of addresses, near
        # to the source files the linker laid out; inside a region (or a small
        # group) the functions go from the largest down.
        regions: dict[int, list[dict]] = {}
        for function in members:
            regions.setdefault(function["address"] >> 16 if len(members) > 400 else 0, []).append(function)
        keys = sorted(regions, key=lambda k: (-sum(f["bytes"] for f in regions[k]), k))
        region_rects = squarify([sum(f["bytes"] for f in regions[k]) for k in keys], gx, gy, gw, gh)
        for region_key, (rx, ry, rw, rh) in zip(keys, region_rects):
            inside = sorted(regions[region_key], key=lambda f: (-f["bytes"], f["address"]))
            if len(regions) > 1:
                outlines.append(f'<rect x="{rx:.1f}" y="{ry:.1f}" width="{rw:.1f}" height="{rh:.1f}" fill="none" '
                                f'stroke="{edge}" stroke-width="1.2"/>')
            for function, (x, y, w, h) in zip(inside, squarify([f["bytes"] for f in inside], rx, ry, rw, rh)):
                key = "done" if function["status"] in ("translated", "verified") else (
                    "wip" if function["status"] == "wip" else "todo")
                if key == "todo" and w * h < 6:
                    continue
                paths[key].append(f"M{x:.1f} {y:.1f}h{w:.1f}v{h:.1f}h{-w:.1f}z")
        if gw > 46 and gh > 16:
            labels.append(f'<text x="{gx + 4:.1f}" y="{gy + 13:.1f}" fill="#e6edf3" font-size="11" '
                          f'paint-order="stroke" stroke="{edge}" stroke-width="3">'
                          f'{"unclassified" if name == "unknown" else name}</text>')
    out += outlines
    out.append(f'<path d="{"".join(paths["todo"])}" fill="{grey}" stroke="{edge}" stroke-width="0.4"/>')
    out.append(f'<path d="{"".join(paths["wip"])}" fill="{amber}" stroke="{edge}" stroke-width="0.4"/>')
    out.append(f'<path d="{"".join(paths["done"])}" fill="{green}" stroke="{edge}" stroke-width="0.4"/>')
    out += labels
    legend_y = height - 10
    for offset, (colour, text) in enumerate([(grey, "to do"), (amber, "in progress"), (green, "translated")]):
        x = 8 + offset * 130
        out.append(f'<rect x="{x}" y="{legend_y - 10}" width="12" height="12" fill="{colour}"/>')
        out.append(f'<text x="{x + 18}" y="{legend_y}" fill="#c9d1d9" font-size="12">{text}</text>')
    out.append(f'<text x="{width - 8}" y="{legend_y}" fill="#8b949e" font-size="11" text-anchor="end">'
               "generated by tools/gen/progress.py from re/FUNCTION_MAP.csv</text>")
    out.append("</svg>")
    return "\n".join(out) + "\n"


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
            f"| {t['functions']} | 60% | {number(lang, m['done'])} / {number(lang, m['in_scope'])} "
            f"| {pct(m['fn_score'])} |",
            f"| {t['levels']} | 30% | {level_text} | {pct(m['level_score'])} |",
            f"| {t['platform']} | 10% | {feature_text} | {pct(m['feature_score'])} |",
            "",
            t["map"].format(
                rows=number(lang, sum(counts.values())),
                translated=number(lang, counts["translated"] + counts["verified"]),
                wip=number(lang, counts["wip"]),
                todo=number(lang, counts["todo"]),
                external=number(lang, counts["external"]),
            ),
            "",
            t["treemap"],
            "",
            t["defs"],
            PROGRESS[1],
        ]
    )


def build_badges(lang: str, m: dict) -> str:
    t = TEXT[lang]
    # Plain strings first: nested same-quote f-strings need Python 3.12.
    phase = m["phase"] + " " + t["phase_state"]
    overall = "{:.1f}%".format(m["overall"] * 100)
    functions = "{} / {}".format(m["done"], m["in_scope"])
    levels = "{} / {}".format(m["done_levels"], m["total_levels"])
    return "\n".join(
        [
            BADGES[0],
            "[status-badge]: " + shield(t["phase"], phase, "orange"),
            "[overall-badge]: " + shield(t["badge_overall"], overall, "yellow"),
            "[fn-badge]: " + shield(t["badge_functions"], functions, "lightgrey"),
            "[levels-badge]: " + shield(t["badge_levels"], levels, "lightgrey"),
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
    svg = build_treemap(load_map_rows())
    old_svg = TREEMAP.read_text(encoding="utf-8") if TREEMAP.exists() else None
    if check:
        if svg != old_svg:
            print("docs/progress.svg: stale; run tools/gen/progress.py", file=sys.stderr)
            status = 1
        else:
            print("docs/progress.svg: up to date")
    elif svg != old_svg:
        TREEMAP.write_text(svg, encoding="utf-8")
        print("docs/progress.svg: updated")
    sys.path.insert(0, str(ROOT / "tools" / "gen"))
    import progress_site  # noqa: PLC0415

    levels, features, _ = load_progress_data()
    for relative, page in progress_site.build_pages(m, FUNCTION_MAP, levels, features, number).items():
        path = ROOT / "docs" / relative
        old = path.read_text(encoding="utf-8") if path.exists() else None
        if check:
            if page != old:
                print(f"docs/{relative}: stale; run tools/gen/progress.py", file=sys.stderr)
                status = 1
            else:
                print(f"docs/{relative}: up to date")
        elif page != old:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(page, encoding="utf-8")
            print(f"docs/{relative}: updated")
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
