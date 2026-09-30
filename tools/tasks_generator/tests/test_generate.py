import importlib.util
import pathlib
import unittest

MODULE_PATH = pathlib.Path(__file__).parents[1] / "generate.py"
SPEC = importlib.util.spec_from_file_location("tasks_generate", MODULE_PATH)
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)


def fixture(name="Task", task_id="a1", requirement=None):
    task = {
        "id": task_id, "name": name, "trader": "trader1", "map": "map1",
        "minPlayerLevel": 2, "experience": 100, "factionName": "Any",
        "taskRequirements": [] if requirement is None else [{"task": requirement, "status": ["complete"]}],
        "objectives": [{"id": task_id + "o", "description": name + " objective", "type": "findItem",
                        "count": 2, "optional": False, "items": ["item1"]}],
        "finishRewards": {"items": [{"item": "item2", "count": 3}],
                          "traderStanding": [{"trader": "trader1", "standing": 0.02}]},
    }
    tasks = {task_id: task}
    if requirement:
        tasks[requirement] = {**task, "id": requirement, "name": "Previous", "taskRequirements": [],
                              "objectives": [], "finishRewards": {}}
    return ({"data": {"tasks": tasks}},
            {"en": {name: name, name + " objective": "Find item", "Previous": "Previous",
                    "Trader": "Trader", "Map": "Map"},
             "zh": {name: "任务", name + " objective": "找到物品", "Previous": "前置任务",
                    "Trader": "商人", "Map": "地图"}},
            {"trader1": {"name": "Trader"}}, {"map1": {"name": "Map"}})


class GeneratorTests(unittest.TestCase):
    def test_merge_and_stable_identity(self):
        rows = GEN.normalize(*fixture())
        self.assertEqual(rows["tasks"][0][0], "a1")
        self.assertIn("任务", rows["tasks"][0])
        self.assertEqual(rows["objective_items"], [("a1_a1o", "item1")])

    def test_deterministic_output(self):
        rows = GEN.normalize(*fixture())
        self.assertEqual(GEN.build(rows, rows), GEN.build(rows, rows))
        self.assertEqual(GEN.build(rows, rows)[1]["structureModes"], ["regular"])

    def test_mode_difference_is_preserved(self):
        regular = GEN.normalize(*fixture())
        pve = GEN.normalize(*fixture("Other"))
        outputs, meta = GEN.build(regular, pve)
        self.assertGreater(meta["regularPveStructuralDifferenceCount"], 0)
        self.assertIn("\npve\t", outputs["task_tasks.tsv"])

    def test_requirement_resolves(self):
        rows = GEN.normalize(*fixture(requirement="a0"))
        self.assertEqual(rows["requirements"], [("a1", "a0")])

    def test_duplicate_objective_rejected(self):
        source, locales, traders, maps = fixture()
        source["data"]["tasks"]["a1"]["objectives"].append(
            dict(source["data"]["tasks"]["a1"]["objectives"][0]))
        with self.assertRaises(ValueError):
            GEN.normalize(source, locales, traders, maps)

    def test_invalid_identity_rejected(self):
        source, locales, traders, maps = fixture()
        source["data"]["tasks"]["a1"]["id"] = "bad id"
        with self.assertRaises(ValueError):
            GEN.normalize(source, locales, traders, maps)

    def test_escape_utf8_and_tabs(self):
        self.assertEqual(GEN.escape("中文\tline\n"), "中文\\tline\\n")

    def test_unlock_rewards_use_item_identity(self):
        source, locales, traders, maps = fixture()
        source["data"]["tasks"]["a1"]["finishRewards"].update({
            "offerUnlock": [{"id": "offer1", "item": "item3"}],
            "craftUnlock": [{"item": "item4"}],
        })
        rewards = GEN.normalize(source, locales, traders, maps)["rewards"]
        self.assertIn(("a1", "offerUnlock", "item3", "1"), rewards)
        self.assertIn(("a1", "craftUnlock", "item4", "1"), rewards)

    def test_task_chain_markers_are_preserved(self):
        source, locales, traders, maps = fixture()
        source["data"]["tasks"]["a1"]["kappaRequired"] = True
        source["data"]["tasks"]["a1"]["lightkeeperRequired"] = True
        row = GEN.normalize(source, locales, traders, maps)["tasks"][0]
        self.assertEqual(row[-2:], (1, 1))


if __name__ == "__main__":
    unittest.main()
