import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import big  # noqa: E402
from big import Entry, find_localized, language_name  # noqa: E402

LANGUAGES = ["default", "english", "french", "italian", "german", "spanish"]


def container(items, extra_names=("ui/ui", "vo/line")):
    """Build a synthetic container; items are (type, name, language, payload)."""
    names = LANGUAGES + list(extra_names)
    entries = [Entry(t, n, lang, len(p), 0, p) for t, n, lang, p in items]
    return big.parse(big.build(names, entries))


class LocaleSelectionTest(unittest.TestCase):
    def test_language_names_index_the_name_table(self):
        names, entries = container([("strtable", "ui/ui", 5, b"E"), ("strtable", "ui/ui", 99, b"X")])
        self.assertEqual(language_name(names, entries[0]), "spanish")
        self.assertEqual(language_name(names, entries[1]), "")

    def test_locale_wins_over_default(self):
        # code.big order: languages 1..5 first, the default copy last.
        names, entries = container(
            [
                ("strtable", "ui/ui", 1, b"E"),
                ("strtable", "ui/ui", 2, b"F"),
                ("strtable", "ui/ui", 5, b"S"),
                ("strtable", "ui/ui", 0, b"D"),
            ]
        )
        self.assertEqual(find_localized(names, entries, "strtable", "ui/ui").data, b"S")
        self.assertEqual(find_localized(names, entries, "STRTABLE", "UI/UI", "SPANISH").data, b"S")
        self.assertEqual(find_localized(names, entries, "strtable", "ui/ui", "french").data, b"F")
        self.assertEqual(find_localized(names, entries, "strtable", "ui/ui", "english").data, b"E")

    def test_default_is_the_fallback(self):
        names, entries = container(
            [
                ("lipsync", "vo/line", 2, b"F"),
                ("lipsync", "vo/line", 0, b"D"),
                ("lipsync", "vo/line", 5, b"S"),
            ]
        )
        for locale in ("german", "default", "klingon"):
            self.assertEqual(find_localized(names, entries, "lipsync", "vo/line", locale).data, b"D")

    def test_other_languages_are_skipped(self):
        names, entries = container([("texture", "ui/ui", 2, b"F")])
        self.assertIsNone(find_localized(names, entries, "texture", "ui/ui", "spanish"))
        self.assertEqual(find_localized(names, entries, "texture", "ui/ui", "french").data, b"F")
        self.assertIsNone(find_localized(names, entries, "texture", "missing"))

    def test_default_locale_is_spanish(self):
        self.assertEqual(big.DEFAULT_LOCALE, "spanish")


if __name__ == "__main__":
    unittest.main()
