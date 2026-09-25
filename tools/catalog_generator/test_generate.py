import json
import tempfile
import unittest
from pathlib import Path

from generate import build, read_json, write_catalog


NL545 = "68c2940aecc41cc5490bd40e"
OLD = "61bf7b6302b3924be92fa8c3"
NEW = "aaaaaaaaaaaaaaaaaaaaaaaa"


def canonical_row(item_id, types=None):
    return {"id": item_id, "width": 2, "height": 1,
            "types": types or ["gun"], "properties": {"caliber": "Caliber545x39"},
            "updated": "2026-09-20T00:00:00Z"}


class CatalogGeneratorTests(unittest.TestCase):
    def setUp(self):
        self.base = {"data": {"items": {
            NL545: canonical_row(NL545), OLD: canonical_row(OLD),
            NEW: canonical_row(NEW)}}}
        self.en = {"data": {
            NL545 + " Name": "Custom Guns NL545 (GP) 5.45x39 assault rifle",
            NL545 + " ShortName": "NL545 GP",
            OLD + " Name": "Metal spare parts", OLD + " ShortName": "M.parts",
            NEW + " Name": "New weapon", NEW + " ShortName": "New",
        }}
        self.zh = {"data": {NL545 + " Name": "NL545 (GP) 5.45x39 突击步枪",
                            NL545 + " ShortName": "NL545 GP"}}

    def test_canonical_english_survives_missing_chinese_and_stale_legacy(self):
        rows, meta = build(self.base, self.en, self.zh,
                           {OLD: ("金属零件", "金属零件"),
                            "bbbbbbbbbbbbbbbbbbbbbbbb": ("stale", "stale")})
        by_id = {row[0]: row for row in rows}
        self.assertEqual(len(rows), 3)
        self.assertEqual(by_id[NL545][1:5],
                         ("NL545 (GP) 5.45x39 突击步枪", "NL545 GP",
                          "Custom Guns NL545 (GP) 5.45x39 assault rifle", "NL545 GP"))
        self.assertEqual(by_id[NL545][5:], (2, 1, "gun", "Caliber545x39"))
        self.assertEqual(by_id[OLD][1:3], ("金属零件", "金属零件"))
        self.assertEqual(by_id[NEW][1:3], ("", ""))
        self.assertEqual(meta["legacy_ids_missing_from_current_source"],
                         ["bbbbbbbbbbbbbbbbbbbbbbbb"])
        self.assertEqual(meta["legacy_chinese_fields_used"], 2)

    def test_alias_collision_is_reported_not_overwritten(self):
        self.en["data"][NEW + " ShortName"] = "NL545 GP"
        rows, meta = build(self.base, self.en, self.zh, {})
        self.assertEqual(len(rows), 3)
        self.assertGreater(meta["alias_collision_count"], 0)
        self.assertTrue(any(alias == "nl545 gp" and ids == [NL545, NEW]
                            for alias, ids in meta["alias_collision_samples"]))

    def test_spt_enriches_missing_chinese_field_before_legacy(self):
        rows, meta = build(self.base, self.en, self.zh,
                           {OLD: ("legacy name", "legacy short")},
                           spt={OLD + " Name": "SPT 金属零件"})
        by_id = {row[0]: row for row in rows}
        self.assertEqual(by_id[OLD][1:3], ("SPT 金属零件", "legacy short"))
        self.assertEqual(meta["spt_chinese_fields_used"], 1)
        self.assertEqual(meta["legacy_chinese_fields_used"], 1)

    def test_duplicate_or_bad_identity_is_rejected(self):
        self.base["data"]["items"][NL545]["id"] = OLD
        with self.assertRaisesRegex(ValueError, "invalid canonical stable ID"):
            build(self.base, self.en, self.zh, {})

    def test_deterministic_output(self):
        rows, meta = build(self.base, self.en, self.zh, {})
        with tempfile.TemporaryDirectory() as directory:
            tsv = Path(directory) / "items_catalog.tsv"
            metadata = Path(directory) / "items_catalog.meta.json"
            write_catalog(rows, meta, tsv, metadata)
            first = (tsv.read_bytes(), metadata.read_bytes())
            write_catalog(rows, meta, tsv, metadata)
            self.assertEqual(first, (tsv.read_bytes(), metadata.read_bytes()))
            self.assertEqual(json.loads(metadata.read_text(encoding="utf-8"))["catalog_count"], 3)

    def test_malformed_utf8_and_duplicate_json_keys_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.json"
            source.write_bytes(b'{"data":"\xff"}')
            with self.assertRaises(UnicodeDecodeError):
                read_json(source)
            source.write_text('{"data":{"item":"first","item":"second"}}',
                              encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate JSON key"):
                read_json(source)


if __name__ == "__main__":
    unittest.main()
