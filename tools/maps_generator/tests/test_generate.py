import copy
import importlib.util
import json
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location("maps_generate", Path(__file__).parents[1] / "generate.py")
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)


def fixture():
    pos = {"x": -10.5, "y": 27, "z": 4.25}
    m = {"id": "map1", "normalizedName": "interchange", "name": "Map Name", "coordinateToCardinalRotation": 180,
         "raidDuration": 40, "players": "11-15", "lootContainers": [{"lootContainer": "item1", "position": pos}],
         "extracts": [{"id": "exit1", "name": "Exit", "faction": side, "position": pos} for side in ("pmc", "scav")],
         "transits": [{"id": "transit1", "map": "map2", "description": "Transit", "position": pos}],
         "spawns": [{"zoneName": "zone1", "sides": ["pmc"], "categories": ["player"], "position": pos}],
         "hazards": [{"hazardType": "minefield", "name": "Mine", "position": pos}],
         "lootLoose": [{"items": ["item2"], "position": pos}],
         "locks": [{"id": "lock1", "lockType": "door", "key": "key1", "position": pos}],
         "switches": [{"id": "switch1", "switchType": "Open", "name": "Power", "position": pos}],
         "stationaryWeapons": [{"stationaryWeapon": "weapon1", "position": pos}],
         "artillery": {"zones": [{"position": pos}]},
         "bosses": [{"mob": "bossKilla", "spawnLocations": [{"name": "OLI", "positions": [pos]}]}],
         "btrStops": [{"name": "Stop", **pos}]}
    return {"data": {"maps": {"map1": m}}}, {"en": {"Map Name": "Interchange", "Exit": "Railway"}, "zh": {"Map Name": "立交桥", "Exit": "铁路"}}


class GeneratorTests(unittest.TestCase):
    def test_real_coordinates_and_localization(self):
        rows, _ = GEN.normalize(*fixture())
        self.assertEqual(rows["maps"][0][2:4], ("立交桥", "Interchange"))
        self.assertEqual(len(rows["points"]), 13)
        self.assertTrue(all(p[-3:] == ("-10.5", "27", "4.25") for p in rows["points"]))
        labels = {p[2]: p[5:7] for p in rows["points"]}
        self.assertEqual(labels["loose"], ("散落物", "Loose loot"))
        self.assertEqual(labels["lock"], ("锁", "Lock"))

    def test_determinism_and_reordering(self):
        source, locales = fixture()
        expected = GEN.build(source, locales)
        source["data"]["maps"]["map1"]["extracts"].reverse()
        self.assertEqual(GEN.build(source, locales), expected)

    def test_faction_identity_does_not_collapse(self):
        rows, _ = GEN.normalize(*fixture())
        exits = [p for p in rows["points"] if p[2] == "extract"]
        self.assertEqual(len({p[0] for p in exits}), 2)
        self.assertEqual({p[4] for p in exits}, {"exit1"})

    def test_exact_duplicates_coalesce(self):
        source, locales = fixture()
        m = source["data"]["maps"]["map1"]
        m["lootContainers"].append(copy.deepcopy(m["lootContainers"][0]))
        self.assertEqual(len(GEN.normalize(source, locales)[0]["points"]), 13)

    def test_missing_position_is_reported_not_invented(self):
        source, locales = fixture()
        source["data"]["maps"]["map1"]["lootContainers"][0]["position"] = None
        rows, missing = GEN.normalize(source, locales)
        self.assertEqual(missing, {"container": 1})
        self.assertEqual(len(rows["points"]), 12)

    def test_task_zone_and_possible_locations(self):
        source, locales = fixture()
        tasks = {"data":{"tasks":{"task1":{"name":"Task", "objectives":[
            {"id":"objective1","description":"Visit","zones":[{"id":"zone1","map":"map1","position":{"x":1,"y":30,"z":2}}]},
            {"id":"objective2","description":"Find","possibleLocations":[{"map":"map1","positions":[{"x":3,"y":31,"z":4},{"x":5,"y":32,"z":6}]}]}
        ]}}}}
        task_locales={"en":{"Task":"Task","Visit":"Visit","Find":"Find"},"zh":{"Task":"任务","Visit":"访问","Find":"寻找"}}
        rows,_=GEN.normalize(source,locales,tasks,task_locales)
        task_rows=[row for row in rows["points"] if row[2]=="task"]
        self.assertEqual(len(task_rows),3)
        self.assertEqual({row[3] for row in task_rows},{"objective","item"})
        self.assertTrue(all(row[5].startswith("任务 · ") for row in task_rows))

    def test_invalid_coordinates_rejected(self):
        for value in (float("nan"), float("inf"), True, "12"):
            source, locales = fixture()
            source["data"]["maps"]["map1"]["lootContainers"][0]["position"]["x"] = value
            with self.assertRaises(ValueError):
                GEN.normalize(source, locales)

    def test_map_identity_and_errors(self):
        for field, value in (("id", "wrong"), ("normalizedName", "woods")):
            source, locales = fixture()
            source["data"]["maps"]["map1"][field] = value
            with self.assertRaises(ValueError):
                GEN.normalize(source, locales)
        source, locales = fixture()
        source["errors"] = ["partial failure"]
        with self.assertRaises(ValueError):
            GEN.normalize(source, locales)

    def test_tsv_columns_and_escaping(self):
        source, locales = fixture()
        locales["zh"]["Exit"] = "铁\t路\n出口\\"
        outputs, _ = GEN.build(source, locales)
        for name, content in outputs.items():
            width = len(content.splitlines()[0].split("\t"))
            self.assertTrue(all(len(row.split("\t")) == width for row in content.splitlines()))
        self.assertIn("铁\\t路\\n出口\\\\", outputs["map_points.tsv"])

    def test_duplicate_json_keys_rejected(self):
        with self.assertRaises(ValueError):
            json.loads('{"data":1,"data":2}', object_pairs_hook=GEN.unique_object)

    def test_boundary_explicit_and_no_images(self):
        _, meta = GEN.build(*fixture())
        self.assertFalse(meta["imagesIncluded"])
        self.assertEqual(meta["structureMode"], "regular")
        self.assertIn("not supplied", meta["floorAssignment"])


if __name__ == "__main__":
    unittest.main()
