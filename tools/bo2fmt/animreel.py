#!/usr/bin/env python3
"""Reference reader for Blood Omen 2 `animreel` entries (docs/FORMATS/animreel.md).

    python3 tools/bo2fmt/animreel.py info <entry file>
    python3 tools/bo2fmt/animreel.py root <container.big> [--match text]

`info` prints the clip header of an extracted `animreel` entry. `root` reads
every `animreel` entry of a container and prints the node 0 translation
(root motion): the displacement over one cycle and the speed it implies.

Only inner version 6 is decoded (FUN_00668700). Channel codecs: AnimChanUC,
AnimChanLSC, AnimChanLC and constants are evaluated per frame; AnimChanStream
carries no decoded samples.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

import big

TAG = b"animreel"
CHUNK_VERSION = 4  # FUN_0066aa20 also accepts 1, 3, 0x10001 and 0x20001
INNER_VERSION = 6  # FUN_00668700
FRAME_TIME = 1.0 / 30.0  # double at 0x872498 (FUN_0068ada0, FUN_0068a9b0)
# The codec constructors (FUN_0067ad20, FUN_0068aca0, FUN_0068b1f0) take the
# frame count as a ushort and the reel keeps it as a short (the reel+0x12 copy
# in FUN_0063c480).
MAX_FRAMES = 65535
# Port-only sanity limit on decoded samples per reel, so small LSC channels
# cannot amplify a file into gigabytes. Not an original limit.
MAX_SAMPLES = 16 * 1024 * 1024

CODEC_UC = 0  # AnimChanUC, FUN_0067ad20
CODEC_LSC = 8  # AnimChanLSC, FUN_0068b1f0
CODEC_LC = 12  # AnimChanLC, FUN_0068aca0
CODEC_STREAM = 16  # AnimChanStream, FUN_006712c0

ROOT_NODE = 0
TRANSLATION = 1  # inferred: matches the oracle, see docs/FORMATS/animreel.md


class AnimReelError(ValueError):
    pass


@dataclass
class Channel:
    node: int
    type: int
    component: int
    kind: str  # "const", "uc", "lsc", "lc" or "stream"
    values: list = field(default_factory=list)  # per frame (empty for const and stream)
    constant: float = 0.0

    def at(self, frame: int) -> float:
        return self.constant if self.kind == "const" else self.values[frame]


@dataclass
class Reel:
    inner_version: int
    unknown: int  # reel+8, 0 in every Kain clip
    events: list  # (symbol id, frame)
    fps: float
    frames: int
    channels: list

    @property
    def duration(self) -> float:
        # FUN_0068ada0 uses the 1/30 constant, not the fps field.
        return (self.frames - 1) * FRAME_TIME


def _need(data: bytes, offset: int, length: int, what: str) -> None:
    if offset + length > len(data):
        raise AnimReelError(f"truncated {what}")


def _f32(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def _align4(value: int) -> int:
    return (value + 3) & ~3


def _lsc_values(blob: bytes, cursor: int, frames: int) -> tuple[list, int]:
    # Layout of AnimChanLSC: f32 offset, f32 scale, f32 start, u16 K, K x u16.
    _need(blob, cursor, 14, "LSC header")
    offset, scale, start, count = struct.unpack_from("<fffH", blob, cursor)
    size = _align4(count * 2 + 2) + 12
    _need(blob, cursor, size, "LSC channel")
    segments = struct.unpack_from(f"<{count}H", blob, cursor + 14)
    return _lsc_expand(frames, offset, scale, start, segments), cursor + size


def _lsc_expand(frames: int, offset: float, scale: float, start: float, segments) -> list:
    """FUN_0068aed0 at every integer frame (fraction 0), in one pass: O(frames + K).

    Past the last segment this port clamps to it and keeps extrapolating. In
    the original the loop advances to index K and reads the u16 one past the
    segment array (padding or the next channel's data) before its `K <= index`
    check. A zero-length segment divides by zero in the original; the port
    returns the running value. No Kain clip reaches either path.
    """
    if not segments:
        return [start] * frames
    index = 0
    value = start
    base = 0  # frames covered by the segments before `index`
    length = segments[0] >> 11
    delta = (segments[0] & 0x7FF) * scale + offset
    out = []
    for frame in range(frames):
        while length < frame - base and index + 1 < len(segments):
            value += delta
            base += length
            index += 1
            length = segments[index] >> 11
            delta = (segments[index] & 0x7FF) * scale + offset
        out.append(value if length == 0 else value + (frame - base) * delta / length)
    return out


def parse(data: bytes) -> Reel:
    _need(data, 0, 28, "header")
    if data[:8] != TAG:
        raise AnimReelError("not an animreel chunk")
    size, chunk_version = struct.unpack_from("<II", data, 8)
    if size != len(data):
        raise AnimReelError(f"chunk size {size} does not match payload size {len(data)}")
    if chunk_version != CHUNK_VERSION:
        raise AnimReelError(f"unsupported chunk version {chunk_version}")
    inner, unknown, event_count = struct.unpack_from("<III", data, 16)
    if inner != INNER_VERSION:
        raise AnimReelError(f"unsupported inner version {inner}")
    cursor = 28
    _need(data, cursor, event_count * 8, "events")
    events = [struct.unpack_from("<If", data, cursor + 8 * i) for i in range(event_count)]
    cursor += event_count * 8
    _need(data, cursor, 16, "clip header")
    fps, frames, desc_count, blob_size = struct.unpack_from("<fIII", data, cursor)
    cursor += 16
    if frames == 0 or frames > MAX_FRAMES:
        raise AnimReelError(f"frame count {frames} is outside 1..{MAX_FRAMES}")
    _need(data, cursor, blob_size, "sample blob")
    blob = data[cursor:cursor + blob_size]
    cursor += blob_size
    _need(data, cursor, desc_count * 8, "descriptors")
    descriptors = [struct.unpack_from("<HBBI", data, cursor + 8 * i) for i in range(desc_count)]
    cursor += desc_count * 8
    if cursor != len(data):
        raise AnimReelError(f"{len(data) - cursor} trailing bytes after the descriptors")

    channels = []
    blob_cursor = 0
    samples_left = MAX_SAMPLES
    for node, kind, component, value in descriptors:
        if value & 1:
            # Constant: the float is the value's bit pattern, tag bit included.
            channels.append(Channel(node, kind, component, "const", constant=_f32(value)))
        elif value in (CODEC_UC, CODEC_LSC, CODEC_LC):
            samples_left -= frames
            if samples_left < 0:
                raise AnimReelError("decoded sample limit exceeded")
            if value == CODEC_UC:
                _need(blob, blob_cursor, frames * 4, "UC channel")
                samples = list(struct.unpack_from(f"<{frames}f", blob, blob_cursor))
                blob_cursor += frames * 4
                channels.append(Channel(node, kind, component, "uc", samples))
            elif value == CODEC_LSC:
                samples, blob_cursor = _lsc_values(blob, blob_cursor, frames)
                channels.append(Channel(node, kind, component, "lsc", samples))
            else:
                size_lc = _align4(frames + 8)
                _need(blob, blob_cursor, size_lc, "LC channel")
                offset, scale = struct.unpack_from("<ff", blob, blob_cursor)
                raw = blob[blob_cursor + 8:blob_cursor + 8 + frames]
                samples = [byte * scale + offset for byte in raw]
                blob_cursor += size_lc
                channels.append(Channel(node, kind, component, "lc", samples))
        elif value == CODEC_STREAM:
            _need(blob, blob_cursor, 4, "stream channel")
            blob_cursor += 4
            channels.append(Channel(node, kind, component, "stream"))
        else:
            raise AnimReelError(f"unknown channel codec {value}")
    if blob_cursor != blob_size:
        raise AnimReelError(f"blob has {blob_size - blob_cursor} unread bytes")
    return Reel(inner, unknown, events, fps, frames, channels)


def root_displacement(reel: Reel) -> tuple[float, float, float]:
    """Root translation over one cycle: last frame minus first frame (x, y, z)."""
    out = [0.0, 0.0, 0.0]
    for channel in reel.channels:
        if (channel.node == ROOT_NODE and channel.type == TRANSLATION
                and channel.component < 3 and channel.kind != "stream"):
            out[channel.component] = channel.at(reel.frames - 1) - channel.at(0)
    return tuple(out)


def root_speed(reel: Reel) -> tuple[float, float, float]:
    duration = reel.duration
    if duration <= 0.0:
        raise AnimReelError("clip has no duration")
    return tuple(delta / duration for delta in root_displacement(reel))


def cmd_info(args) -> int:
    reel = parse(Path(args.entry).read_bytes())
    codecs: dict[str, int] = {}
    for channel in reel.channels:
        codecs[channel.kind] = codecs.get(channel.kind, 0) + 1
    print(f"inner version {reel.inner_version}")
    print(f"fps {reel.fps:g}")
    print(f"frames {reel.frames}")
    print(f"duration {reel.duration:.4f} s")
    print(f"events {len(reel.events)}")
    for symbol, frame in reel.events:
        print(f"  symbol {symbol} at frame {frame:g}")
    print(f"descriptors {len(reel.channels)}")
    for kind in sorted(codecs):
        print(f"  {kind} {codecs[kind]}")
    return 0


def cmd_root(args) -> int:
    _, entries = big.load(Path(args.container))
    needle = args.match.lower()
    for entry in entries:
        if entry.type != "animreel" or needle not in entry.name.lower():
            continue
        try:
            reel = parse(entry.data)
            displacement = root_displacement(reel)
            speed = root_speed(reel) if reel.duration > 0.0 else None
        except AnimReelError as error:
            print(f"error {entry.name}: {error}")
            continue
        text = "n/a (single frame)" if speed is None else (
            f"{speed[0]:.4f} {speed[1]:.4f} {speed[2]:.4f} u/s")
        print(f"{entry.name} frames {reel.frames} duration {reel.duration:.4f} s "
              f"displacement {displacement[0]:.4f} {displacement[1]:.4f} {displacement[2]:.4f} "
              f"speed {text}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Read Blood Omen 2 animreel entries")
    sub = parser.add_subparsers(dest="command", required=True)
    p_info = sub.add_parser("info", help="print the header of an extracted animreel entry")
    p_info.add_argument("entry")
    p_info.set_defaults(func=cmd_info)
    p_root = sub.add_parser("root", help="print root motion for every animreel in a container")
    p_root.add_argument("container")
    p_root.add_argument("--match", default="", help="case-insensitive entry name filter")
    p_root.set_defaults(func=cmd_root)
    args = parser.parse_args()
    try:
        return args.func(args)
    except (AnimReelError, big.BigError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
