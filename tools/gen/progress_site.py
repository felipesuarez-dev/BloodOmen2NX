"""The progress page of the project (GitHub Pages, served from docs/).

tools/gen/progress.py calls build_pages() with the numbers it already
measured and writes docs/index.html (Spanish) and docs/en/index.html
(English); `progress.py --check` fails when they are stale. The pages are
static: no script, no font or file from another site, and no image of the
game (the only picture is docs/progress.svg, the map of the port's code).
"""

from __future__ import annotations

import csv
import html
from pathlib import Path

REPO = "https://github.com/felipesuarez-dev/BloodOmen2NX"
MILESTONES = ["M0", "M1", "M2", "M3", "M4", "M5", "M6"]

TEXT = {
    "es": {
        "lang": "es",
        "file": "index.html",
        "other": ("English", "en/"),
        "root": "",
        "title": "Blood Omen 2 · Port para Nintendo Switch · Progreso",
        "kicker": "Blood Omen 2 · port nativo para Nintendo Switch",
        "overall": "progreso global",
        "phase": "Fase {phase} en curso",
        "summary": "{done} de {scope} funciones del juego traducidas · {chapters} de {levels} capítulos completables",
        "components": "Componentes",
        "weight": "peso",
        "functions": "Funciones traducidas",
        "levels": "Capítulos completables",
        "platform": "Características de plataforma",
        "map_line": "{rows} funciones en el mapa: {translated} traducidas, {wip} en curso, {todo} pendientes "
                    "y {external} externas (no son código del juego).",
        "why": "Una función cuenta solo cuando el port reproduce todo su contrato y una prueba lo respalda; "
               "cada una suma unos 0,0035 puntos, un capítulo 2,7 y una característica de plataforma 0,8.",
        "subsystems": "Funciones por sistema",
        "translated": "traducidas",
        "wip": "en curso",
        "chapters": "Capítulos de la campaña",
        "chapter_done": "completable",
        "chapter_todo": "todavía no",
        "features": "Plataforma",
        "state": {"done": "lista", "partial": "parcial", "todo": "pendiente"},
        "code_map": "Mapa del código",
        "code_map_alt": "Mapa del código de bo2.exe: un rectángulo por función, del tamaño de su código y agrupado por sistema; verde lo traducido, ámbar lo que está en curso",
        "milestones": "Hitos",
        "milestone_state": {"done": "completo", "now": "en curso", "next": "sin iniciar"},
        "milestone_names": [
            "Laboratorio y triaje del binario",
            "Contenedores y recursos",
            "Esqueleto del host y primer fotograma",
            "Porción jugable",
            "Sistemas completos",
            "Contenido completo",
            "Lanzamiento en Switch",
        ],
        "footer_repo": "Repositorio",
        "footer_readme": "README",
        "footer_note": "Proyecto de aficionados, sin afiliación con los dueños de Blood Omen 2. El repositorio no "
                       "incluye el juego: hace falta una copia original para PC.",
        "generated": "Generado por tools/gen/progress.py desde re/FUNCTION_MAP.csv.",
        "feature_names": {
            "Host build skeleton": "Esqueleto del host",
            "Installer container (SFXI)": "Contenedor del instalador (SFXI)",
            "BIG/RKV parsers": "Lectores BIG y RKV",
            "Validated asset extraction": "Recursos validados",
            "Headless host first frame": "Primer fotograma sin ventana",
            "Level geometry (cellgrup)": "Geometría de niveles (cellgrup)",
            "Level textures (DDS binding)": "Texturas de niveles (DDS)",
            "Host renderer (OpenGL)": "Renderizador del host (OpenGL)",
            "Switch renderer (deko3d)": "Renderizador de Switch (deko3d)",
            "Audio": "Audio",
            "Input": "Entrada",
            "Video (Bink)": "Video (Bink)",
        },
        "subsystem_names": {
            "script": "Scripts", "animation": "Animación", "player": "Jugador", "level": "Niveles",
            "camera": "Cámara", "render": "Render", "collision": "Colisión", "font": "Texto",
            "entity": "Entidades", "locale": "Idiomas", "engine": "Motor",
        },
    },
    "en": {
        "lang": "en",
        "file": "en/index.html",
        "other": ("Español", "../"),
        "root": "../",
        "title": "Blood Omen 2 · Nintendo Switch port · Progress",
        "kicker": "Blood Omen 2 · native Nintendo Switch port",
        "overall": "overall progress",
        "phase": "Phase {phase} in progress",
        "summary": "{done} of {scope} game functions translated · {chapters} of {levels} chapters completable",
        "components": "Components",
        "weight": "weight",
        "functions": "Functions translated",
        "levels": "Chapters completable",
        "platform": "Platform features",
        "map_line": "{rows} functions in the map: {translated} translated, {wip} in progress, {todo} to do "
                    "and {external} external (not game code).",
        "why": "A function counts only when the port reproduces its whole contract and a test pins it; each one "
               "adds about 0.0035 points, a chapter 2.7 and a platform feature 0.8.",
        "subsystems": "Functions by system",
        "translated": "translated",
        "wip": "in progress",
        "chapters": "Campaign chapters",
        "chapter_done": "completable",
        "chapter_todo": "not yet",
        "features": "Platform",
        "state": {"done": "done", "partial": "partial", "todo": "to do"},
        "code_map": "Code map",
        "code_map_alt": "Map of the bo2.exe code: one rectangle per function, sized by its code and grouped by system; green is translated, amber is in progress",
        "milestones": "Milestones",
        "milestone_state": {"done": "done", "now": "in progress", "next": "not started"},
        "milestone_names": [
            "Lab and binary triage",
            "Containers and assets",
            "Host skeleton and first frame",
            "Playable slice",
            "Complete systems",
            "Complete content",
            "Switch release",
        ],
        "footer_repo": "Repository",
        "footer_readme": "README",
        "footer_note": "A fan project, not affiliated with the owners of Blood Omen 2. The repository does not "
                       "include the game: an original PC copy is needed.",
        "generated": "Generated by tools/gen/progress.py from re/FUNCTION_MAP.csv.",
        "subsystem_names": {
            "script": "Scripts", "animation": "Animation", "player": "Player", "level": "Levels",
            "camera": "Camera", "render": "Rendering", "collision": "Collision", "font": "Text",
            "entity": "Entities", "locale": "Languages", "engine": "Engine",
        },
    },
}

CSS = """
:root{--bg:#0c0b0d;--card:#141216;--line:#262228;--text:#ece7e2;--muted:#9b938c;--accent:#b81d33;
--accent-soft:#3a1519;--wip:#c4923a;--wip-soft:#3a2c14;--ok:#8fb996}
@media (prefers-color-scheme:light){:root{--bg:#f7f4f1;--card:#fffdfb;--line:#e6dfd8;--text:#1f1b18;
--muted:#6f675f;--accent:#9e1428;--accent-soft:#f3dadd;--wip:#a8721b;--wip-soft:#f3e6cf;--ok:#3f7a4a}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.55 system-ui,-apple-system,"Segoe UI",
Roboto,Helvetica,Arial,sans-serif;-webkit-font-smoothing:antialiased}
main{max-width:860px;margin:0 auto;padding:40px 16px 56px}
a{color:inherit;text-decoration-color:var(--accent);text-underline-offset:3px}
.top{display:flex;justify-content:space-between;align-items:baseline;gap:16px;flex-wrap:wrap}
.kicker{font-size:12px;letter-spacing:.18em;text-transform:uppercase;color:var(--muted)}
.lang{font-size:13px;color:var(--muted)}
.hero{margin:28px 0 36px}
.big{font-family:"Iowan Old Style","Palatino Linotype",Palatino,Georgia,serif;font-size:clamp(64px,14vw,112px);
line-height:1;font-weight:400;letter-spacing:-.02em;font-variant-numeric:tabular-nums}
.big small{font-size:.38em;color:var(--muted);margin-left:.15em}
.label{color:var(--muted);margin-top:6px}
.bar{height:6px;background:var(--line);border-radius:3px;overflow:hidden;margin:22px 0 14px}
.bar>span{display:block;height:100%;background:var(--accent)}
.meta{display:flex;gap:10px;flex-wrap:wrap;font-size:14px;color:var(--muted)}
.pill{border:1px solid var(--line);border-radius:999px;padding:2px 10px}
.pill.now{border-color:var(--accent);color:var(--text)}
section{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:22px 22px 18px;margin:18px 0}
h2{font-family:"Iowan Old Style","Palatino Linotype",Palatino,Georgia,serif;font-weight:400;font-size:22px;
margin:0 0 14px}
.row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:4px 16px;margin:12px 0}
.row .name{font-weight:500}.row .value{font-variant-numeric:tabular-nums;color:var(--muted);text-align:right}
.row .bar{grid-column:1/-1;margin:0;height:5px}
.note{font-size:13px;color:var(--muted);margin:14px 0 0}
.sys{display:grid;grid-template-columns:110px minmax(0,1fr) auto;gap:6px 14px;align-items:center;margin:8px 0}
.stack{display:flex;height:10px;border-radius:5px;overflow:hidden;background:var(--line)}
.stack .t{background:var(--accent)}.stack .w{background:var(--wip)}
.num{font-variant-numeric:tabular-nums;color:var(--muted);font-size:13px;white-space:nowrap}
.legend{display:flex;gap:16px;font-size:13px;color:var(--muted);margin-bottom:6px}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:6px;vertical-align:baseline}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:8px}
.cell{border:1px solid var(--line);border-radius:10px;padding:9px 12px;font-size:14px}
.cell span{display:block;font-size:12px;color:var(--muted)}
.cell.done{border-color:var(--ok)}.cell.partial{border-color:var(--wip)}
.map img{width:100%;height:auto;border-radius:8px;display:block}
ol.steps{list-style:none;margin:0;padding:0}
ol.steps li{display:grid;grid-template-columns:44px minmax(0,1fr) auto;gap:10px;padding:8px 0;
border-top:1px solid var(--line);font-size:14px}
ol.steps li:first-child{border-top:0}
ol.steps b{font-variant-numeric:tabular-nums}
ol.steps .s{color:var(--muted);font-size:13px}
ol.steps .now .s{color:var(--accent)}ol.steps .done .s{color:var(--ok)}
footer{margin-top:28px;font-size:13px;color:var(--muted)}
footer p{margin:6px 0}
@media (max-width:520px){section{padding:18px 16px 14px}.sys{grid-template-columns:84px minmax(0,1fr) auto}}
""".strip()


def subsystem_counts(function_map: Path) -> list[tuple[str, int, int]]:
    """(subsystem, translated, wip) for each subsystem with work, most first."""
    counts: dict[str, list[int]] = {}
    with function_map.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            status = (row.get("status") or "").strip().lower()
            if status not in ("translated", "verified", "wip"):
                continue
            entry = counts.setdefault((row.get("subsystem") or "unknown").strip(), [0, 0])
            entry[0 if status != "wip" else 1] += 1
    return sorted(((name, t, w) for name, (t, w) in counts.items()), key=lambda r: (-(r[1] + r[2]), r[0]))


def _e(text: str) -> str:
    return html.escape(text, quote=True)


def _pct(value: float, digits: int = 1) -> str:
    return f"{value * 100:.{digits}f}%"


def build_page(lang: str, m: dict, systems: list[tuple[str, int, int]], levels: list, features: list,
               number) -> str:
    t = TEXT[lang]
    counts = m["counts"]
    phase_index = MILESTONES.index(m["phase"]) if m["phase"] in MILESTONES else 0
    out = [
        "<!doctype html>",
        f'<html lang="{t["lang"]}">',
        "<head>",
        '<meta charset="utf-8">',
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        f"<title>{_e(t['title'])}</title>",
        f'<meta name="description" content="{_e(t["summary"].format(done=m["done"], scope=m["in_scope"], chapters=m["done_levels"], levels=m["total_levels"]))}">',
        f"<style>{CSS}</style>",
        "</head>",
        "<body>",
        "<main>",
        '<div class="top">',
        f'<div class="kicker">{_e(t["kicker"])}</div>',
        f'<a class="lang" href="{t["other"][1]}">{_e(t["other"][0])}</a>',
        "</div>",
        '<div class="hero">',
        f'<div class="big">{m["overall"] * 100:.1f}<small>%</small></div>',
        f'<div class="label">{_e(t["overall"])}</div>',
        f'<div class="bar"><span style="width:{m["overall"] * 100:.2f}%"></span></div>',
        '<div class="meta">',
        f'<span class="pill now">{_e(t["phase"].format(phase=m["phase"]))}</span>',
        f'<span>{_e(t["summary"].format(done=number(lang, m["done"]), scope=number(lang, m["in_scope"]), chapters=m["done_levels"], levels=m["total_levels"]))}</span>',
        "</div>",
        "</div>",
    ]

    def component(name: str, weight: str, value: str, score: float) -> list[str]:
        return [
            '<div class="row">',
            f'<span class="name">{_e(name)} <span class="num">· {_e(t["weight"])} {weight}</span></span>',
            f'<span class="value">{_e(value)} · {_pct(score, 2)}</span>',
            f'<div class="bar"><span style="width:{min(score, 1.0) * 100:.2f}%"></span></div>',
            "</div>",
        ]

    features_value = f"{m['feature_done']} / {m['feature_total']}"
    if m["feature_partial"]:
        features_value += f" + {m['feature_partial']} {t['state']['partial']}"
    out += ["<section>", f"<h2>{_e(t['components'])}</h2>"]
    out += component(t["functions"], "60%", f"{number(lang, m['done'])} / {number(lang, m['in_scope'])}", m["fn_score"])
    out += component(t["levels"], "30%", f"{m['done_levels']} / {m['total_levels']}", m["level_score"])
    out += component(t["platform"], "10%", features_value, m["feature_score"])
    out.append('<p class="note">' + _e(t["map_line"].format(
        rows=number(lang, sum(counts.values())), translated=number(lang, counts["translated"] + counts["verified"]),
        wip=number(lang, counts["wip"]), todo=number(lang, counts["todo"]), external=number(lang, counts["external"])))
        + "</p>")
    out.append(f'<p class="note">{_e(t["why"])}</p>')
    out.append("</section>")

    widest = max((tr + w for _, tr, w in systems), default=1)
    out += ["<section>", f"<h2>{_e(t['subsystems'])}</h2>",
            f'<div class="legend"><span><i class="dot" style="background:var(--accent)"></i>{_e(t["translated"])}</span>'
            f'<span><i class="dot" style="background:var(--wip)"></i>{_e(t["wip"])}</span></div>']
    for name, done, wip in systems:
        label = t["subsystem_names"].get(name, name.capitalize())
        out.append(
            f'<div class="sys"><span>{_e(label)}</span><div class="stack" style="width:{(done + wip) / widest * 100:.1f}%">'
            f'<span class="t" style="flex:{done}"></span><span class="w" style="flex:{wip}"></span></div>'
            f'<span class="num">{done} · {wip}</span></div>'
        )
    out.append("</section>")

    out += ["<section>", f"<h2>{_e(t['chapters'])}</h2>", '<div class="grid">']
    for name, completable in levels:
        state = t["chapter_done"] if completable else t["chapter_todo"]
        out.append(f'<div class="cell{" done" if completable else ""}">{_e(name)}<span>{_e(state)}</span></div>')
    out += ["</div>", "</section>"]

    out += ["<section>", f"<h2>{_e(t['features'])}</h2>", '<div class="grid">']
    for name, state in features:
        css = {"done": " done", "partial": " partial"}.get(state, "")
        label = t.get("feature_names", {}).get(name, name)
        out.append(f'<div class="cell{css}">{_e(label)}<span>{_e(t["state"].get(state, state))}</span></div>')
    out += ["</div>", "</section>"]

    out += ['<section class="map">', f"<h2>{_e(t['code_map'])}</h2>",
            f'<img src="{t["root"]}progress.svg" alt="{_e(t["code_map_alt"])}" width="960" height="480" loading="lazy">',
            "</section>"]

    out += ["<section>", f"<h2>{_e(t['milestones'])}</h2>", '<ol class="steps">']
    for index, (code, name) in enumerate(zip(MILESTONES, t["milestone_names"])):
        key = "done" if index < phase_index else "now" if index == phase_index else "next"
        out.append(f'<li class="{key}"><b>{code}</b><span>{_e(name)}</span>'
                   f'<span class="s">{_e(t["milestone_state"][key])}</span></li>')
    out += ["</ol>", "</section>"]

    readme = f"{REPO}#readme" if lang == "es" else f"{REPO}/blob/develop/README.en.md"
    out += [
        "<footer>",
        f'<p><a href="{REPO}">{_e(t["footer_repo"])}</a> · <a href="{readme}">{_e(t["footer_readme"])}</a></p>',
        f"<p>{_e(t['footer_note'])}</p>",
        f"<p>{_e(t['generated'])}</p>",
        "</footer>",
        "</main>",
        "</body>",
        "</html>",
    ]
    return "\n".join(out) + "\n"


def build_pages(m: dict, function_map: Path, levels: list, features: list, number) -> dict[str, str]:
    """{path relative to docs/: page} for both languages."""
    systems = subsystem_counts(function_map)
    return {TEXT[lang]["file"]: build_page(lang, m, systems, levels, features, number) for lang in TEXT}
