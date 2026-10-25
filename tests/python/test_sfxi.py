import struct
import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

from sfxi import read_entries  # noqa: E402


def build(entries, truncated_last=False):
    body = bytearray()
    for name, payload in entries:
        name_bytes = name.encode("latin1")
        assert len(name_bytes) < 256
        blob = zlib.compress(payload)
        body += name_bytes.ljust(256, b"\0")
        body += struct.pack("<I", len(blob))
        body += blob
    if truncated_last:
        body = body[:-3]
    total = 12 + len(body)
    return b"SFXI" + struct.pack("<I", total) + struct.pack("<I", 24) + bytes(body)


class SfxiTest(unittest.TestCase):
    def test_reads_two_entries(self):
        data = build([("a.txt", b"hello"), ("dir/b.bin", b"\x00\x01\x02")])
        entries = read_entries(data)
        self.assertEqual([e.name for e in entries], ["a.txt", "dir/b.bin"])
        self.assertEqual(entries[0].payload, b"hello")
        self.assertTrue(entries[0].compressed)
        self.assertEqual(entries[1].payload, b"\x00\x01\x02")
        self.assertFalse(entries[0].truncated)

    def test_truncated_last_entry_is_reported(self):
        data = build([("a.txt", b"hello"), ("b.txt", b"world")], truncated_last=True)
        entries = read_entries(data)
        self.assertEqual(entries[0].payload, b"hello")
        self.assertEqual(len(entries), 2)
        self.assertTrue(entries[1].truncated)

    def test_rejects_bad_magic(self):
        with self.assertRaises(ValueError):
            read_entries(b"NOPE" + b"\0" * 32)


if __name__ == "__main__":
    unittest.main()
