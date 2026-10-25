#!/usr/bin/env python3
"""Seed re/FUNCTION_MAP.csv from a Ghidra function export.

The map is the inventory the port works from. New rows start as `todo`
with an `unknown` subsystem; names and subsystems are curated by hand as
evidence appears. Existing rows are preserved by address.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

FIELDS = ["address", "name", "subsystem", "status", "notes"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("export", type=Path, help="functions.csv from Ghidra")
    parser.add_argument("--out", type=Path, default=Path("re/FUNCTION_MAP.csv"))
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    existing: dict[str, dict[str, str]] = {}
    if args.out.exists():
        with args.out.open(newline="", encoding="utf-8") as fh:
            for row in csv.DictReader(fh):
                if row.get("address"):
                    existing[row["address"].lower()] = row

    rows: list[dict[str, str]] = []
    with args.export.open(newline="", encoding="utf-8") as fh:
        for index, row in enumerate(csv.DictReader(fh)):
            if args.limit and index >= args.limit:
                break
            address = (row.get("address") or "").strip().lower()
            name = row.get("name") or f"fn_{address}"
            if not address:
                continue
            if name.startswith("Catch@") or name.startswith("thunk_"):
                continue
            if address in existing:
                rows.append(existing[address])
                continue
            is_game = name.startswith("FUN_")
            rows.append(
                {
                    "address": address,
                    "name": name,
                    "subsystem": "unknown" if is_game else "crt",
                    "status": "todo" if is_game else "external",
                    "notes": "ghidra",
                }
            )

    rows.sort(key=lambda item: int(item["address"], 16) if item["address"].startswith("0x") else item["address"])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} rows to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
