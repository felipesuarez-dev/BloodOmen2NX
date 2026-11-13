import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import big  # noqa: E402
import model  # noqa: E402


def chunk(name, payload):
    header = name.encode("latin1").ljust(8, b"\0")
    return header + struct.pack("<II", 16 + len(payload), 0) + payload


def rmd(subchunks):
    body = b"".join(chunk(name, payload) for name, payload in subchunks)
    record = b"default\0" + struct.pack("<I", 16 + len(body)) + struct.pack("<I", 0) + body
    return struct.pack("<I", 2) + record


def model_entry(chunks):
    body = b"".join(chunk(name, payload) for name, payload in chunks)
    return struct.pack("<I", 105) + b"default\0" + struct.pack("<I", 16 + len(body)) + struct.pack("<I", 0) + body


def bone(**fields):
    values = {
        "name": 0, "state_mask": 0, "state_value": 0, "bone_index": 0,
        "current_matrix": 0, "parent_matrix": 0, "first_collision_prim": 0,
        "collision_prim_count": 0, "first_vertex": 0, "vertex_count": 0,
        "channel_mask": 0, "rotation_order": 0,
        "pivot": (0.0, 0.0, 0.0), "rest_orientation": (0.0, 0.0, 0.0),
        "joint_orientation": (0.0, 0.0, 0.0), "rotation": (0.0, 0.0, 0.0),
        "scale": (1.0, 1.0, 1.0),
    }
    values.update(fields)
    record = struct.pack(
        "<10I", values["name"], values["state_mask"], values["state_value"],
        values["bone_index"], values["current_matrix"], values["parent_matrix"],
        values["first_collision_prim"], values["collision_prim_count"],
        values["first_vertex"], values["vertex_count"])
    record += struct.pack("<HH", values["channel_mask"], values["rotation_order"])
    for key in ("pivot", "rest_orientation", "joint_orientation", "rotation", "scale"):
        record += struct.pack("<3f", *values[key])
    return record


def skel(records):
    return struct.pack("<I", len(records)) + b"".join(records)


class ModelTest(unittest.TestCase):
    def test_skeleton_and_weights(self):
        data = model_entry(
            [
                ("rmd", rmd([
                    ("vtxweigh", struct.pack("<I2f", 2, 0.25, 0.75)),
                    ("jointidx", struct.pack("<I2H", 2, 1, 3)),
                ])),
                ("modlskel", skel([bone(name=0xFFFFFFFF, state_mask=0xFFFFFFFF),
                                  bone(bone_index=1, parent_matrix=2)])),
            ]
        )
        parsed = model.parse_model(data)
        self.assertEqual(parsed["name"], "default")
        self.assertEqual(len(parsed["skeleton"]["bones"]), 2)
        self.assertEqual(parsed["skeleton"]["bones"][1]["bone_index"], 1)
        self.assertEqual(parsed["skeleton"]["bones"][1]["parent_matrix"], 2)
        self.assertEqual(parsed["skeleton"]["bones"][1]["scale"], [1.0, 1.0, 1.0])
        self.assertEqual(parsed["skeleton"]["weights"], [0.25, 0.75])
        self.assertEqual(parsed["skeleton"]["joints"], [1, 3])

    def test_extract_copies_models_and_animations(self):
        data = big.build(
            ["m", "a", "r"],
            [
                big.Entry("model", "m", 0, 0, 0, model_entry([("modlskel", skel([]))])),
                big.Entry("animatio", "a", 0, 0, 0, b"anim"),
                big.Entry("animreel", "r", 0, 0, 0, b"reel"),
            ],
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "level.big"
            source.write_bytes(data)
            out = root / "out"
            self.assertEqual(model.extract(source, out), 3)
            self.assertEqual(len(list(out.iterdir())), 3)

    def test_collect_models(self):
        data = big.build(
            ["m"], [big.Entry("model", "m", 0, 0, 0, model_entry([("modlskel", skel([]))]))]
        )
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "level.big"
            source.write_bytes(data)
            models = model.collect_models(source)
            self.assertEqual(len(models), 1)
            self.assertEqual(models[0]["entry"], "m")


if __name__ == "__main__":
    unittest.main()
