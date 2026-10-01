import unittest
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1]))
import generate_satellite as satellite


class SatelliteTests(unittest.TestCase):
    def test_overlay_removes_ground_and_other_floor(self):
        raw = b'<svg xmlns="http://www.w3.org/2000/svg"><defs/><g id="Ground_Level"/><g id="First_Floor"/><g id="Second_Floor"/></svg>'
        result = satellite.upper_overlay(raw, 'First_Floor')
        self.assertIn('First_Floor', result)
        self.assertNotIn('Ground_Level', result)
        self.assertNotIn('Second_Floor', result)
        self.assertIn('defs', result)
    def config(self):
        return {'bounds': [[598, -442], [-433, 426]], 'coordinateRotation': 180,
                'transform': [0.265, 150.6, 0.265, 134.6]}

    def test_world_corners_and_zoom(self):
        bounds = satellite.projected_bounds(self.config(), 0)
        self.assertAlmostEqual(bounds[0], -7.87)
        self.assertAlmostEqual(bounds[1], 17.47)
        self.assertAlmostEqual(bounds[2] - bounds[0], 1031 * .265)
        self.assertAlmostEqual(bounds[3] - bounds[1], 868 * .265)
        self.assertEqual(satellite.projected_bounds(self.config(), 4), tuple(v * 16 for v in bounds))

    def test_changed_contract_rejects(self):
        config = self.config()
        config['transform'][0] = .5
        with self.assertRaises(ValueError):
            satellite.projected_bounds(config)
