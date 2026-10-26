import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

from big import Entry, build, parse  # noqa: E402


def build_big(entries):
    names = [entry[0] for entry in entries]
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
    for index, entry in enumerate(entries):
        name, type_name, payload = entry[:3]
        values = entry[3] if len(entry) > 3 else ()
        values_blob = b"".join(struct.pack("<H", value) for value in values)
        symbols_size = (20 + len(values_blob) + 3) & ~3
        body = (
            b"symlist\0"
            + struct.pack("<I", symbols_size)
            + struct.pack("<I", 0)
            + struct.pack("<I", len(values))
            + values_blob
            + b"\0" * (symbols_size - 20 - len(values_blob))
            + struct.pack("<I", len(payload))
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


def entry_start(data):
    return 0x10 + struct.unpack_from("<I", data, 0x18)[0]


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

    def test_reads_unaligned_payload(self):
        data = build_big([("a", "model", b"abcde")])
        _, entries = parse(data)
        self.assertEqual(entries[0].size, 5)
        self.assertEqual(entries[0].data, b"abcde")
        self.assertEqual(entries[0].offset, entry_start(data) + 44)

    def test_reads_empty_payload(self):
        data = build_big([("a", "region", b"")])
        _, entries = parse(data)
        self.assertEqual(entries[0].size, 0)
        self.assertEqual(entries[0].data, b"")

    def test_rejects_bad_magic(self):
        with self.assertRaises(ValueError):
            parse(b"NOPE" + b"\0" * 64)

    def test_rejects_truncated_header(self):
        with self.assertRaises(ValueError):
            parse(b"goefile\0" + b"\0" * 8)

    def test_rejects_bad_directory_length(self):
        data = bytearray(build_big([("a", "model", b"abc")]))
        struct.pack_into("<I", data, 0x18, 8)
        with self.assertRaisesRegex(ValueError, "directory"):
            parse(bytes(data))

    def test_rejects_directory_past_end(self):
        data = bytearray(build_big([("a", "model", b"abc")]))
        struct.pack_into("<I", data, 0x18, len(data))
        with self.assertRaisesRegex(ValueError, "directory"):
            parse(bytes(data))

    def test_rejects_bad_entry_length(self):
        data = bytearray(build_big([("a", "model", b"abc")]))
        struct.pack_into("<I", data, entry_start(data) + 8, 0xFFFFFFFF)
        with self.assertRaisesRegex(ValueError, "entry length"):
            parse(bytes(data))

    def test_reads_symbol_values(self):
        data = build_big([("a", "texture", b"DDS " + b"\0" * 4, (7, 8, 9, 10))])
        _, entries = parse(data)
        self.assertEqual(entries[0].size, 8)
        self.assertEqual(entries[0].data[:4], b"DDS ")

    def test_rejects_bad_symbols_size(self):
        data = bytearray(build_big([("a", "model", b"abc")]))
        struct.pack_into("<I", data, entry_start(data) + 28, 0x1000)
        with self.assertRaisesRegex(ValueError, "symbol list"):
            parse(bytes(data))

    def test_rejects_data_length_over_entry(self):
        data = bytearray(build_big([("a", "model", b"abc")]))
        struct.pack_into("<I", data, entry_start(data) + 40, 0x1000)
        with self.assertRaisesRegex(ValueError, "data length"):
            parse(bytes(data))

    def test_rejects_truncated_entry(self):
        data = build_big([("a", "model", b"abcdefgh")])
        with self.assertRaisesRegex(ValueError, "entry length"):
            parse(data[:-4])

    def test_build_round_trip(self):
        data = build_big(
            [
                ("sl01/level", "model", b"MODELDATA"),
                ("sl01/texture", "texture", b"DDS " + b"\0" * 12),
            ]
        )
        names, entries = parse(data)
        self.assertEqual(build(names, entries), data)

    def test_build_preserves_symbols_and_language(self):
        data = build_big([("a", "texture", b"DDS " + b"\0" * 4, (7, 8, 9, 10))])
        names, entries = parse(data)
        rebuilt = Entry(
            entries[0].type,
            entries[0].name,
            entries[0].language,
            entries[0].size,
            0,
            entries[0].data,
            entries[0].symbols,
            entries[0].name_index,
        )
        self.assertEqual(build(names, [rebuilt]), data)
        _, second = parse(build(names, [rebuilt]))
        self.assertEqual(second[0].symbols, (7, 8, 9, 10))

    def test_build_rejects_bad_type(self):
        data = build_big([("a", "model", b"abc")])
        names, entries = parse(data)
        entries[0].type = "toolongtype"
        with self.assertRaises(ValueError):
            build(names, entries)

    def test_rejects_trailing_junk(self):
        data = build_big([("a", "model", b"abc")])
        with self.assertRaises(ValueError):
            parse(data + b"JUNK")


if __name__ == "__main__":
    unittest.main()
