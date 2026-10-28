#!/usr/bin/env python3
"""BO2 Oracle client (Ubuntu side).

Talks to the native Windows oracle harness through the SSH reverse tunnel:

    http://127.0.0.1:8765  ->  Windows 127.0.0.1:8765

Token lookup order:
    $ORACLE_TOKEN
    $ORACLE_TOKEN_FILE
    ~/.config/bo2oracle/token
    ./.oracle_token
    tools/oracle/.oracle_token
    <script dir>/.oracle_token

Examples:
    python3 tools/oracle/oracle_client.py health
    python3 tools/oracle/oracle_client.py list
    python3 tools/oracle/oracle_client.py download capture_20261027_2107 --dest external/captures
    python3 tools/oracle/oracle_client.py download capture_20261027_2107 --no-camera --dest external/captures
    python3 tools/oracle/oracle_client.py download capture_20261027_2107 --all --dest external/captures
    python3 tools/oracle/oracle_client.py run probe
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

DEFAULT_URL = os.environ.get("ORACLE_URL", "http://127.0.0.1:8765")
CORE_NAMES = {
    "game_manifest.json",
    "notes.md",
    "SHA256SUMS",
    "inputs.jsonl",
    "state.csv",
    "camera_d3d8.jsonl",
    "camera_frames.jsonl",
    "state_summary.json",
}


def find_token() -> str:
    env = os.environ.get("ORACLE_TOKEN", "").strip()
    if env:
        return env
    candidates = []
    if os.environ.get("ORACLE_TOKEN_FILE"):
        candidates.append(Path(os.environ["ORACLE_TOKEN_FILE"]))
    candidates.extend([
        Path.home() / ".config" / "bo2oracle" / "token",
        Path(".oracle_token"),
        Path("tools/oracle/.oracle_token"),
        Path(__file__).resolve().parent / ".oracle_token",
    ])
    for path in candidates:
        try:
            text = path.read_text(encoding="utf-8").strip()
        except OSError:
            continue
        if text:
            return text
    return ""


def safe_relative(path: str) -> str:
    """Normalize a capture path and reject absolute or traversing paths."""
    text = str(path).replace("\\", "/").strip()
    if not text:
        raise ValueError("empty capture path")
    if text.startswith("/") or (len(text) > 1 and text[1] == ":"):
        raise ValueError(f"absolute capture path: {path!r}")
    parts = [part for part in text.split("/") if part not in ("", ".")]
    if not parts or any(part == ".." for part in parts):
        raise ValueError(f"unsafe capture path: {path!r}")
    return "/".join(parts)


class Client:
    def __init__(self, base: str, token: str):
        self.base = base.rstrip("/")
        self.token = token

    def _request(self, path: str, method: str = "GET", body: bytes | None = None, stream: bool = False):
        url = self.base + path
        req = urllib.request.Request(url, data=body, method=method)
        if self.token:
            req.add_header("Authorization", "Bearer " + self.token)
        if body is not None:
            req.add_header("Content-Type", "application/json")
        return urllib.request.urlopen(req, timeout=120)

    def get_json(self, path: str):
        with self._request(path) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def post_json(self, path: str, payload: dict):
        data = json.dumps(payload).encode("utf-8")
        with self._request(path, "POST", data) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def download(self, remote_path: str, local_path: Path):
        local_path.parent.mkdir(parents=True, exist_ok=True)
        with self._request(remote_path, stream=True) as resp, local_path.open("wb") as fh:
            while True:
                chunk = resp.read(1024 * 1024)
                if not chunk:
                    break
                fh.write(chunk)
        return local_path.stat().st_size


def print_json(obj):
    print(json.dumps(obj, ensure_ascii=False, indent=2))


def cmd_health(c: Client, args):
    print_json(c.get_json("/health"))


def cmd_list(c: Client, args):
    data = c.get_json("/api/captures")
    if args.json:
        print_json(data)
        return
    for cap in data.get("captures", []):
        flags = []
        for key in ("has_manifest", "has_video", "has_inputs", "has_state", "has_camera", "has_saves", "has_memory"):
            if cap.get(key):
                flags.append(key[4:])
        print(f"{cap['name']}  {cap.get('mtime','')}  {'/'.join(flags)}")
        manifest = cap.get("manifest") or {}
        if manifest.get("bo2_sha256"):
            print(f"  bo2={manifest.get('bo2_sha256')} res={manifest.get('resolution')} v={manifest.get('bo2_version')}")


def require_capture(c: Client, name: str):
    data = c.get_json("/api/captures/" + urllib.parse.quote(name))
    return data["capture"]


def cmd_summary(c: Client, args):
    cap = require_capture(c, args.capture)
    if args.json:
        print_json(cap)
        return
    print(f"capture: {cap['name']}")
    print(f"mtime:   {cap.get('mtime')}")
    manifest = cap.get("manifest_full") or cap.get("manifest") or {}
    for key in ("bo2_sha256", "bo2_version", "resolution", "windowed", "installed_dir", "patch_applied"):
        if key in manifest:
            print(f"{key}: {manifest[key]}")
    print(f"screenshots: {len(cap.get('screenshots', []))}")
    print(f"ppm frames:  {len(cap.get('frames_ppm', []))}")
    print(f"state lines: {cap.get('state_lines', 0)}")
    print(f"camera lines:{cap.get('camera_lines', 0)}")
    notes = cap.get("notes")
    if notes:
        print("\n--- notes.md (head) ---")
        print("\n".join(notes.splitlines()[:30]))


def cmd_files(c: Client, args):
    cap = require_capture(c, args.capture)
    files = cap.get("files", [])
    if args.json:
        print_json(files)
        return
    for f in files:
        print(f"{f['size']:>12}  {f['path']}")


def cmd_get(c: Client, args):
    cap = require_capture(c, args.capture)
    try:
        rel = safe_relative(args.path)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    if rel not in {safe_relative(f["path"]) for f in cap.get("files", [])}:
        raise SystemExit(f"not in capture: {rel}")
    remote = "/files/" + urllib.parse.quote(cap["name"]) + "/" + urllib.parse.quote(rel, safe="/")
    dest = Path(args.out) if args.out else Path(cap["name"]) / rel
    size = c.download(remote, dest)
    print(f"{dest}  {size} bytes")


def cmd_download(c: Client, args):
    cap = require_capture(c, args.capture)
    dest_root = Path(args.dest) / cap["name"]
    files = cap.get("files", [])
    selected = []
    for f in files:
        p = f["path"]
        if not args.all:
            if p == "capture.zip" and not args.include_zip:
                continue
            if p.startswith("video") and not args.include_video:
                continue
            if p.startswith("memory/") and not args.include_memory:
                continue
            if p.startswith("logs/") and not args.include_logs:
                continue
            if p.startswith("camera_") and args.no_camera:
                continue
            if p.startswith("frames_ppm/") and args.no_ppm:
                continue
            if p.startswith("screenshots/") and args.no_screenshots:
                continue
        selected.append(f)
    if args.only:
        prefixes = tuple(args.only)
        selected = [f for f in selected if f["path"].startswith(prefixes)]
    total = 0
    for f in selected:
        try:
            rel = safe_relative(f["path"])
        except ValueError as exc:
            raise SystemExit(str(exc)) from exc
        remote = "/files/" + urllib.parse.quote(cap["name"]) + "/" + urllib.parse.quote(rel, safe="/")
        local = dest_root / rel
        size = c.download(remote, local)
        total += size
        print(f"{rel}  {size} bytes")
    print(f"\n{len(selected)} files, {total} bytes -> {dest_root}")


def cmd_run(c: Client, args):
    params = json.loads(args.params) if args.params else {}
    job = c.post_json("/api/jobs", {"type": args.type, "params": params, "reason": args.reason})["job"]
    print_json(job)


def cmd_job(c: Client, args):
    job = c.get_json("/api/jobs/" + args.id)["job"]
    if args.json:
        print_json(job)
        return
    print(f"{job['id']}  {job['type']}  {job['status']}  exit={job.get('exit_code')}")
    if args.log and job.get("log_tail"):
        print("\n--- log tail ---")
        print(job["log_tail"])


def cmd_cancel(c: Client, args):
    print_json(c.post_json(f"/api/jobs/{args.id}/cancel", {}))


def main():
    parser = argparse.ArgumentParser(description="BO2 Oracle client")
    parser.add_argument("--url", default=DEFAULT_URL, help="oracle base URL (default: %(default)s)")
    parser.add_argument("--token", default="", help="override token")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("health"); p.set_defaults(func=cmd_health)
    p = sub.add_parser("list"); p.add_argument("--json", action="store_true"); p.set_defaults(func=cmd_list)
    p = sub.add_parser("summary"); p.add_argument("capture"); p.add_argument("--json", action="store_true"); p.set_defaults(func=cmd_summary)
    p = sub.add_parser("files"); p.add_argument("capture"); p.add_argument("--json", action="store_true"); p.set_defaults(func=cmd_files)
    p = sub.add_parser("get"); p.add_argument("capture"); p.add_argument("path"); p.add_argument("--out"); p.set_defaults(func=cmd_get)
    p = sub.add_parser("download"); p.add_argument("capture"); p.add_argument("--dest", default="external/captures"); p.add_argument("--all", action="store_true"); p.add_argument("--include-video", action="store_true"); p.add_argument("--include-memory", action="store_true"); p.add_argument("--include-zip", action="store_true"); p.add_argument("--include-logs", action="store_true"); p.add_argument("--no-camera", action="store_true"); p.add_argument("--no-ppm", action="store_true"); p.add_argument("--no-screenshots", action="store_true"); p.add_argument("--only", nargs="*"); p.set_defaults(func=cmd_download)
    p = sub.add_parser("run"); p.add_argument("type", choices=["probe", "capture"]); p.add_argument("--params", default=""); p.add_argument("--reason", default=""); p.set_defaults(func=cmd_run)
    p = sub.add_parser("job"); p.add_argument("id"); p.add_argument("--log", action="store_true"); p.add_argument("--json", action="store_true"); p.set_defaults(func=cmd_job)
    p = sub.add_parser("cancel"); p.add_argument("id"); p.set_defaults(func=cmd_cancel)

    args = parser.parse_args()
    token = args.token or find_token()
    client = Client(args.url, token)
    try:
        args.func(client, args)
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")
        print(f"HTTP {exc.code}: {body}", file=sys.stderr)
        sys.exit(1)
    except urllib.error.URLError as exc:
        print(f"connection failed: {exc.reason}", file=sys.stderr)
        print(f"URL: {args.url}", file=sys.stderr)
        print("Check the SSH reverse tunnel on the Windows host and that the server is running.", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()
