"""Relative links of the documentation: the file and the heading they name must exist."""

import re
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LINK = re.compile(r"\]\(([^)]+)\)")
HEADING = re.compile(r"^(#{1,6})\s+(.*)$")


def anchor(text: str) -> str:
    """The GitHub anchor of a heading, with the rule of the issue."""
    for character in ",*_":
        text = text.replace(character, "")
    text = text.lower()
    text = "".join(character for character in text
                   if character.isalnum() or character in "-_ ")
    return text.replace(" ", "-")


def headings(path: Path) -> set[str]:
    anchors = set()
    in_fence = False
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.strip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        match = HEADING.match(line)
        if match:
            anchors.add(anchor(match.group(2)))
    return anchors


def broken_links(files: list[Path]) -> list[tuple[Path, str, str]]:
    cache = {}
    broken = []
    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        for destination in LINK.findall(text):
            target = destination.strip()
            if not target or target.startswith(("http", "mailto:")):
                continue
            if target.startswith("#"):
                file, fragment = path, target[1:]
            else:
                name, _, fragment = target.partition("#")
                file = (path.parent / name).resolve()
            if not file.exists():
                broken.append((path, destination, "file"))
                continue
            if fragment:
                if not file.is_file():
                    broken.append((path, destination, "anchor"))
                    continue
                if file not in cache:
                    cache[file] = headings(file)
                if fragment not in cache[file]:
                    broken.append((path, destination, "anchor"))
    return broken


class DocsLinksTest(unittest.TestCase):
    def test_repository_links(self):
        files = sorted((ROOT / "docs").rglob("*.md"))
        files += [ROOT / "README.md", ROOT / "README.en.md", ROOT / "STATE.md"]
        broken = broken_links(files)
        self.assertEqual(
            broken, [],
            "\n".join(f"{path.relative_to(ROOT)}: {destination} ({kind})"
                      for path, destination, kind in broken),
        )

    def test_synthetic_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "other.md").write_text("# Other section\n", encoding="utf-8")
            (root / "main.md").write_text(
                "# Title\n\n"
                "[good](other.md#other-section)\n"
                "[self](#title)\n"
                "[missing](nope.md)\n"
                "[bad](other.md#nope)\n",
                encoding="utf-8",
            )
            broken = broken_links([root / "main.md"])
            self.assertEqual(
                [(path.name, destination, kind) for path, destination, kind in broken],
                [("main.md", "nope.md", "file"), ("main.md", "other.md#nope", "anchor")],
            )


if __name__ == "__main__":
    unittest.main()
