"""Consistency checks for the progress inputs (tools/gen, re/FUNCTION_MAP.csv, docs/LEVELS.md)."""

import csv
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "gen"))

import progress_data  # noqa: E402


class LevelsTest(unittest.TestCase):
    def test_levels_doc_lists_the_same_chapters_in_the_same_order(self):
        text = (ROOT / "docs" / "LEVELS.md").read_text(encoding="utf-8")
        table = text.split("## Chapter table", 1)[1].split("\n## ", 1)[0]
        rows = re.findall(r"^\|\s*(\d+)\s*\|\s*([^|]+?)\s*\|", table, flags=re.MULTILINE)
        self.assertEqual(
            [(int(number), name) for number, name in rows],
            [(index + 1, name) for index, (name, _) in enumerate(progress_data.LEVELS)],
        )

    def test_level_entries_are_well_formed(self):
        names = [name for name, _ in progress_data.LEVELS]
        self.assertEqual(len(names), len(set(names)))
        for _, completable in progress_data.LEVELS:
            self.assertIsInstance(completable, bool)

    def test_platform_feature_states_are_known(self):
        for name, state in progress_data.PLATFORM_FEATURES:
            self.assertIn(state, {"done", "partial", "todo"}, name)


class FunctionMapTest(unittest.TestCase):
    STATUSES = {"todo", "wip", "translated", "verified", "external"}

    @classmethod
    def setUpClass(cls):
        with (ROOT / "re" / "FUNCTION_MAP.csv").open(newline="", encoding="utf-8") as handle:
            cls.rows = list(csv.DictReader(handle))

    def test_rows_are_unique_sorted_and_use_known_statuses(self):
        addresses = [row["address"] for row in self.rows]
        self.assertEqual(addresses, sorted(addresses))
        self.assertEqual(len(addresses), len(set(addresses)))
        for row in self.rows:
            self.assertIn(row["status"], self.STATUSES, row["address"])
            self.assertEqual(row["name"] == "FUN_" + row["address"], row["name"].startswith("FUN_"))

    def test_translated_and_wip_rows_name_an_existing_port_file(self):
        claimed = [row for row in self.rows if row["status"] in {"wip", "translated", "verified"}]
        self.assertTrue(claimed)
        for row in claimed:
            path, separator, detail = row["notes"].partition(": ")
            self.assertTrue(separator and detail, f"{row['address']}: notes must be '<path>: <detail>'")
            self.assertTrue((ROOT / path).is_file(), f"{row['address']}: {path} does not exist")
            self.assertNotEqual(row["subsystem"], "unknown", row["address"])

    def test_claimed_functions_are_cited_in_the_port_or_its_docs(self):
        sources = ""
        for folder in ("src", "include", "tests", "tools", "docs"):
            for path in (ROOT / folder).rglob("*"):
                if path.suffix in {".cpp", ".hpp", ".py", ".md"} and path.is_file():
                    sources += path.read_text(encoding="utf-8", errors="replace")
        for row in self.rows:
            if row["status"] in {"wip", "translated", "verified"}:
                self.assertIn(row["name"], sources, f"{row['name']} is not cited anywhere in the repo")


if __name__ == "__main__":
    unittest.main()
