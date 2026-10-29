"""Consistency checks for the progress inputs (tools/gen, re/FUNCTION_MAP.csv, docs/LEVELS.md)."""

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


if __name__ == "__main__":
    unittest.main()
