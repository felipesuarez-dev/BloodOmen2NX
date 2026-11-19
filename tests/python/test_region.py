import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import big  # noqa: E402
import region  # noqa: E402


def section(name, payload):
    return name.encode("latin1").ljust(8, b"\0") + struct.pack("<I", 12 + len(payload)) + payload


def entity_record(name="dagger001", model="dagger"):
    data = struct.pack("<I", 0)
    data += name.encode() + b"\0"
    data += model.encode() + b"\0"
    data = data.ljust((len(data) + 3) & ~3, b"\0")
    data += struct.pack("<fff", 1.0, 2.0, 3.0)
    key = b"reserved.rot.z\0"
    key = key.ljust((len(key) + 3) & ~3, b"\0")
    data += key + struct.pack("<II", 3, 0)
    return b"entity\0\0" + struct.pack("<I", 12 + len(data)) + data


def region_payload(entities):
    payload = struct.pack("<II", 1, 3)
    payload += section("packages", b"")
    payload += section("entities", struct.pack("<II", 0, 9) + entity_record(*entities))
    return payload


class RegionTest(unittest.TestCase):
    def test_parse_entity(self):
        payload = region_payload(())
        sections = region.parse_sections(payload)
        self.assertEqual([item["name"] for item in sections], ["packages", "entities"])
        entities = region.parse_entity_section(sections[1]["payload"])
        self.assertEqual(len(entities), 1)
        self.assertEqual(entities[0]["name"], "dagger001")
        self.assertEqual(entities[0]["model"], "dagger")
        self.assertEqual(entities[0]["position"], [1.0, 2.0, 3.0])
        self.assertEqual(entities[0]["properties"][0]["key"], "reserved.rot.z")

    def test_collect_from_big(self):
        payload = region_payload(())
        data = big.build(["r"], [big.Entry("region", "r", 0, 0, 0, payload)])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "level.big"
            path.write_bytes(data)
            entities = region.collect_entities(path)
            self.assertEqual(len(entities), 1)
            self.assertEqual(entities[0]["model"], "dagger")

    def test_reserved_script(self):
        # FUN_007828c0: the value of the first reserved.script, "" for none.
        entity = {"properties": [{"key": "reserved.marker", "type": 2, "value": 1}]}
        self.assertEqual(region.reserved_script(entity), "")
        self.assertEqual(region.reserved_script({"properties": []}), "")
        entity = {
            "properties": [
                {"key": "reserved.script", "type": 8, "value": "hint_proximity"},
                {"key": "reserved.script", "type": 8, "value": "other"},
            ]
        }
        self.assertEqual(region.reserved_script(entity), "hint_proximity")

    def test_region_package_name(self):
        # FUN_00783900: the five controls of BO2NX-185.
        self.assertEqual(region.region_package_name("slums/sl01"), "slums_sl01")
        self.assertEqual(
            region.region_package_name("bossrooms/charmvampirearena/cvarena"),
            "bossrooms_charmvampirearena_cvarena",
        )
        self.assertEqual(region.region_package_name("eternal prison/ep02"), "eternal_prison_ep02")
        self.assertEqual(region.region_package_name("ui/optionscreen_01"), "ui_optionscreen_01")
        self.assertEqual(region.region_package_name("  9a/B c \t"), "a_b_c")


@unittest.skipUnless(os.environ.get("BO2NX_ASSETS_DIR"), "BO2NX_ASSETS_DIR is unset")
class GameRegionTest(unittest.TestCase):
    def test_scripted_entities_of_slums(self):
        # 78, 66 and 94 entities with a script, none of them with a slash.
        levels = Path(os.environ["BO2NX_ASSETS_DIR"]) / "media/gogames/bo2/resources/levels/slums"
        expected = {"sl01.big": 78, "sl02.big": 66, "sl_music.big": 94}
        for name, count in expected.items():
            entities = region.collect_entities(levels / name)
            scripts = [region.reserved_script(entity) for entity in entities]
            scripts = [script for script in scripts if script]
            self.assertEqual(len(scripts), count, name)
            for script in scripts:
                self.assertNotIn("/", script)


if __name__ == "__main__":
    unittest.main()
