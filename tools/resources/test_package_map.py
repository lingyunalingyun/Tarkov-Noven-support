import json
import tempfile
import unittest
from pathlib import Path
from package_map import package, safe_name


class PackagingTests(unittest.TestCase):
    map_id = '55f2d3fd4bdc2d5f408b4567'
    def test_names(self):
        for name in ('maps/a.png', 'maps/license/SOURCE.md'):
            self.assertTrue(safe_name(name))
        for name in ('C:/maps/a.png', '/maps/a.png', 'maps/../a.png', 'maps/a.exe', 'maps/nul.png', 'maps/a//b.png', 'maps/a./b.png'):
            self.assertFalse(safe_name(name))

    def test_approval_and_determinism(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / 'source'
            (source / 'maps').mkdir(parents=True)
            (source / 'maps/a.png').write_bytes(b'original synthetic bytes')
            (source / 'maps/SOURCE.md').write_text('Original synthetic fixture, not third-party imagery.')
            inventory = root / 'inventory.json'
            records = {n: dict(source='original', license='test-only') for n in ('maps/a.png', 'maps/SOURCE.md')}
            inventory.write_text(json.dumps(dict(classification='original-synthetic', approvalReference='original author test', files=records)))
            args = (source, inventory)
            first = package(*args, root / 'one', self.map_id, '0.0.1', 'test', 'test', True)
            second = package(*args, root / 'two', self.map_id, '0.0.1', 'test', 'test', True)
            self.assertEqual(first, second)
            self.assertEqual((root / 'one' / first['artifact']).read_bytes(), (root / 'two' / second['artifact']).read_bytes())
            with self.assertRaises(ValueError):
                package(*args, root / 'denied', self.map_id, '0.0.1', 'test', 'test')
            with self.assertRaises(ValueError):
                package(*args, root / 'unknown', 'factory', '0.0.1', 'test', 'test', True)
            (source / 'maps/evil.exe').write_bytes(b'not allowed')
            with self.assertRaises(ValueError):
                package(*args, root / 'exe', self.map_id, '0.0.1', 'test', 'test', True)


if __name__ == '__main__':
    unittest.main()
