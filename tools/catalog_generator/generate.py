"""Build the offline OCR catalog. Development tool only; never runs in the app."""

import argparse
import csv
import hashlib
import json
import re
import sys
import unicodedata
import urllib.request
from collections import defaultdict
from pathlib import Path


SOURCE = "https://json.tarkov.dev/regular/items"
SPT_SOURCE = ("https://raw.githubusercontent.com/sp-tarkov/server-csharp/main/"
              "Libraries/SPTarkov.Server.Assets/SPT_Data/database/locales/global/ch.json")
FIELDS = ("id", "nameZh", "shortNameZh", "nameEn", "shortNameEn",
          "width", "height", "types", "caliber")
ID_PATTERN = re.compile(r"[0-9a-f]{24}\Z")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def read_json(path):
    if isinstance(path, Path):
        raw = path.read_bytes()
    else:
        with urllib.request.urlopen(path, timeout=30) as response:
            raw = response.read()
    value = json.loads(raw.decode("utf-8"), object_pairs_hook=unique_object)
    return value, hashlib.sha256(raw).hexdigest()


def read_legacy(path):
    if path is None or not path.exists():
        return {}
    result = {}
    with path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.reader((line for line in stream if line and not line.startswith("#")),
                            delimiter="\t")
        for row in reader:
            if not row or row[0] == "id":
                continue
            if len(row) < 5 or not ID_PATTERN.fullmatch(row[0]):
                raise ValueError(f"invalid legacy catalog row: {row[:1]}")
            if row[0] in result:
                raise ValueError(f"duplicate legacy ID: {row[0]}")
            result[row[0]] = (row[1].replace("\\t", "\t").replace("\\n", "\n"),
                              row[2].replace("\\t", "\t").replace("\\n", "\n"))
    return result


def text(value, context):
    if value is None:
        return ""
    if not isinstance(value, str):
        raise ValueError(f"non-text localization at {context}")
    if "\x00" in value:
        raise ValueError(f"NUL in localization at {context}")
    return value.strip()


def alias_key(value):
    return " ".join(unicodedata.normalize("NFKC", value).casefold().split())


def build(canonical, english, chinese, legacy, economy=None, source_hash="", spt=None):
    items = canonical.get("data", {}).get("items")
    en = english.get("data")
    zh = chinese.get("data")
    if not isinstance(items, dict) or not isinstance(en, dict) or not isinstance(zh, dict):
        raise ValueError("unexpected tarkov.dev items/localization schema")
    if len(items) < 1:
        raise ValueError("canonical item data is empty")

    rows = []
    aliases = defaultdict(set)
    coverage = dict.fromkeys(("nameEn", "shortNameEn", "nameZh", "shortNameZh"), 0)
    fallback_fields = 0
    spt_fields = 0
    updated = []
    unsupported_ids = []
    for item_id in sorted(items):
        item = items[item_id]
        if not isinstance(item, dict) or item.get("id") != item_id:
            raise ValueError(f"invalid canonical stable ID: {item_id}")
        if not ID_PATTERN.fullmatch(item_id):
            # The API also publishes synthetic IDs (currently custom dogtags),
            # which cannot be looked up by the game's 24-hex template ID.
            unsupported_ids.append(item_id)
            continue
        name_en = text(en.get(item_id + " Name"), item_id + " English name")
        short_en = text(en.get(item_id + " ShortName"), item_id + " English shortName")
        if not name_en:
            raise ValueError(f"canonical item lacks English name: {item_id}")
        old_name, old_short = legacy.get(item_id, ("", ""))
        name_zh = text(zh.get(item_id + " Name"), item_id + " Chinese name")
        short_zh = text(zh.get(item_id + " ShortName"), item_id + " Chinese shortName")
        if not name_zh and spt:
            name_zh = text(spt.get(item_id + " Name"), item_id + " SPT Chinese name")
            spt_fields += bool(name_zh)
        if not short_zh and spt:
            short_zh = text(spt.get(item_id + " ShortName"), item_id + " SPT Chinese shortName")
            spt_fields += bool(short_zh)
        if not name_zh and old_name:
            name_zh = old_name
            fallback_fields += 1
        if not short_zh and old_short:
            short_zh = old_short
            fallback_fields += 1
        width, height = item.get("width"), item.get("height")
        if type(width) is not int or type(height) is not int \
                or not (1 <= width <= 30 and 1 <= height <= 30):
            raise ValueError(f"impossible dimensions for {item_id}: {width}x{height}")
        types = item.get("types", [])
        if not isinstance(types, list) or any(not isinstance(kind, str)
                                              or not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", kind)
                                              for kind in types):
            raise ValueError(f"invalid item types for {item_id}")
        properties = item.get("properties") or {}
        caliber = text(properties.get("caliber"), item_id + " caliber")
        if caliber and not re.fullmatch(r"[A-Za-z0-9_.-]+", caliber):
            raise ValueError(f"invalid caliber for {item_id}")
        row = (item_id, name_zh, short_zh, name_en, short_en,
               width, height, ";".join(sorted(set(types))), caliber)
        rows.append(row)
        for field, value in zip(FIELDS[1:5], row[1:5]):
            if value:
                coverage[field] += 1
                aliases[alias_key(value)].add(item_id)
        if isinstance(item.get("updated"), str):
            updated.append(item["updated"])

    canonical_ids = set(items) - set(unsupported_ids)
    economy_items = (economy or {}).get("items", {})
    economy_ids = ({item["id"] for item in economy_items}
                   if isinstance(economy_items, list) else set(economy_items))
    extra_localization = sorted(set(legacy) - canonical_ids)
    collisions = sorted((alias, sorted(ids)) for alias, ids in aliases.items()
                        if alias and len(ids) > 1)
    metadata = {
        "schema_version": 2,
        "generated_at": max(updated) if updated else "unknown",
        "canonical_source": SOURCE,
        "canonical_sha256": source_hash,
        "source_version": source_hash[:16] if source_hash else "unknown",
        "canonical_count": len(items),
        "non_template_ids_excluded": unsupported_ids,
        "localization_source": [SOURCE + "_zh", SPT_SOURCE,
                                "legacy SPT catalog by stable ID"],
        "catalog_count": len(rows),
        "alias_count": sum(1 for row in rows for value in row[1:5] if value),
        "coverage": coverage,
        "legacy_chinese_fields_used": fallback_fields,
        "spt_chinese_fields_used": spt_fields,
        "items_missing_all_aliases": 0,
        "economy_ids_missing_from_catalog": sorted(economy_ids - canonical_ids),
        "catalog_ids_missing_from_current_source": [],
        "legacy_ids_missing_from_current_source": extra_localization,
        "alias_collision_count": len(collisions),
        "alias_collision_samples": collisions[:20],
    }
    return rows, metadata


def write_catalog(rows, metadata, tsv_path, meta_path):
    # All validation has completed before either output is replaced.
    tsv_path.parent.mkdir(parents=True, exist_ok=True)
    meta_path.parent.mkdir(parents=True, exist_ok=True)
    tsv_temp = tsv_path.with_suffix(tsv_path.suffix + ".tmp")
    meta_temp = meta_path.with_suffix(meta_path.suffix + ".tmp")
    with tsv_temp.open("w", encoding="utf-8", newline="") as stream:
        stream.write("# Noven item catalog TSV v2; generated by tools/catalog_generator/generate.py\n")
        stream.write("\t".join(FIELDS) + "\n")
        for row in rows:
            values = list(row)
            while len(values) > 7 and values[-1] == "":
                values.pop()
            stream.write("\t".join(str(value).replace("\\", "\\\\")
                                   .replace("\t", "\\t").replace("\n", "\\n")
                                   for value in values) + "\n")
    meta_temp.write_text(json.dumps(metadata, ensure_ascii=False, indent=2,
                                    sort_keys=True) + "\n", encoding="utf-8")
    tsv_temp.replace(tsv_path)
    meta_temp.replace(meta_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--canonical-json", type=Path)
    parser.add_argument("--english-json", type=Path)
    parser.add_argument("--chinese-json", type=Path)
    parser.add_argument("--spt-json", type=Path)
    parser.add_argument("--legacy-tsv", type=Path)
    parser.add_argument("--economy-json", type=Path)
    parser.add_argument("--output-tsv", type=Path, required=True)
    parser.add_argument("--output-meta", type=Path, required=True)
    args = parser.parse_args()
    canonical, digest = read_json(args.canonical_json or SOURCE)
    english, _ = read_json(args.english_json or SOURCE + "_en")
    chinese, _ = read_json(args.chinese_json or SOURCE + "_zh")
    spt, _ = read_json(args.spt_json or SPT_SOURCE)
    economy = read_json(args.economy_json)[0] if args.economy_json else None
    rows, metadata = build(canonical, english, chinese,
                           read_legacy(args.legacy_tsv), economy, digest, spt)
    write_catalog(rows, metadata, args.output_tsv, args.output_meta)
    print(f"canonical={metadata['canonical_count']} catalog={metadata['catalog_count']} "
          f"aliases={metadata['alias_count']} collisions={metadata['alias_collision_count']}")
    print(f"coverage={metadata['coverage']} spt_zh_fields={metadata['spt_chinese_fields_used']} "
          f"legacy_zh_fields={metadata['legacy_chinese_fields_used']}")
    print(f"economy_missing={len(metadata['economy_ids_missing_from_catalog'])} "
          f"legacy_extra={len(metadata['legacy_ids_missing_from_current_source'])}")
    for alias, ids in metadata["alias_collision_samples"]:
        print(f"alias collision: {alias!r} -> {','.join(ids)}", file=sys.stderr)


if __name__ == "__main__":
    main()
