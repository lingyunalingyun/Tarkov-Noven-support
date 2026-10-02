import importlib.util
import unittest
import tempfile
from unittest.mock import patch
from pathlib import Path

spec = importlib.util.spec_from_file_location('icons', Path(__file__).parents[1] / 'generate_icons.py')
icons = importlib.util.module_from_spec(spec)
spec.loader.exec_module(icons)


class IconTests(unittest.TestCase):
    def test_mode_specific_offline_sources_never_fetch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'pve_maps.json').write_text('{"data":{"maps":{"pve":{}}}}', encoding='utf-8')
            with patch('urllib.request.urlopen', side_effect=AssertionError('network forbidden')):
                record, url, digest = icons.load_source('maps', 'pve', root, True)
                self.assertEqual(record, {'maps': {'pve': {}}})
                self.assertEqual(url, 'https://json.tarkov.dev/pve/maps')
                self.assertEqual(len(digest), 64)
                with self.assertRaises(FileNotFoundError):
                    icons.load_source('maps', 'regular', root, True)
                with self.assertRaises(ValueError):
                    icons.load_source('maps', 'regular', None, True)

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
