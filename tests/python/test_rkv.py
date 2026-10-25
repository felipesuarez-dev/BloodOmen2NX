import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

from rkv import parse  # noqa: E402


def build_rkv(entries):
    language = b"english"
    section = b"strstrem"
    body = bytearray()
    body += struct.pack("<I", 1)  # language count
    body += struct.pack("<I", len(language)) + language
    body += struct.pack("<I", 1)  # section count
    body += struct.pack("<I", len(section)) + section
    body += struct.pack("<I", len(entries)) + struct.pack("<I", 0)
    for index, (name, payload) in enumerate(entries):
        name_bytes = name.encode("latin1")
        body += struct.pack("<I", len(name_bytes)) + name_bytes
        body += struct.pack("<I", 1)
        body += struct.pack("<I", index)
        body += struct.pack("<I", len(payload))
        body += struct.pack("<I", 0)
    index_size = len(body)
    data_start = 8 + index_size + 0x800
    out = bytearray(struct.pack("<I", 1) + struct.pack("<I", index_size) + body)
    out += b"\0" * (data_start - len(out))
    for index, (_, payload) in enumerate(entries):
        out += payload.ljust(2048, b"\0")
    return bytes(out)


class RkvTest(unittest.TestCase):
    def test_reads_entries(self):
        data = build_rkv(
            [
                ("vo/SL02_UMAH_01", b"OggS" + b"a" * 20),
                ("music/wav/sl01", b"OggS" + b"b" * 20),
            ]
        )
        entries = parse(data)
        self.assertEqual([e.name for e in entries], ["vo/SL02_UMAH_01", "music/wav/sl01"])
        self.assertEqual(entries[0].section, "strstrem")
        self.assertEqual(entries[0].language, "english")
        self.assertEqual(data[entries[0].offset : entries[0].offset + 4], b"OggS")
        self.assertEqual(data[entries[1].offset : entries[1].offset + 4], b"OggS")

    def test_rejects_bad_version(self):
        with self.assertRaises(ValueError):
            parse(struct.pack("<I", 2) + b"\0" * 32)


if __name__ == "__main__":
    unittest.main()
