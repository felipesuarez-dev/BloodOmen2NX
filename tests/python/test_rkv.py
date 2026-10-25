import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

from rkv import parse  # noqa: E402

SECTOR = 2048


def record(name, sector, size, flag=1):
    raw = name.encode("latin1")
    return (
        struct.pack("<I", len(raw))
        + raw
        + struct.pack("<III", flag, sector, size)
        + struct.pack("<I", 0)
    )


def build_index(groups):
    body = bytearray(struct.pack("<I", len(groups)))
    for language, sections in groups:
        raw = language.encode("latin1")
        body += struct.pack("<I", len(raw)) + raw
        body += struct.pack("<I", len(sections))
        for section, records in sections:
            raw = section.encode("latin1")
            body += struct.pack("<I", len(raw)) + raw
            body += struct.pack("<I", len(records)) + struct.pack("<I", 0)
            for item in records:
                body += item
    return bytes(body)


def build_rkv(groups, index_size=None, data=b""):
    body = build_index(groups)
    if index_size is None:
        index_size = len(body)
    data_start = 8 + index_size + 0x800
    out = bytearray(struct.pack("<II", 1, index_size) + body)
    if len(out) < data_start:
        out += b"\0" * (data_start - len(out))
    out += data
    return bytes(out)


class RkvTest(unittest.TestCase):
    def test_reads_entries(self):
        payloads = [b"OggS" + b"a" * 20, b"OggS" + b"b" * 20]
        data = build_rkv(
            [
                (
                    "english",
                    [
                        (
                            "strstrem",
                            [
                                record("vo/SL02_UMAH_01", 0, len(payloads[0])),
                                record("music/wav/sl01", 1, len(payloads[1])),
                            ],
                        )
                    ],
                )
            ],
            data=b"".join(p.ljust(SECTOR, b"\0") for p in payloads),
        )
        entries = parse(data)
        self.assertEqual([e.name for e in entries], ["vo/SL02_UMAH_01", "music/wav/sl01"])
        self.assertEqual(entries[0].section, "strstrem")
        self.assertEqual(entries[0].language, "english")
        self.assertEqual(data[entries[0].offset : entries[0].offset + 4], b"OggS")
        self.assertEqual(data[entries[1].offset : entries[1].offset + 4], b"OggS")

    def test_reads_multiple_languages_and_sections(self):
        payloads = [b"OggSone", b"OggStwo"]
        data = build_rkv(
            [
                ("english", [("strstrem", [record("a", 0, len(payloads[0]))])]),
                ("spanish", [("strstrem", [record("b", 1, len(payloads[1]))])]),
            ],
            data=b"".join(p.ljust(SECTOR, b"\0") for p in payloads),
        )
        entries = parse(data)
        self.assertEqual([(e.language, e.name) for e in entries], [("english", "a"), ("spanish", "b")])

    def test_reads_empty_archive(self):
        self.assertEqual(parse(build_rkv([])), [])

    def test_rejects_bad_version(self):
        with self.assertRaises(ValueError):
            parse(struct.pack("<I", 2) + b"\0" * 32)

    def test_rejects_truncated_header(self):
        with self.assertRaises(ValueError):
            parse(struct.pack("<I", 1) + b"\0" * 4)

    def test_rejects_index_past_end(self):
        with self.assertRaisesRegex(ValueError, "index"):
            parse(struct.pack("<II", 1, 0x1000) + b"\0" * 16)

    def test_rejects_truncated_string(self):
        body = struct.pack("<I", 1) + struct.pack("<I", 100) + b"abc"
        with self.assertRaisesRegex(ValueError, "truncated string"):
            parse(struct.pack("<II", 1, len(body)) + body)

    def test_rejects_index_overrun(self):
        body = build_index([("english", [])])
        with self.assertRaisesRegex(ValueError, "index overruns"):
            parse(build_rkv([("english", [])], index_size=len(body) - 4))

    def test_rejects_bad_entry_flag(self):
        data = build_rkv(
            [("english", [("strstrem", [record("a", 0, 4, flag=2)])])],
            data=b"OggS".ljust(SECTOR, b"\0"),
        )
        with self.assertRaisesRegex(ValueError, "entry flag"):
            parse(data)

    def test_rejects_entry_past_end(self):
        data = build_rkv(
            [("english", [("strstrem", [record("a", 100, 4)])])],
            data=b"OggS".ljust(SECTOR, b"\0"),
        )
        with self.assertRaisesRegex(ValueError, "out of range"):
            parse(data)

    def test_rejects_entry_size_past_end(self):
        data = build_rkv(
            [("english", [("strstrem", [record("a", 0, 4096)])])],
            data=b"OggS".ljust(SECTOR, b"\0"),
        )
        with self.assertRaisesRegex(ValueError, "out of range"):
            parse(data)


if __name__ == "__main__":
    unittest.main()
