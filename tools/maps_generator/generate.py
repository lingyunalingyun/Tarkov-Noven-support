"""开发期接入 Tarkov.dev 立交桥点位；不下载底图、不生成游戏状态。
Generate local Interchange positions from Tarkov.dev; no images or game-state access.
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import re
import urllib.request
from collections import Counter
from pathlib import Path

BASE = "https://json.tarkov.dev/"
HEADERS = {
    "maps": "id\tnormalizedName\tnameZh\tnameEn\trotation\traidDuration\tplayers",
    "points": "id\tmapId\tkind\tsubtype\tsourceId\tnameZh\tnameEn\tx\ty\tz",
}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def identity(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_-]+", value):
        raise ValueError(f"invalid identity: {value!r}")
    return value


def text(value):
    if not isinstance(value, str) or "\0" in value:
        raise ValueError("invalid text")
    value.encode("utf-8", errors="strict")
    return value


def number(value):
    if type(value) not in (int, float) or not math.isfinite(value):
        raise ValueError("invalid coordinate/number")
    return format(value, ".15g")


def escape(value):
    return text(str(value)).replace("\\", "\\\\").replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n")


def normalize(source, locales):
    if source.get("errors"):
        raise ValueError("upstream errors")
    maps = source["data"]["maps"]
    matches = [(key, m) for key, m in maps.items() if m["normalizedName"] == "interchange"]
    if len(matches) != 1:
        raise ValueError("exactly one Interchange map required")
    key, m = matches[0]
    map_id = identity(m["id"])
    if key != map_id:
        raise ValueError("map ID mismatch")

    def names(key):
        key = text(key)
        en = text(locales["en"].get(key, key))
        zh = text(locales["zh"].get(key, en))
        return zh or en, en

    duration = m["raidDuration"]
    if type(duration) is not int or not 0 <= duration <= 1440:
        raise ValueError("invalid raid duration")
    rotation = number(m["coordinateToCardinalRotation"])
    rows = {"maps": [(map_id, "interchange", *names(m["name"]), rotation, duration, text(m["players"]))], "points": []}
    missing = Counter()
    seen = {}

    def add(kind, subtype, source_id, label, position):
        if position is None:
            missing[kind] += 1
            return
        xyz = tuple(number(position[axis]) for axis in ("x", "y", "z"))
        source_id, subtype = text(source_id), text(subtype)
        # 上游无唯一点位 ID 时，使用类型、来源身份与完整坐标，不依赖列表下标。
        # Missing point IDs use type, source identity and full coordinates, never rendering indexes.
        stable = json.dumps([map_id, kind, subtype, source_id, *xyz], separators=(",", ":"), ensure_ascii=False)
        point_id = kind + "-" + hashlib.sha256(stable.encode("utf-8")).hexdigest()
        row = (point_id, map_id, kind, subtype, source_id, *names(label), *xyz)
        if point_id in seen and seen[point_id] != row:
            raise ValueError("conflicting point identity")
        seen[point_id] = row

    for p in m.get("lootContainers", []):
        add("container", "", identity(p["lootContainer"]), p["lootContainer"], p.get("position"))
    for p in m.get("extracts", []):
        add("extract", p["faction"], identity(p["id"]), p["name"], p.get("position"))
    for p in m.get("transits", []):
        add("transit", identity(p["map"]), identity(p["id"]), p["description"], p.get("position"))
    for p in m.get("spawns", []):
        subtype = json.dumps({"sides": sorted(set(p["sides"])), "categories": sorted(set(p["categories"]))}, separators=(",", ":"), sort_keys=True)
        add("spawn", subtype, p["zoneName"], p["zoneName"], p.get("position"))
    for p in m.get("hazards", []):
        add("hazard", p["hazardType"], p["name"], p["name"], p.get("position"))
    for boss in m.get("bosses", []):
        for location in boss.get("spawnLocations", []):
            for position in location.get("positions", []):
                add("boss", location["name"], boss["mob"], boss["mob"], position)
    for p in m.get("btrStops", []):
        add("btr", "", p["name"], p["name"], p)
    rows["points"] = sorted(seen.values())
    if not rows["points"]:
        raise ValueError("no positioned data")
    return rows, dict(sorted(missing.items()))


def build(source, locales):
    rows, missing = normalize(source, locales)
    outputs = {}
    for key, header in HEADERS.items():
        outputs[f"map_{key}.tsv"] = header + "\n" + "".join("\t".join(escape(v) for v in row) + "\n" for row in rows[key])
    meta = {
        "schemaVersion": 1, "source": BASE, "structureMode": "regular", "map": "interchange",
        "mapCount": len(rows["maps"]), "pointCount": len(rows["points"]),
        "pointKinds": dict(sorted(Counter(row[2] for row in rows["points"]).items())),
        "missingPositions": missing,
        "coordinateSpace": "upstream world x/y/z; y is height, not screen y",
        "floorAssignment": "not supplied by this API; requires verified map-layer extents",
        "imagesIncluded": False,
        "omittedFields": ["lootLoose", "locks", "switches", "stationaryWeapons", "artillery", "tasks", "outlines", "extractConditions"],
        "licensing": "API provenance is not map-image redistribution permission; no images are bundled.",
    }
    return outputs, meta


def fetch(path):
    request = urllib.request.Request(BASE + path, headers={"User-Agent": "Noven-maps-generator/1.0"})
    with urllib.request.urlopen(request, timeout=30) as response:
        raw = response.read()
    return json.loads(raw.decode("utf-8"), object_pairs_hook=unique_object), hashlib.sha256(raw).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("assets/data"))
    args = parser.parse_args()
    paths = ["regular/maps", "regular/maps_en", "regular/maps_zh"]
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        fetched = dict(zip(paths, pool.map(fetch, paths)))
    locales = {lang: fetched[f"regular/maps_{lang}"][0]["data"] for lang in ("en", "zh")}
    outputs, meta = build(fetched["regular/maps"][0], locales)
    meta["sourceHashes"] = {BASE + path: digest for path, (_, digest) in fetched.items()}
    args.output.mkdir(parents=True, exist_ok=True)
    for name, content in outputs.items():
        (args.output / name).write_text(content, encoding="utf-8", newline="\n")
    (args.output / "map_catalog.meta.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(meta, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
