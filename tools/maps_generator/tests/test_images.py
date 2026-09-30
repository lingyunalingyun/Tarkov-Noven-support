import importlib.util
import unittest
from pathlib import Path
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location("images", Path(__file__).parents[1] / "generate_images.py")
images = importlib.util.module_from_spec(spec)
spec.loader.exec_module(images)


class ImageTests(unittest.TestCase):
    def source(self):
        return b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 80"><defs/><g id="Ground_Level"/><g id="First_Floor"/><g id="Second_Floor"/></svg>'

    def test_floor_composition(self):
        variants, bounds = images.variants(self.source())
        self.assertEqual(bounds, [0, 0, 100, 80])
        for layer, svg in variants.items():
            groups = [g.attrib["id"] for g in ET.fromstring(svg) if g.tag.endswith("}g")]
            self.assertEqual(groups, ["Ground_Level"] if layer == "Ground_Level" else ["Ground_Level", layer])

    def test_determinism(self):
        self.assertEqual(images.variants(self.source()), images.variants(self.source()))

    def test_unknown_layer_rejected(self):
        with self.assertRaises(ValueError):
            images.variants(self.source().replace(b"First_Floor", b"Unknown"))

    def test_invalid_bounds_rejected(self):
        with self.assertRaises(ValueError):
            images.variants(self.source().replace(b"100 80", b"0 80"))
