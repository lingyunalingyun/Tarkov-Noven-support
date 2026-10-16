import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from prepare_svg_maps import prepare


class SvgPreparationTests(unittest.TestCase):
    def fixture(self, root, svg=b'<svg xmlns="http://www.w3.org/2000/svg"/>'):
        assets, sources, upstream = (root / n for n in ('assets', 'sources', 'upstream'))
        (assets / 'data/pve').mkdir(parents=True)
        (assets / 'maps/factory').mkdir(parents=True)
        sources.mkdir()
        upstream.mkdir()
        (sources / ('a' * 64)).write_bytes(svg)
        (upstream / 'LICENSE.md').write_text('Test fixture notice, not a public asset.')
        (upstream / 'README.md').write_text('Test-only upstream notice.')
        map_id = '55f2d3fd4bdc2d5f408b4567'
        url = 'https://assets.tarkov.dev/maps/svg/Factory.svg'
        (assets / 'data/map_maps.tsv').write_text(f'id\tnameZh\tnameEn\n{map_id}\tTest\tTest\n')
        floors = f'mapId\tabstractPath\n{map_id}\tmaps/factory/base.png\n'
        for name in ('map_floors.tsv', 'pve/map_floors.tsv'):
            (assets / 'data' / name).write_text(floors)
        (assets / 'maps/factory/base.manifest.json').write_text(json.dumps(dict(abstract=dict(kind='svg', author='Shebuka', source=url))))
        derived = {}
        for name, data in (('base.png', b'PNG'), ('base.tiles', b'NVTILES1test')):
            (assets / 'maps/factory' / name).write_bytes(data)
            derived['maps/factory/' + name] = dict(sha256=hashlib.sha256(data).hexdigest(), sourceUrls=[url])
        ledger = dict(sources=[dict(url=url, status=200, cachePath='a'*64, sha256=hashlib.sha256(svg).hexdigest())], maps=[dict(mapId=map_id, canonical='factory')], derived=derived)
        (assets / 'maps/all.manifest.json').write_text(json.dumps(ledger))
        return assets, sources, upstream

    def test_complete_deterministic_notices(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            args = self.fixture(root)
            a = prepare(*args, root / 'one', '1.0.0', '1'*40)
            b = prepare(*args, root / 'two', '1.0.0', '1'*40)
            self.assertEqual(a, b)
            self.assertTrue((root / 'one' / a[0]['stableMapId'] / 'input/maps/factory/PROVENANCE.json').is_file())

    def test_incomplete_map_withheld(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            assets, sources, upstream = self.fixture(root)
            (assets / 'maps/factory/base.manifest.json').write_text('{"abstract":{"kind":"tile-fallback"}}')
            self.assertEqual(prepare(assets, sources, upstream, root / 'out', '1.0.0', '1'*40), [])

    def test_embedded_imagery_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            args = self.fixture(root, b'<svg><image href="data:image/png;base64,AAAA"/></svg>')
            with self.assertRaises(ValueError):
                prepare(*args, root / 'out', '1.0.0', '1'*40)

    def test_source_and_derived_hashes(self):
        for file in ('source', 'derived'):
            with tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                args = self.fixture(root)
                target = args[1] / ('a'*64) if file == 'source' else args[0] / 'maps/factory/base.png'
                target.write_bytes(b'corrupt')
                with self.assertRaises(ValueError):
                    prepare(*args, root / 'out', '1.0.0', '1'*40)


if __name__ == '__main__':
    unittest.main()
