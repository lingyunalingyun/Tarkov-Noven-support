"""离线管线、真实投影和来源完整性合同。 Offline pipeline/projection/provenance contracts."""
import csv
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

sys.path.insert(0, str(Path(__file__).parents[1]))
spec = importlib.util.spec_from_file_location('all_images', Path(__file__).parents[1] / 'generate_all_images.py')
images = importlib.util.module_from_spec(spec)
spec.loader.exec_module(images)

SVG = b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><defs/><g id="Ground_Level"><rect width="100" height="100" fill="blue"/></g><g id="Upper"><rect x="50" width="50" height="100" fill="red"/></g></svg>'


def config():
    return {'key': 'actual', 'projection': 'interactive', 'bounds': [[0, 0], [100, 100]],
            'coordinateRotation': 180, 'svgPath': 'https://assets.tarkov.dev/maps/svg/Test.svg',
            'svgLayer': 'Ground_Level', 'author': 'Recorded author', 'altMaps': ['actual-alt'],
            'layers': [{'name': '2nd Floor', 'svgLayer': 'Upper', 'extents': [
                {'height': [2, 5], 'bounds': [[[10, 20], [30, 40], 'a'], [[50, 60], [70, 80], 'b']]},
                {'height': [5, 10]}]}]}


class AllImagesTests(unittest.TestCase):
    def test_detail_bounds_preserve_accepted_interchange_resolution(self):
        self.assertEqual(images.image_size('interchange', 1127.6852, 947.02582, 4096), (8192, 6880))
        self.assertEqual(images.image_size('tall', 100, 200, 8192), (4096, 8192))
        self.assertEqual(images.image_size('fixture', 100, 100, 64), (64, 64))
    def test_rotation_screen_y_and_four_corner_bbox(self):
        bounds = [[0, 0], [100, 100]]
        self.assertAlmostEqual(images.project(0, 0, bounds, 180, 100, 100)[0], 100)
        self.assertAlmostEqual(images.project(0, 0, bounds, 180, 100, 100)[1], 0)
        self.assertAlmostEqual(images.project(0, 0, bounds, 90, 100, 100)[0], 100)
        self.assertAlmostEqual(images.project(0, 0, bounds, 90, 100, 100)[1], 100)
        self.assertAlmostEqual(images.project(100, 0, bounds, 45, 100, 100)[0], 100)
        self.assertAlmostEqual(images.project(100, 0, bounds, 45, 100, 100)[1], 50)
        self.assertEqual(images.rectangle([[598, -442], [-433, 426]]), (-433, 598, -442, 426))

    def test_extents_preserve_all_rectangles_priority_and_default_bounds(self):
        cfg = config()
        cfg['layers'].append({'name': 'Underground', 'svgLayer': 'Basement', 'extents': [{'height': [-5, 0]}]})
        rows = images.extent_rows('id', cfg, images.floors(cfg))
        self.assertEqual(rows, [('id', 'Upper', 2, 5, 10, 30, 20, 40),
                                ('id', 'Upper', 2, 5, 50, 70, 60, 80),
                                ('id', 'Upper', 5, 10, 0, 100, 0, 100),
                                ('id', 'Basement', -5, 0, 0, 100, 0, 100)])

    def test_svg_bounds_crop_resample_not_stretch_entire_source(self):
        cfg = config()
        cfg['bounds'] = [[0, 0], [100, 100]]
        cfg['svgBounds'] = [[0, -100], [100, 100]]
        _, view, crop = images.svg_variant(SVG, cfg, images.floors(cfg)[0][1])
        self.assertEqual(view, [0, 0, 100, 100])
        self.assertAlmostEqual(crop[1], 50)
        self.assertAlmostEqual(crop[3], 50)
        svg = b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><g id="Ground_Level"><rect width="100" height="50" fill="red"/><rect y="50" width="100" height="50" fill="blue"/></g></svg>'
        text, _, _ = images.svg_variant(svg, cfg, images.floors(cfg)[0][1])
        rendered = images.raster_svg(text, (64, 32))
        self.assertEqual(rendered.size, (64, 32))
        self.assertEqual(rendered.getpixel((32, 16)), (0, 0, 255, 255))

    def test_floor_composition_overlay_has_no_ground(self):
        cfg = config()
        layer = cfg['layers'][0]
        full, _, _ = images.svg_variant(SVG, cfg, layer)
        overlay, _, _ = images.svg_variant(SVG, cfg, layer, overlay=True)
        self.assertIn('Ground_Level', full)
        self.assertNotIn('Ground_Level', overlay)
        self.assertIn('Upper', overlay)
        with self.assertRaisesRegex(ValueError, 'layer absent'):
            images.svg_variant(SVG, cfg, {'svgLayer': 'Imagined'})

    def test_offline_hash_validation_and_never_network(self):
        with tempfile.TemporaryDirectory() as directory:
            online = images.Sources(directory)
            url = 'https://assets.tarkov.dev/maps/real/0/0/0.png'
            online.get(url, seed=b'real source bytes')
            offline = images.Sources(directory, offline=True)
            with patch('requests.Session', side_effect=AssertionError('network forbidden')):
                self.assertEqual(offline.get(url), b'real source bytes')
                with self.assertRaisesRegex(ValueError, 'offline source missing'):
                    offline.get(url + '?missing', optional=True)
                (Path(directory) / images.digest(url.encode())).write_bytes(b'corrupt')
                with self.assertRaisesRegex(ValueError, 'source hash mismatch'):
                    offline.get(url)

    def test_recorded_404_is_not_a_fake_download(self):
        with tempfile.TemporaryDirectory() as directory:
            url = 'https://assets.tarkov.dev/missing.png'
            record = {'url': url, 'status': 404, 'sha256': None, 'cachePath': None}
            (Path(directory) / (images.digest(url.encode()) + '.json')).write_bytes(images.json_bytes(record))
            sources = images.Sources(directory, offline=True)
            self.assertIsNone(sources.get(url, optional=True))
            with self.assertRaisesRegex(ValueError, 'required source unavailable'):
                sources.get(url)

    def test_alt_maps_share_paths_and_missing_metadata_is_discoverable(self):
        cfg = config()
        refs = images.references([{'normalizedName': 'actual', 'maps': [cfg]}])
        self.assertEqual(refs['actual'], refs['actual-alt'])
        self.assertNotIn('actual-tutorial', refs)

    def test_tile_geometry_matches_native_positive_rotation(self):
        cfg = config()
        cfg.update(bounds=[[77, -64.5], [-65.5, 67.4]], coordinateRotation=90,
                   transform=[1.629, 119.9, 1.629, 139.3], minZoom=1, maxZoom=6)
        zoom, bounds = images.tile_geometry(cfg, 4096)
        self.assertEqual(zoom, 5)
        self.assertAlmostEqual(bounds[0], (-67.4*1.629+119.9)*32)
        self.assertAlmostEqual(bounds[1], (-77*1.629+139.3)*32)

    def test_real_png_tiles_subpixel_projection_and_pack(self):
        with tempfile.TemporaryDirectory() as directory:
            cfg = {'bounds': [[0, 0], [16, 16]], 'coordinateRotation': 0,
                   'transform': [1, 0, 1, 16], 'minZoom': 0, 'maxZoom': 0, 'tileSize': 16}
            sources = images.Sources(directory, offline=True)
            template = 'https://assets.tarkov.dev/maps/real/{z}/{x}/{y}.png'
            tile = Image.new('RGBA', (32, 32), (20, 30, 40, 255))
            sources.get(template.format(z=0, x=0, y=0), seed=images.png(tile))
            with patch('requests.Session', side_effect=AssertionError('network forbidden')):
                result, metadata = images.satellite(sources, cfg, template, (16, 16), workers=1)
            self.assertEqual(result.getpixel((8, 8)), (20, 30, 40, 255))
            self.assertEqual(metadata['missingTiles'], [])
            self.assertEqual(metadata['compositionLayers'][0]['inputs'][0]['encodedSize'], [32, 32])
            packed = images.tile_pack(images.png(result))
            self.assertEqual(packed[:8], b'NVTILES1')
            self.assertEqual(struct.unpack_from('<4I', packed, 8), (16, 16, 512, 1))

    def test_complete_offline_build_determinism_and_exact_headers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cfg = config()
            cfg.update(coordinateRotation=0, tilePath='https://assets.tarkov.dev/maps/test/{z}/{x}/{y}.png',
                       transform=[1, 0, 1, 100], minZoom=0, maxZoom=0, tileSize=100)
            sources = images.Sources(root / 'cache', offline=True)
            sources.get(cfg['svgPath'], seed=SVG)
            tile_url = cfg['tilePath'].format(z=0, x=0, y=0)
            tile_bytes = images.png(Image.new('RGBA', (100, 100), (20, 30, 40, 255)))
            sources.get(tile_url, seed=tile_bytes)
            sources.get('https://assets.tarkov.dev/images/handbook/icon.png', seed=tile_bytes)
            api = {'data': {'maps': {'1': {'id': '1', 'normalizedName': 'actual'},
                                     '2': {'id': '2', 'normalizedName': 'actual-alt'},
                                     '3': {'id': '3', 'normalizedName': 'actual-tutorial'}}}}
            layout = [{'normalizedName': 'actual', 'maps': [cfg]}]
            sources.get(images.LAYOUT_URL, seed=images.json_bytes(layout))
            sources.get(images.API_URL, seed=images.json_bytes(api))
            sources.get(images.SVG_RIGHTS_URL, seed=b'Test-only rights fixture')
            with patch('requests.Session', side_effect=AssertionError('network forbidden')):
                manifest = images.build(root / 'one', sources, api, layout, detail_width=64, preview=32)
                images.build(root / 'two', sources, api, layout, detail_width=64, preview=32)
            files = lambda folder: {p.relative_to(folder).as_posix(): images.digest(p.read_bytes()) for p in folder.rglob('*') if p.is_file()}
            self.assertEqual(files(root / 'one'), files(root / 'two'))
            before = files(root / 'one')
            images.build(root / 'one', sources, api, layout, detail_width=64, preview=32, reuse_existing=True)
            self.assertEqual(files(root / 'one'), before)
            for name, header in images.HEADERS.items():
                self.assertEqual((root / 'one/data' / name).read_text(encoding='utf-8').splitlines()[0], header)
            with (root / 'one/data/map_floors.tsv').open(encoding='utf-8') as file:
                rows = list(csv.DictReader(file, delimiter='\t'))
            self.assertEqual(rows[0]['abstractPath'], rows[2]['abstractPath'])
            self.assertEqual(rows[0]['order'], '1')
            self.assertEqual(rows[1]['order'], '0')
            self.assertEqual(manifest['maps'][2]['status'], 'missing_metadata')
            with (root / 'one/data/map_update_assets.tsv').open(encoding='utf-8') as file:
                updates = list(csv.DictReader(file, delimiter='\t'))
            self.assertEqual(len(updates), 1)
            self.assertNotIn(cfg['svgPath'], [row['url'] for row in updates])
            self.assertEqual(updates[0]['url'], tile_url)
            for row in updates:
                self.assertEqual(row['sha256'], '')
                self.assertEqual(images.digest((root / 'one' / row['relativePath']).read_bytes()), sources.used[row['url']]['sha256'])
            with (root / 'one/data/map_compositions.tsv').open(encoding='utf-8') as file:
                recipes = list(csv.DictReader(file, delimiter='\t'))
            self.assertEqual(len(recipes), 3)
            self.assertEqual([row['blend'] for row in recipes], ['replace', 'replace', 'over'])
            self.assertEqual(recipes[0]['sourcePath'], updates[0]['relativePath'])
            for row in recipes:
                self.assertEqual((row['width'], row['height'], row['tileSize']), ('64', '64', '100'))
                self.assertEqual(tuple(row[k] for k in ('left', 'top', 'right', 'bottom')), ('0', '0', '100', '100'))
                self.assertEqual((row['tileX'], row['tileY']), ('0', '0'))
                with Image.open(root / 'one' / row['sourcePath']) as image:
                    self.assertEqual(image.size, (100, 100))
            for relative, record in manifest['derived'].items():
                self.assertEqual(images.digest((root / 'one' / relative).read_bytes()), record['sha256'])
                self.assertTrue(cfg['svgPath'] in record['sourceUrls'] or tile_url in record['sourceUrls'])
                self.assertTrue(set(record['sourceUrls']).issubset(sources.used))
                if relative.endswith('.png'):
                    with Image.open(root / 'one' / relative) as image:
                        self.assertLessEqual(max(image.size), 512 if '/composition_sources/' in relative else 32)

    def test_pinned_snapshot_does_not_refresh_api(self):
        with tempfile.TemporaryDirectory() as directory:
            sources = images.Sources(directory)
            sources.get(images.API_URL, seed=b'older downloaded API')
            with patch('requests.Session', side_effect=AssertionError('network forbidden')):
                sources.pin(images.API_URL, b'point catalog pinned API')
                self.assertEqual(images.Sources(directory, True).get(images.API_URL), b'point catalog pinned API')

    def test_sparse_explicit_floor_base_retains_real_404_gaps(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sources = images.Sources(root / 'cache', offline=True)
            template = 'https://assets.tarkov.dev/maps/sparse/{z}/{x}/{y}.png'
            cfg = {'key': 'actual', 'projection': 'interactive', 'bounds': [[0, 0], [48, 48]], 'coordinateRotation': 0,
                   'transform': [1, 0, 1, 48], 'minZoom': 0, 'maxZoom': 0, 'tileSize': 16,
                   'tilePath': template, 'layers': [{'name': 'Infirmary', 'tilePath': template}]}
            tile = images.png(Image.new('RGBA', (16, 16), (20, 30, 40, 255)))
            for y in range(3):
                for x in range(3):
                    url = template.format(z=0, x=x, y=y)
                    if (x, y) == (1, 1):
                        record = {'url': url, 'status': 404, 'sha256': None, 'cachePath': None}
                        (sources.directory / (images.digest(url.encode()) + '.json')).write_bytes(images.json_bytes(record))
                    else:
                        sources.get(url, seed=tile)
            with self.assertRaises(images.SourceUnavailable):
                images.satellite(sources, cfg, template, (64, 64), workers=1)
            api = {'data': {'maps': {'1': {'id': '1', 'normalizedName': 'actual'}}}}
            layout = [{'normalizedName': 'actual', 'maps': [cfg]}]
            sources.get(images.LAYOUT_URL, seed=images.json_bytes(layout))
            sources.get(images.API_URL, seed=images.json_bytes(api))
            with patch('requests.Session', side_effect=AssertionError('network forbidden')):
                images.build(root / 'output', sources, api, layout, detail_width=64, preview=32)
            metadata = json.loads((root / 'output/maps/actual/Base.manifest.json').read_bytes())
            self.assertTrue(metadata['satellite']['sparseFloorBase'])
            self.assertEqual(len(metadata['satellite']['missingTiles']), 1)
            with Image.open(root / 'output/maps/actual/Base.satellite.png') as image:
                self.assertEqual(image.getpixel((16, 16))[3], 0)

    def test_interchange_accepted_ids_and_labels(self):
        cfg = {'key': 'interchange', 'svgLayer': 'Ground_Level', 'layers': [
            {'name': '2nd Floor', 'svgLayer': 'First_Floor'}, {'name': '3rd Floor', 'svgLayer': 'Second_Floor'}]}
        self.assertEqual([(floor, layer['name']) for floor, layer in images.floors(cfg)],
                         [('Ground_Level', 'B1'), ('First_Floor', '1F'), ('Second_Floor', '2F')])


if __name__ == '__main__':
    unittest.main()
