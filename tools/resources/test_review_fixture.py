import hashlib
import tempfile
import unittest
from pathlib import Path
from create_review_fixture import create


class FixtureTests(unittest.TestCase):
    def test_deterministic_synthetic_only(self):
        source = Path(__file__).resolve().parents[2] / 'assets/data'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = create(source, root / 'a'), create(source, root / 'b')
            self.assertEqual(first, second)
            self.assertEqual(len(first), 2)
            for record in first:
                self.assertEqual(record['resourceId'], 'maps.' + record['stableMapId'])
                data = (root / 'a' / record['artifact']).read_bytes()
                self.assertEqual(data, (root / 'b' / record['artifact']).read_bytes())
                self.assertEqual(len(data), record['downloadSize'])
                self.assertEqual(hashlib.sha256(data).hexdigest(), record['sha256'])
                self.assertTrue(data.startswith(b'NVR1'))
                self.assertIn('SYNTHETIC TEST', record['titleEn'])


if __name__ == '__main__':
    unittest.main()
