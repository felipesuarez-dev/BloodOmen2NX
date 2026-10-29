import contextlib
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import animreel  # noqa: E402
import big  # noqa: E402


def f32_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def const_value(value):
    """A float whose bit pattern has bit 0 set, as the descriptors store constants."""
    return f32_bits(value) | 1


def lsc_channel(offset, scale, start, segments):
    body = struct.pack("<fffH", offset, scale, start, len(segments))
    body += struct.pack(f"<{len(segments)}H", *segments)
    return body.ljust(12 + ((len(segments) * 2 + 2 + 3) & ~3), b"\0")


def lc_channel(offset, scale, samples):
    body = struct.pack("<ff", offset, scale) + bytes(samples)
    return body.ljust((len(samples) + 8 + 3) & ~3, b"\0")


def uc_channel(samples):
    return struct.pack(f"<{len(samples)}f", *samples)


def reel(frames, channels, events=(), inner=6, chunk_version=4, extra=b"", fps=30.0):
    """channels: list of (node, type, component, value, blob bytes)."""
    blob = b"".join(c[4] for c in channels)
    body = struct.pack("<III", inner, 0, len(events))
    body += b"".join(struct.pack("<If", symbol, frame) for symbol, frame in events)
    body += struct.pack("<fIII", fps, frames, len(channels), len(blob)) + blob
    body += b"".join(struct.pack("<HBBI", n, t, c, v) for n, t, c, v, _ in channels)
    body += extra
    size = 16 + len(body)
    return b"animreel" + struct.pack("<II", size, chunk_version) + body


class AnimReelTest(unittest.TestCase):
    def test_header(self):
        data = reel(5, [(0, 1, 0, const_value(2.0), b"")], events=[(3, 5.0), (4, 17.0)])
        parsed = animreel.parse(data)
        self.assertEqual(parsed.inner_version, 6)
        self.assertEqual(parsed.frames, 5)
        self.assertEqual(parsed.fps, 30.0)
        self.assertEqual(parsed.events, [(3, 5.0), (4, 17.0)])
        self.assertAlmostEqual(parsed.duration, 4.0 / 30.0)

    def test_constant_keeps_the_tag_bit(self):
        bits = 0x3F800001  # 1.0 plus one ulp: bit 0 is part of the float
        parsed = animreel.parse(reel(3, [(2, 5, 1, bits, b"")]))
        channel = parsed.channels[0]
        self.assertEqual(channel.kind, "const")
        self.assertEqual(f32_bits(channel.constant), bits)
        self.assertEqual(channel.at(2), channel.constant)

    def test_uc_decodes_float_samples(self):
        samples = [1.0, 2.5, -4.0]
        parsed = animreel.parse(reel(3, [(1, 1, 0, 0, uc_channel(samples))]))
        self.assertEqual(parsed.channels[0].kind, "uc")
        self.assertEqual(parsed.channels[0].values, samples)

    def test_lc_decodes_bytes(self):
        data = reel(5, [(1, 1, 2, 12, lc_channel(10.0, 0.5, [0, 2, 4, 6, 8]))])
        values = animreel.parse(data).channels[0].values
        self.assertEqual(values, [10.0, 11.0, 12.0, 13.0, 14.0])

    def test_lsc_decodes_segments(self):
        # Two segments of 2 and 3 frames; deltas are (raw * scale + offset).
        segments = [(2 << 11) | 10, (3 << 11) | 20]
        blob = lsc_channel(0.0, 2.0, 5.0, segments)
        parsed = animreel.parse(reel(6, [(1, 1, 0, 8, blob)]))
        values = parsed.channels[0].values
        expected = [5.0, 15.0, 25.0, 25.0 + 40.0 / 3, 25.0 + 80.0 / 3, 65.0]
        self.assertEqual(len(values), 6)
        for got, want in zip(values, expected):
            self.assertAlmostEqual(got, want, places=9)

    def test_lsc_offset_and_clamp_to_last_segment(self):
        segments = [(2 << 11) | 4]
        blob = lsc_channel(1.0, 0.5, 0.0, segments)  # delta per segment = 4 * 0.5 + 1 = 3
        values = animreel.parse(reel(4, [(1, 1, 0, 8, blob)])).channels[0].values
        # Past the last segment the final one keeps extrapolating.
        self.assertEqual(values, [0.0, 1.5, 3.0, 4.5])

    def test_limits_and_truncation(self):
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(reel(0, []))
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(reel(65536, []))
        animreel.parse(reel(65535, []))
        cut = lsc_channel(0.0, 1.0, 0.0, [1 << 11] * 3)[:-4]
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(reel(3, [(1, 1, 0, 8, cut)]))
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(reel(3, [(1, 1, 0, 8, bytes(8))]))
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(reel(16, [(1, 1, 0, 12, lc_channel(0.0, 1.0, [1, 2]))]))
        events = bytearray(reel(2, []))
        events[24:28] = struct.pack("<I", 0x7FFFFFFF)
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(bytes(events))

    def test_decoded_sample_cap(self):
        tiny = lsc_channel(0.0, 1.0, 1.0, [])
        animreel.parse(reel(65535, [(1, 1, 0, 8, tiny)] * 256))
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(reel(65535, [(1, 1, 0, 8, tiny)] * 257))

    def test_lsc_expands_in_one_pass(self):
        segments = [(1 << 11) | 1] * 65535
        data = reel(65535, [(1, 1, 0, 8, lsc_channel(0.0, 1.0, 0.0, segments))])
        values = animreel.parse(data).channels[0].values
        self.assertEqual(values[65534], 65534.0)

    def test_blob_is_consumed_in_descriptor_order(self):
        channels = [
            (1, 1, 0, 0, uc_channel([1.0, 2.0])),
            (1, 1, 1, const_value(7.0), b""),
            (1, 1, 2, 12, lc_channel(0.0, 1.0, [3, 4])),
            (1, 1, 3, 16, b"\0\0\0\0"),
        ]
        parsed = animreel.parse(reel(2, channels))
        self.assertEqual([c.kind for c in parsed.channels], ["uc", "const", "lc", "stream"])
        self.assertEqual(parsed.channels[2].values, [3.0, 4.0])
        self.assertEqual(parsed.channels[3].values, [])

    def test_unread_blob_bytes_raise(self):
        channels = [(1, 1, 0, 0, uc_channel([1.0, 2.0]) + b"\0\0\0\0")]
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(reel(2, channels))

    def test_short_blob_raises(self):
        channels = [(1, 1, 0, 0, uc_channel([1.0]))]
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(reel(2, channels))

    def test_bad_version_codec_and_trailing_bytes_raise(self):
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(reel(1, [], inner=5))
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(reel(1, [], chunk_version=3))
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(reel(1, [(1, 1, 0, 4, b"")]))
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(reel(1, [], extra=b"\0\0\0\0"))
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(b"notreel!" + reel(1, [])[8:])
        with self.assertRaises(animreel.AnimReelError):
            animreel.parse(reel(1, [])[:-1])

    def test_root_displacement_and_speed(self):
        channels = [
            (0, 1, 0, 8, lsc_channel(0.0, 1.0, 0.0, [(30 << 11) | 30])),  # 0 -> 30 in 30 frames
            (0, 1, 2, const_value(9.0), b""),  # constant z: no displacement
            (0, 19, 0, const_value(100.0), b""),  # other type: ignored
            (3, 1, 1, 0, uc_channel([0.0] * 31)),  # other node: ignored
        ]
        parsed = animreel.parse(reel(31, channels))
        self.assertAlmostEqual(parsed.duration, 1.0)
        self.assertEqual(animreel.root_displacement(parsed), (30.0, 0.0, 0.0))
        self.assertEqual(animreel.root_speed(parsed), (30.0, 0.0, 0.0))

    def test_single_frame_clip_has_no_speed(self):
        parsed = animreel.parse(reel(1, [(0, 1, 0, const_value(1.0), b"")]))
        with self.assertRaises(animreel.AnimReelError):
            animreel.root_speed(parsed)

    def test_cli_info_and_root(self):
        data = reel(31, [(0, 1, 0, 0, uc_channel([float(i) for i in range(31)]))])
        entries = [big.Entry("animreel", "kain/run", 0, 0, 0, data),
                   big.Entry("texture", "other", 0, 0, 0, b"x")]
        container = big.build([e.name for e in entries], entries)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "kain.big"
            path.write_bytes(container)
            entry_path = Path(tmp) / "run.bin"
            entry_path.write_bytes(data)
            args = type("Args", (), {"container": str(path), "match": "run", "entry": str(entry_path)})
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(animreel.cmd_info(args), 0)
                self.assertEqual(animreel.cmd_root(args), 0)
            self.assertIn("kain/run frames 31", out.getvalue())
            self.assertIn("speed 30.0000", out.getvalue())


if __name__ == "__main__":
    unittest.main()
