import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import audio  # noqa: E402
import big  # noqa: E402


def sound_payload(records, name="default", flags=2, remainder=b""):
    body = b"".join(struct.pack("<HHI", key, field, value) for key, field, value in records)
    body += remainder
    payload = struct.pack("<II", 1, flags) + body
    data = name.encode("latin1") + b"\0" + struct.pack("<I", len(payload) + len(name) + 5)
    return data + payload


class AudioTest(unittest.TestCase):
    def test_parse_sound(self):
        data = sound_payload([(0, 6, 0x3F800000), (7, 0, 0xFFFF0001)])
        parsed = audio.parse_sound(data)
        self.assertEqual(parsed["name"], "default")
        self.assertEqual(parsed["version"], 1)
        self.assertEqual(parsed["flags"], 2)
        self.assertEqual(len(parsed["records"]), 2)
        self.assertEqual(parsed["records"][0]["field"], 6)
        self.assertEqual(parsed["remainder"], "")

    def test_parse_sound_keeps_remainder(self):
        data = sound_payload([(0, 6, 0)], remainder=b"\x01\x02\x03\x04")
        parsed = audio.parse_sound(data)
        self.assertEqual(parsed["remainder"], "01020304")

    def test_collect_sounds(self):
        data = big.build(
            ["events/a"],
            [big.Entry("sound", "events/a", 0, 0, 0, sound_payload([(0, 6, 1)]))],
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "level.big"
            source.write_bytes(data)
            sounds = audio.collect_sounds(source)
            self.assertEqual(len(sounds), 1)
            self.assertEqual(sounds[0]["entry"], "events/a")

    def test_collect_strmfiles(self):
        data = big.build(
            ["events/a"],
            [big.Entry("strmfile", "events/a", 0, 0, 0, b"model\ta\r\n")],
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "level.big"
            source.write_bytes(data)
            out = root / "out"
            self.assertEqual(audio.collect_strmfiles(source, out), 1)
            self.assertTrue((out / "0000_events_a.etp").is_file())


if __name__ == "__main__":
    unittest.main()
