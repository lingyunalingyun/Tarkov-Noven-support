import unittest
from prepare_additional_svg import convert, inspect_svg


class AdditionalSvgTests(unittest.TestCase):
    source = dict(Min=dict(x=0, y=0), Max=dict(x=10, y=10), rotation=90)
    target = dict(minX=0, maxX=10, minZ=0, maxZ=10, rotation=270)

    def test_orientation_uses_world_projection_not_filename(self):
        raw = b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10"><rect width="2" height="2" fill="red"/></svg>'
        image = convert(raw, self.source, self.target, None, (100, 100))
        self.assertGreater(image.getpixel((90, 90))[0], 240)
        self.assertEqual(image.getpixel((10, 10))[3], 0)

    def test_floor_is_selected_explicitly(self):
        raw = b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10"><g id="first"><rect width="2" height="2" fill="red"/></g><g id="second"><rect width="10" height="10" fill="blue"/></g></svg>'
        image = convert(raw, self.source, self.target, 'first', (100, 100))
        self.assertGreater(image.getpixel((90, 90))[0], 240)
        self.assertEqual(image.getpixel((40, 40))[3], 0)
        with self.assertRaises(ValueError):
            convert(raw, self.source, self.target, 'missing', (100, 100))

    def test_external_and_active_content_rejected(self):
        for child in ('<image href="test.png"/>', '<script/>', '<foreignObject/>',
                      '<use href="https://example.invalid/x"/>', '<g onclick="x"/>',
                      '<style>@import "test.css";</style>'):
            with self.subTest(child=child), self.assertRaises(ValueError):
                inspect_svg(('<svg xmlns="http://www.w3.org/2000/svg">'+child+'</svg>').encode())

    def test_transparent_or_flat_output_rejected(self):
        for child in ('', '<rect width="10" height="10" fill="black"/>'):
            with self.subTest(child=child), self.assertRaises(ValueError):
                convert(('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10">'+child+'</svg>').encode(),
                        self.source, self.target, None, (100, 100))


if __name__ == '__main__':
    unittest.main()
