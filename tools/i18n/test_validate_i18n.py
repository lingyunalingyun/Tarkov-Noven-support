import json
from pathlib import Path
import tempfile
import unittest
from validate_i18n import validate


class ValidationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.write("zh-CN", {"example": "中文 {count}"})

    def write(self, locale, texts, filename=None):
        data = dict(texts)
        data["_meta"] = {"nameEnglish": "Test", "name": "测试", "locale": locale}
        (self.directory / (filename or f"{locale}.json")).write_text(
            json.dumps(data, ensure_ascii=False), encoding="utf-8")

    def test_order_and_mock_discovery(self):
        self.write("xx-TEST", {"example": "Test {count}"})
        self.assertEqual(validate(self.directory)[0], [])
        self.assertEqual(len(validate(self.directory)[1]), 2)

    def test_placeholder_mismatch(self):
        self.write("en-US", {"example": "Items"})
        self.assertIn("Placeholder mismatch", "\n".join(validate(self.directory)[0]))

    def test_missing_and_unknown(self):
        self.write("en-US", {"typo": "Text"})
        errors = "\n".join(validate(self.directory)[0])
        self.assertIn("Missing keys", errors)
        self.assertIn("Unknown keys", errors)

    def test_duplicate_locale(self):
        self.write("zh-CN", {"example": "文本 {count}"}, "copy.json")
        self.assertIn("duplicate locale", "\n".join(validate(self.directory)[0]))

    def test_malformed_and_utf8(self):
        (self.directory / "en-US.json").write_bytes(b'\xff')
        self.assertTrue(validate(self.directory)[0])
        (self.directory / "en-US.json").write_text('{broken', encoding="utf-8")
        self.assertTrue(validate(self.directory)[0])

    def test_duplicate_keys_and_invalid_metadata(self):
        (self.directory / "en-US.json").write_text('{"a":"1","a":"2"}', encoding="utf-8")
        self.assertIn("Duplicate JSON key", "\n".join(validate(self.directory)[0]))
        (self.directory / "en-US.json").write_text('{}', encoding="utf-8")
        self.assertTrue(validate(self.directory)[0])

    def test_invalid_values_and_braces(self):
        for value in (None, 1, [], "", "bad {", "bad }", "{bad token}"):
            self.write("en-US", {"example": value})
            self.assertTrue(validate(self.directory)[0])

    def test_missing_source(self):
        (self.directory / "zh-CN.json").unlink()
        self.assertTrue(validate(self.directory)[0])


if __name__ == "__main__":
    unittest.main()
