import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('icons', Path(__file__).parents[1] / 'generate_icons.py')
icons = importlib.util.module_from_spec(spec)
spec.loader.exec_module(icons)


class IconTests(unittest.TestCase):
    def test_specific_category_before_parent(self):
        data = {'itemCategories': {'card': {'normalizedName': 'electronic-key', 'parent': 'key'},
                                 'key': {'normalizedName': 'key', 'parent': 'card'}}}
        self.assertEqual(icons.item_icon({'categories': ['card']}, data), 'keycard')
        self.assertEqual(icons.item_icon({}, data), 'other')

    def test_multiple_possible_loot_types_preserve_point(self):
        points = [{'id': 'stable', 'kind': 'loose', 'sourceId': 'a,b', 'subtype': ''}]
        data = {'items': {'a': {'categories': ['drink']}, 'b': {'categories': ['food']}},
                'itemCategories': {'drink': {'normalizedName': 'drink'}, 'food': {'normalizedName': 'food'}}}
        result = icons.build(points, {}, data)
        self.assertEqual(result, 'id\ticons\nstable\tdrink,food\n')
        points[0]['sourceId'] = 'b,a'
        self.assertEqual(result, icons.build(points, {}, data))

    def test_container_source_identity(self):
        points = [{'id': 'box', 'kind': 'container', 'sourceId': 'source', 'subtype': ''}]
        self.assertEqual(icons.build(points, {'lootContainers': {'source': {'normalizedName': 'toolbox'}}}, {}),
                         'id\ticons\nbox\ttoolbox\n')
