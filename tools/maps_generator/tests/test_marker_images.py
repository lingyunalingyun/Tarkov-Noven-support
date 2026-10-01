import importlib.util
import io
from pathlib import Path
import unittest
from PIL import Image

spec = importlib.util.spec_from_file_location('marker_images', Path(__file__).parents[1] / 'generate_marker_images.py')
markers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(markers)


class MarkerImageTests(unittest.TestCase):
    def test_mapping_and_official_aliases(self):
        records = {slug: {'normalizedName': slug, 'imageLink': 'https://assets.tarkov.dev/' + slug + '.webp'}
                   for slug in markers.HANDBOOK.values()}
        urls = markers.image_urls('pinned', {'handbookCategories': records})
        self.assertEqual(len(urls), 63)
        self.assertEqual(urls['detail_medical_crate'], urls['detail_technical_crate'])
        self.assertEqual(urls['detail_pmc_body'], urls['detail_dead_scav'])
        self.assertIn('/pinned/', urls['category_pmc_extract'])
        self.assertTrue(urls['detail_electronics'].startswith('https://assets.tarkov.dev/'))

    def test_png_determinism_preserves_pixels(self):
        source = Image.new('RGBA', (24, 48), (20, 80, 200, 100))
        out = io.BytesIO()
        source.save(out, 'PNG')
        a = markers.png(out.getvalue())
        self.assertEqual(a, markers.png(out.getvalue()))
        decoded = Image.open(io.BytesIO(a))
        self.assertEqual(decoded.size, (48, 48))
        self.assertEqual(decoded.crop((12, 0, 36, 48)).tobytes(), source.tobytes())
        self.assertEqual(decoded.getpixel((0, 0)), (0, 0, 0, 0))

    def test_missing_category_rejects(self):
        with self.assertRaises(KeyError):
            markers.image_urls('pinned', {'handbookCategories': {}})
