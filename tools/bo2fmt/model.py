#!/usr/bin/env python3
"""Inspect and extract Blood Omen 2 model and skeleton chunks.

    python3 tools/bo2fmt/model.py inspect <container.big> [--out models.json]
    python3 tools/bo2fmt/model.py extract <container.big> --out <dir>

A shared `model` entry starts with a u32 version, a NUL-terminated record
name, the block size, a zero u32 and the chunk list. The `rmd` chunk
holds the render records; `skel`, `vtxweigh` and `jointidx` are parsed
here. `animatio` and `animreel` entries are copied as raw payloads by
`extract`; their keyframe encoding is still open.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

import big


class ModelError(ValueError):
    pass


def _u32(data: bytes, offset: int) -> int:
    if offset + 4 > len(data):
        raise ModelError("truncated u32")
    return struct.unpack_from("<I", data, offset)[0]


def _cstr(data: bytes, offset: int, limit: int) -> tuple[str, int]:
    end = data.find(b"\0", offset, limit)
    if end < 0:
        raise ModelError("unterminated model name")
    return data[offset:end].decode("latin1"), end + 1


def _chunks(data: bytes, start: int, end: int) -> dict[str, bytes]:
    result: dict[str, bytes] = {}
    position = start
    while position + 16 <= end:
        name = data[position : position + 8].rstrip(b"\0").decode("latin1")
        size = _u32(data, position + 8)
        if size < 16 or position + size > end:
            raise ModelError(f"bad chunk {name!r} at {position:#x}")
        result[name] = data[position + 16 : position + size]
        position += size
    if position != end:
        raise ModelError("model chunk area has a trailer")
    return result


def parse_rmd_subchunks(data: bytes) -> dict[str, bytes]:
    if len(data) < 20 or _u32(data, 0) != 2:
        raise ModelError("bad rmd header")
    name, offset = _cstr(data, 4, len(data))
    size = _u32(data, offset)
    end = 4 + size
    if size < 16 or end > len(data):
        raise ModelError("bad rmd record size")
    return _chunks(data, offset + 8, end)


def parse_skeleton(chunks: dict[str, bytes]) -> dict:
    result = {"bones": [], "bind_pose": b"", "weights": [], "joints": []}
    skel = chunks.get("skel") or chunks.get("skeleton") or chunks.get("modlskel")
    if skel:
        if len(skel) < 4:
            raise ModelError("skel chunk is shorter than its count")
        count = _u32(skel, 0)
        used = 4 + count * 8
        if used > len(skel):
            raise ModelError("skel bone array overruns the chunk")
        result["bones"] = [
            list(struct.unpack_from("<II", skel, 4 + index * 8)) for index in range(count)
        ]
        result["bind_pose"] = skel[used:]
    weights = chunks.get("vtxweigh")
    if weights:
        if len(weights) < 4:
            raise ModelError("vtxweigh chunk is shorter than its count")
        count = _u32(weights, 0)
        if 4 + count * 4 > len(weights):
            raise ModelError("vtxweigh array overruns the chunk")
        result["weights"] = list(struct.unpack_from(f"<{count}f", weights, 4))
    joints = chunks.get("jointidx")
    if joints:
        if len(joints) < 4:
            raise ModelError("jointidx chunk is shorter than its count")
        count = _u32(joints, 0)
        if 4 + count * 2 > len(joints):
            raise ModelError("jointidx array overruns the chunk")
        result["joints"] = list(struct.unpack_from(f"<{count}H", joints, 4))
    return result


def parse_model(data: bytes) -> dict:
    if len(data) < 20:
        raise ModelError("model entry is shorter than its header")
    version = _u32(data, 0)
    name, offset = _cstr(data, 4, len(data))
    size = _u32(data, offset)
    end = 4 + size
    if size < 16 or end > len(data):
        raise ModelError("bad model block size")
    chunks_start = offset + 8
    chunks = _chunks(data, chunks_start, end)
    combined = dict(chunks)
    if "rmd" in chunks:
        combined.update(parse_rmd_subchunks(chunks["rmd"]))
    skeleton = parse_skeleton(combined)
    return {
        "version": version,
        "name": name,
        "size": size,
        "chunks": list(chunks),
        "skeleton": {
            "bones": skeleton["bones"],
            "bind_pose_bytes": len(skeleton["bind_pose"]),
            "weights": skeleton["weights"],
            "joints": skeleton["joints"],
        },
    }


def collect_models(path: Path) -> list[dict]:
    _, entries = big.load(path)
    result = []
    for index, entry in enumerate(entries):
        if entry.type != "model":
            continue
        try:
            parsed = parse_model(entry.data)
            parsed["entry"] = entry.name
            parsed["index"] = index
            result.append(parsed)
        except ModelError as error:
            result.append({"entry": entry.name, "index": index, "error": str(error)})
    return result


def extract(path: Path, out: Path) -> int:
    _, entries = big.load(path)
    out.mkdir(parents=True, exist_ok=True)
    written = 0
    for index, entry in enumerate(entries):
        if entry.type not in ("model", "animatio", "animreel"):
            continue
        suffix = {"model": "model", "animatio": "animatio", "animreel": "animreel"}[entry.type]
        name = "".join(
            character if character.isalnum() or character in "._-" else "_"
            for character in entry.name
        ).strip("._")
        target = out / f"{index:04d}_{suffix}_{name or 'entry'}.bin"
        target.write_bytes(entry.data)
        written += 1
    return written


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect Blood Omen 2 models")
    sub = parser.add_subparsers(dest="command", required=True)
    p_inspect = sub.add_parser("inspect", help="parse model and skeleton chunks")
    p_inspect.add_argument("container")
    p_inspect.add_argument("--out")
    p_extract = sub.add_parser("extract", help="copy model and animation payloads")
    p_extract.add_argument("container")
    p_extract.add_argument("--out", required=True)
    args = parser.parse_args()
    try:
        if args.command == "inspect":
            models = collect_models(Path(args.container))
            if args.out:
                Path(args.out).write_text(json.dumps(models, indent=2), encoding="utf-8")
            errors = sum(1 for model in models if "error" in model)
            print(f"models: {len(models)} ({errors} errors)")
            return 0
        if args.command == "extract":
            written = extract(Path(args.container), Path(args.out))
            print(f"extracted {written} model/animation payloads")
            return 0
    except (ModelError, big.BigError, OSError) as error:
        print(f"model.py: {error}", file=sys.stderr)
        return 1
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
