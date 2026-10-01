import importlib.util
import unittest
from pathlib import Path
import xml.etree.ElementTree as ET
import io
import struct
from PIL import Image

spec = importlib.util.spec_from_file_location("images", Path(__file__).parents[1] / "generate_images.py")
images = importlib.util.module_from_spec(spec)
spec.loader.exec_module(images)


class ImageTests(unittest.TestCase):
    def test_tile_pack_edges_and_pixels(self):
        source = Image.new('RGBA', (513, 515), (10, 20, 30, 255))
        source.putpixel((512, 514), (200, 100, 50, 255))
        png = io.BytesIO()
        source.save(png, format='PNG')
        packed = images.tile_pack(png.getvalue())
        self.assertEqual(packed[:8], b'NVTILES1')
        self.assertEqual(struct.unpack_from('<4I', packed, 8), (513, 515, 512, 4))
        expected_sizes = [(512, 512), (1, 512), (512, 3), (1, 3)]
        previous_end = 24 + 4 * 8
        for index, size in enumerate(expected_sizes):
            offset, length = struct.unpack_from('<2I', packed, 24 + index * 8)
            self.assertEqual(offset, previous_end)
            with Image.open(io.BytesIO(packed[offset:offset + length])) as tile:
                self.assertEqual(tile.size, size)
                if index == 3:
                    self.assertEqual(tile.getpixel((0, 2)), (200, 100, 50, 255))
            previous_end = offset + length
        self.assertEqual(previous_end, len(packed))
        self.assertEqual(packed, images.tile_pack(png.getvalue()))

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
