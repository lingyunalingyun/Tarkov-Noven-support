import unittest
import generate_satellite as satellite


class SatelliteTests(unittest.TestCase):
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
