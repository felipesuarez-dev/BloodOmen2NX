#!/usr/bin/env python3
"""Compare two rendered frames and write review artifacts.

    python3 tools/compare_frames.py left.ppm right.ppm --out /tmp/gate
    python3 tools/compare_frames.py left.ppm right.ppm --out /tmp/gate \
        --tiles 4 --region ui:0,320,640,160 --diff-gain 4

Writes `<out>-side-by-side.ppm`, `<out>-diff.ppm` and
`<out>-metrics.json`. The report includes whole-frame metrics, optional
N x N tile metrics and named regions, which is how the UI, world and
lighting layers are isolated for review.

Both inputs must be binary PPM (P6, 8-bit). The port writes PPM with
`--out`; convert an oracle capture to PPM before comparing it.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Image:
    width: int
    height: int
    pixels: bytes


def _token(data: bytes, offset: int) -> tuple[bytes, int]:
    while offset < len(data):
        if data[offset:offset + 1] == b"#":
            end = data.find(b"\n", offset)
            offset = len(data) if end < 0 else end + 1
            continue
        if data[offset:offset + 1].isspace():
            offset += 1
            continue
        break
    end = offset
    while end < len(data) and not data[end:end + 1].isspace() and data[end:end + 1] != b"#":
        end += 1
    if end == offset:
        raise ValueError("truncated PPM header")
    return data[offset:end], end


def read_ppm(path: Path) -> Image:
    data = Path(path).read_bytes()
    magic, offset = _token(data, 0)
    if magic != b"P6":
        raise ValueError(f"{path}: expected binary PPM (P6)")
    width_token, offset = _token(data, offset)
    height_token, offset = _token(data, offset)
    max_token, offset = _token(data, offset)
    width = int(width_token)
    height = int(height_token)
    maximum = int(max_token)
    if width <= 0 or height <= 0 or maximum != 255:
        raise ValueError(f"{path}: unsupported PPM dimensions or depth")
    if offset < len(data) and data[offset:offset + 1].isspace():
        offset += 1
    expected = width * height * 3
    if len(data) - offset < expected:
        raise ValueError(f"{path}: truncated pixel data")
    return Image(width, height, data[offset:offset + expected])


def write_ppm(path: Path, image: Image) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    header = f"P6\n{image.width} {image.height}\n255\n".encode("ascii")
    path.write_bytes(header + image.pixels)


def _crop(image: Image, x: int, y: int, width: int, height: int) -> Image:
    if x < 0 or y < 0 or width <= 0 or height <= 0:
        raise ValueError("invalid region")
    if x + width > image.width or y + height > image.height:
        raise ValueError("region exceeds the frame")
    rows = []
    for row in range(y, y + height):
        start = (row * image.width + x) * 3
        rows.append(image.pixels[start:start + width * 3])
    return Image(width, height, b"".join(rows))


def compare(left: Image, right: Image, region: tuple[int, int, int, int] | None = None) -> dict:
    if left.width != right.width or left.height != right.height:
        raise ValueError("frames have different dimensions")
    if region is not None:
        left = _crop(left, *region)
        right = _crop(right, *region)
    total = left.width * left.height
    absolute = 0
    squared = 0
    maximum = 0
    changed = 0
    min_x = left.width
    min_y = left.height
    max_x = -1
    max_y = -1
    for index in range(total):
        offset = index * 3
        delta = 0
        for channel in range(3):
            difference = abs(left.pixels[offset + channel] - right.pixels[offset + channel])
            absolute += difference
            squared += difference * difference
            maximum = max(maximum, difference)
            if difference:
                delta = 1
        if delta:
            changed += 1
            x = index % left.width
            y = index // left.width
            min_x = min(min_x, x)
            min_y = min(min_y, y)
            max_x = max(max_x, x)
            max_y = max(max_y, y)
    samples = total * 3
    return {
        "width": left.width,
        "height": left.height,
        "pixels": total,
        "mean_absolute": absolute / samples if samples else 0.0,
        "rmse": math.sqrt(squared / samples) if samples else 0.0,
        "max_difference": maximum,
        "changed_pixels": changed,
        "changed_ratio": changed / total if total else 0.0,
        "bounding_box": None if max_x < 0 else [min_x, min_y, max_x, max_y],
    }


def side_by_side(left: Image, right: Image) -> Image:
    if left.width != right.width or left.height != right.height:
        raise ValueError("frames have different dimensions")
    separator = 2
    width = left.width * 2 + separator
    rows = []
    for row in range(left.height):
        left_start = row * left.width * 3
        right_start = row * right.width * 3
        rows.append(left.pixels[left_start:left_start + left.width * 3])
        rows.append(b"\x20\x20\x20" * separator)
        rows.append(right.pixels[right_start:right_start + right.width * 3])
    return Image(width, left.height, b"".join(rows))


def difference(left: Image, right: Image, gain: int) -> Image:
    if left.width != right.width or left.height != right.height:
        raise ValueError("frames have different dimensions")
    if gain < 1:
        raise ValueError("diff gain must be at least 1")
    output = bytearray(len(left.pixels))
    for index, (a, b) in enumerate(zip(left.pixels, right.pixels)):
        output[index] = min(255, abs(a - b) * gain)
    return Image(left.width, left.height, bytes(output))


def tile_metrics(left: Image, right: Image, tiles: int) -> list[dict]:
    if tiles < 1:
        raise ValueError("tiles must be at least 1")
    if left.width % tiles or left.height % tiles:
        raise ValueError("frame dimensions are not divisible by the tile count")
    width = left.width // tiles
    height = left.height // tiles
    result = []
    for row in range(tiles):
        for column in range(tiles):
            metrics = compare(left, right, (column * width, row * height, width, height))
            metrics["tile"] = [column, row]
            result.append(metrics)
    return result


def parse_region(value: str) -> tuple[str, tuple[int, int, int, int]]:
    name, _, geometry = value.partition(":")
    if not name or not geometry:
        raise ValueError(f"bad region {value!r}; expected name:x,y,w,h")
    parts = geometry.split(",")
    if len(parts) != 4:
        raise ValueError(f"bad region {value!r}; expected name:x,y,w,h")
    values = [int(part) for part in parts]
    return name, (values[0], values[1], values[2], values[3])


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare two PPM frames")
    parser.add_argument("left")
    parser.add_argument("right")
    parser.add_argument("--out", required=True, help="output prefix")
    parser.add_argument("--diff-gain", type=int, default=1)
    parser.add_argument("--tiles", type=int, default=0)
    parser.add_argument("--region", action="append", default=[], metavar="NAME:X,Y,W,H")
    args = parser.parse_args()
    try:
        left = read_ppm(Path(args.left))
        right = read_ppm(Path(args.right))
        prefix = Path(args.out)
        write_ppm(Path(f"{prefix}-side-by-side.ppm"), side_by_side(left, right))
        write_ppm(Path(f"{prefix}-diff.ppm"), difference(left, right, args.diff_gain))
        report = {"whole": compare(left, right), "tiles": [], "regions": {}}
        if args.tiles:
            report["tiles"] = tile_metrics(left, right, args.tiles)
        for value in args.region:
            name, region = parse_region(value)
            report["regions"][name] = compare(left, right, region)
        Path(f"{prefix}-metrics.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        whole = report["whole"]
        print(
            f"rmse {whole['rmse']:.3f} mean {whole['mean_absolute']:.3f} "
            f"max {whole['max_difference']} changed {whole['changed_ratio']:.2%}"
        )
        return 0
    except (OSError, ValueError) as error:
        print(f"compare_frames: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
