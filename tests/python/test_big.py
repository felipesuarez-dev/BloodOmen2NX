import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

from big import parse  # noqa: E402


def build_big(entries):
    names = [name for name, _, _ in entries]
    name_blob = b"".join(name.encode("latin1") + b"\0" for name in names)
    directory = (
        b"symlist\0"
        + struct.pack("<I", 0)
        + struct.pack("<I", 0)
        + struct.pack("<I", len(names))
        + name_blob
    )
    if len(directory) & 1:
        directory += b"\0"
    directory_length = len(directory)
    directory = directory[:8] + struct.pack("<I", directory_length) + directory[12:]
    header = (
        b"goefile\0"
        + struct.pack("<I", 0)
        + struct.pack("<I", 1)
        + directory
    )
    out = bytearray(header)
    for index, (_, type_name, payload) in enumerate(entries):
        body = (
            b"symlist\0"
            + struct.pack("<I", 20)
            + struct.pack("<I", 0)
            + struct.pack("<I", 0)
            + struct.pack("<I", len(payload))
            + b"\0" * 8
            + payload
        )
        entry_length = 20 + len(body)
        pad = (-entry_length) % 4
        body += b"\0" * pad
        entry_length += pad
        out += (
            type_name.encode("latin1").ljust(8, b"\0")
            + struct.pack("<I", entry_length)
            + struct.pack("<I", 0)
            + struct.pack("<H", index)
            + struct.pack("<H", 0)
            + body
        )
    struct.pack_into("<I", out, 8, len(out))
    return bytes(out)


class BigTest(unittest.TestCase):
    def test_reads_entries(self):
        data = build_big(
            [
                ("sl01/level", "model", b"MODELDATA"),
                ("sl01/texture", "texture", b"DDS " + b"\0" * 12),
            ]
        )
        names, entries = parse(data)
        self.assertEqual(names, ["sl01/level", "sl01/texture"])
        self.assertEqual([e.type for e in entries], ["model", "texture"])
        self.assertEqual([e.name for e in entries], names)
        self.assertEqual(entries[0].data, b"MODELDATA")
        self.assertEqual(entries[1].data[:4], b"DDS ")

    def test_rejects_bad_magic(self):
        with self.assertRaises(ValueError):
            parse(b"NOPE" + b"\0" * 64)


if __name__ == "__main__":
    unittest.main()
