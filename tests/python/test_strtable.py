import contextlib
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import big  # noqa: E402
import strtable  # noqa: E402
from big import Entry  # noqa: E402

LANGUAGES = ["default", "english", "french", "italian", "german", "spanish"]
# Name indices 6..11 of the synthetic container.
NAMES = LANGUAGES + ["ui/ui", "key_one", "uno", "key_two", "TCUVWXY", "one"]
END = 0xFFFF


def payload(words, size=None):
    body = struct.pack("<%dH" % len(words), *words)
    total = 20 + len(body) if size is None else size
    return b"default\0" + struct.pack("<III", total, 1, 2) + body


def entry_of(words, symbols, language=5, data=None):
    data = payload(words) if data is None else data
    return Entry("strtable", "ui/ui", language, len(data), 0, data, symbols)


def parse_one(words, symbols):
    names, entries = big.parse(big.build(NAMES, [entry_of(words, symbols)]))
    return strtable.parse_strtable(names, entries[0])


class ParseStrtableTest(unittest.TestCase):
    def test_reads_records(self):
        # symbols: ref0 key_one, ref1 uno, ref2 key_two, ref3 TCUVWXY
        records, irregular = parse_one([7, 0, 1, END, 7, 2, 3, END], (7, 8, 9, 10))
        self.assertEqual(records, [("key_one", "uno"), ("key_two", "TCUVWXY")])
        self.assertEqual(irregular, [])

    def test_keys_may_share_a_value(self):
        records, _ = parse_one([7, 0, 1, END, 7, 2, 1, END], (7, 8, 9, 10))
        self.assertEqual(records, [("key_one", "uno"), ("key_two", "uno")])

    def test_irregular_record_keeps_alignment(self):
        # The shape seen once in the Italian ui/UI: not [7, key, value].
        words = [7, 0, 1, END, 0x0107, 2, 3, 0, 1, 3, 7, 0, 3, END, 7, 2, 3, END]
        records, irregular = parse_one(words, (7, 8, 9, 10))
        self.assertEqual(records, [("key_one", "uno"), ("key_two", "TCUVWXY")])
        self.assertEqual(irregular, [(0x0107, 2, 3, 0, 1, 3, 7, 0, 3)])

    def test_lone_terminator_is_an_empty_irregular_record(self):
        records, irregular = parse_one([END, 7, 0, 1, END], (7, 8))
        self.assertEqual(records, [("key_one", "uno")])
        self.assertEqual(irregular, [()])

    def test_rejects_references_out_of_range(self):
        with self.assertRaisesRegex(strtable.StrTableError, "symbol list"):
            parse_one([7, 0, 2, END], (7, 8))
        with self.assertRaisesRegex(strtable.StrTableError, "name table"):
            parse_one([7, 0, 1, END], (7, 500))

    def test_rejects_missing_terminator(self):
        with self.assertRaisesRegex(strtable.StrTableError, "terminator"):
            parse_one([7, 0, 1], (7, 8))

    def test_rejects_bad_header(self):
        names = NAMES
        good = payload([7, 0, 1, END])
        for data, message in (
            (b"x" + good[1:], "not a strtable"),
            (good[:12], "not a strtable"),
            (payload([7, 0, 1, END], size=999), "size"),
            (payload([7, 0, 1, END], size=29) + b"\0", "truncated"),
        ):
            with self.subTest(message=message):
                entry = entry_of([], (7, 8), data=data)
                with self.assertRaisesRegex(strtable.StrTableError, message):
                    strtable.parse_strtable(names, entry)


class DisplayTextTest(unittest.TestCase):
    def test_spanish_accent_placeholders(self):
        self.assertEqual(strtable.display_text("TCUVWXY", "spanish"), "áéíóúñ¿")
        self.assertEqual(strtable.display_text("TCUVWXY", "SPANISH"), strtable.display_text("TCUVWXY", "spanish"))

    def test_other_text_is_untouched(self):
        for text in ("", "a-z (1).?", "@c\\x %d: q"):
            self.assertEqual(strtable.display_text(text, "spanish"), text)

    def test_locales_without_a_table_return_the_stored_text(self):
        self.assertEqual(strtable.display_text("TCUVWXY", "english"), "TCUVWXY")
        self.assertEqual(strtable.display_text("TCUVWXY", "default"), "TCUVWXY")

    def test_default_locale_is_spanish(self):
        self.assertEqual(strtable.display_text("W"), "ú")


class DumpCommandTest(unittest.TestCase):
    def run_dump(self, *argv):
        original = sys.argv
        sys.argv = ["strtable.py", "dump", *argv]
        # The command switches stdout to UTF-8, so it needs a binary-backed wrapper.
        buffer = io.BytesIO()
        wrapper = io.TextIOWrapper(buffer, encoding="utf-8", newline="\n")
        try:
            with contextlib.redirect_stdout(wrapper):
                code = strtable.main()
            wrapper.flush()
        finally:
            sys.argv = original
        return code, buffer.getvalue().decode("utf-8")

    def test_dump_selects_the_locale_entry(self):
        spanish = entry_of([7, 0, 1, END, 7, 2, 3, END], (7, 8, 9, 10), language=5)
        default = entry_of([7, 0, 1, END], (7, 11), language=0)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "t.big"
            path.write_bytes(big.build(NAMES, [default, spanish]))
            code, text = self.run_dump(str(path), "--entry", "UI/UI")
            self.assertEqual(code, 0)
            self.assertIn("language=5 (spanish) records=2 irregular=0", text)
            self.assertIn("key_one\tuno\n", text)
            self.assertIn("key_two\táéíóúñ¿\n", text)

            code, text = self.run_dump(str(path), "--entry", "ui/ui", "--raw")
            self.assertIn("key_two\tTCUVWXY\n", text)

            code, text = self.run_dump(str(path), "--entry", "ui/ui", "--locale", "german")
            self.assertIn("language=0 (default)", text)
            self.assertIn("key_one\tone\n", text)


if __name__ == "__main__":
    unittest.main()
