import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import animatio  # noqa: E402
import big  # noqa: E402
import model  # noqa: E402
import region  # noqa: E402
import strtable  # noqa: E402
import textures  # noqa: E402


@unittest.skipUnless(os.environ.get("BO2NX_ASSETS_DIR"), "BO2NX_ASSETS_DIR is unset")
class DataClaimsTest(unittest.TestCase):
    """The figures tools/bo2fmt/README.md gives as verified, measured in one pass."""

    @classmethod
    def setUpClass(cls):
        root = Path(os.environ["BO2NX_ASSETS_DIR"]) / "media/gogames/bo2"
        cls.containers = sorted(root.rglob("*.big"))
        cls.entries = 0
        cls.types = {}
        cls.formats = {}
        cls.models = 0
        cls.models_with_bones = 0
        cls.entities = 0
        cls.entity_errors = 0
        cls.entity_models = set()
        cls.animatio_graphs = 0
        cls.animatio_lists = 0
        cls.strtables = 0
        cls.irregular = 0
        for path in cls.containers:
            names, entries = big.load(path)
            for entry in entries:
                cls.entries += 1
                cls.types[entry.type] = cls.types.get(entry.type, 0) + 1
                if entry.type == "texture":
                    fmt = textures.dds_info(entry.data)["format"]
                    cls.formats[fmt] = cls.formats.get(fmt, 0) + 1
                elif entry.type == "model":
                    parsed = model.parse_model(entry.data)
                    cls.models += 1
                    if parsed["skeleton"]["bones"]:
                        cls.models_with_bones += 1
                elif entry.type == "region":
                    for section in region.parse_sections(region.region_payload(entry.data)):
                        if section["name"] != "entities":
                            continue
                        for entity in region.parse_entity_section(section["payload"]):
                            cls.entities += 1
                            if "error" in entity:
                                cls.entity_errors += 1
                            if entity["model"]:
                                cls.entity_models.add(entity["model"])
                elif entry.type == "animatio":
                    parsed = animatio.parse(entry.data)
                    if isinstance(parsed, list):
                        cls.animatio_lists += 1
                    else:
                        cls.animatio_graphs += 1
                elif entry.type == "strtable":
                    _, irregular = strtable.parse_strtable(names, entry)
                    cls.strtables += 1
                    cls.irregular += len(irregular)

    def test_containers_and_entries(self):
        self.assertEqual(len(self.containers), 179)
        self.assertEqual(self.entries, 246332)
        self.assertEqual(self.types, {
            "lipsync": 71182, "sound": 62073, "animreel": 32791, "texture": 22483,
            "sample": 21251, "strmfile": 16428, "effect": 7865, "model": 5168,
            "animatio": 4081, "tunedata": 1720, "bpackage": 480, "cellgrup": 236,
            "region": 165, "planinfo": 159, "litedata": 142, "strtable": 92,
            "musicbnk": 11, "texfont": 4, "liteinfo": 1,
        })

    def test_texture_formats(self):
        self.assertEqual(self.formats, {
            "DXT1": 15374, "DXT5": 4337, "DXT3": 2742, "rgb32": 19, "DXT2": 11,
        })

    def test_models(self):
        self.assertEqual(self.models, 5168)
        self.assertEqual(self.models_with_bones, 1586)

    def test_region_entities(self):
        self.assertEqual(self.entities, 15632)
        self.assertEqual(self.entity_errors, 0)
        self.assertEqual(len(self.entity_models), 533)

    def test_animatio(self):
        self.assertEqual(self.animatio_graphs, 2156)
        self.assertEqual(self.animatio_lists, 1925)

    def test_strtables(self):
        self.assertEqual(self.strtables, 92)
        self.assertEqual(self.irregular, 1)


if __name__ == "__main__":
    unittest.main()
