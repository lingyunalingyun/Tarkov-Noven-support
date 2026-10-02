"""开发期接入 Tarkov.dev 地图点位；不下载底图、不生成游戏状态。
Generate local map positions from Tarkov.dev; no images or game-state access.
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
    "outlines": "pointId\tvertex\tx\ty\tz",
    "conditions": "pointId\tfield\tvalue",
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


def normalize(source, locales, tasks=None, task_locales=None, map_slug="interchange"):
    if source.get("errors"):
        raise ValueError("upstream errors")
    maps = source["data"]["maps"]
    matches = [(key, m) for key, m in maps.items() if m["normalizedName"] == map_slug]
    if len(matches) != 1:
        raise ValueError(f"exactly one {map_slug} map required")
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
    rows = {"maps": [(map_id, identity(map_slug), *names(m["name"]), rotation, duration, text(m["players"]))],
            "points": [], "outlines": [], "conditions": []}
    missing = Counter()
    seen = {}

    def add(kind, subtype, source_id, label, position, localized=None):
        if position is None:
            missing[kind] += 1
            return
        xyz = tuple(number(position[axis]) for axis in ("x", "y", "z"))
        source_id, subtype = text(source_id), text(subtype)
        # 上游无唯一点位 ID 时，使用类型、来源身份与完整坐标，不依赖列表下标。
        # Missing point IDs use type, source identity and full coordinates, never rendering indexes.
        stable = json.dumps([map_id, kind, subtype, source_id, *xyz], separators=(",", ":"), ensure_ascii=False)
        point_id = kind + "-" + hashlib.sha256(stable.encode("utf-8")).hexdigest()
        label_names = localized if localized is not None else names(label)
        row = (point_id, map_id, kind, subtype, source_id, *label_names, *xyz)
        if point_id in seen and seen[point_id] != row:
            raise ValueError("conflicting point identity")
        seen[point_id] = row
        return point_id

    def geometry(point_id, record):
        if point_id:
            for index, position in enumerate(record.get("outline") or []):
                rows["outlines"].append((point_id, index, *(number(position[axis]) for axis in ("x", "y", "z"))))

    def conditions(point_id, record):
        if point_id:
            for field in ("switch", "switches", "transferItem"):
                if field in record and record[field] is not None:
                    rows["conditions"].append((point_id, field, json.dumps(record[field], ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)))

    for p in m.get("lootContainers", []):
        add("container", "", identity(p["lootContainer"]), p["lootContainer"], p.get("position"))
    for p in m.get("lootLoose", []):
        items = sorted(identity(item) for item in p.get("items", []))
        add("loose", "", ",".join(items) or "unknown", "Loose loot", p.get("position"), ("散落物", "Loose loot"))
    for p in m.get("locks", []):
        lock_id = identity(p["id"])
        key_id = identity(p["key"]) if p.get("key") else "unknown"
        add("lock", f"{text(str(p.get('lockType') or ''))}:{lock_id}", key_id, "Lock", p.get("position"), ("锁", "Lock"))
    for p in m.get("switches", []):
        add("switch", text(str(p.get("switchType") or "")), identity(p["id"]), "Switch", p.get("position"), ("开关", "Switch"))
    for p in m.get("stationaryWeapons", []):
        add("stationary", "", identity(p["stationaryWeapon"]), "Stationary weapon", p.get("position"), ("固定武器", "Stationary weapon"))
    for p in m.get("extracts", []):
        # 变体接口有时不提供阵营，明确保留 unknown，不套用普通版本的阵营。
        # Variant sources may omit faction; retain unknown rather than assume the regular map's faction.
        point_id = add("extract", p.get("faction") or "unknown", identity(p["id"]), p["name"], p.get("position"))
        geometry(point_id, p)
        conditions(point_id, p)
    for p in m.get("transits", []):
        point_id = add("transit", identity(p["map"]), identity(p["id"]), p["description"], p.get("position"))
        geometry(point_id, p)
        conditions(point_id, p)
    for p in m.get("spawns", []):
        subtype = json.dumps({"sides": sorted(set(p["sides"])), "categories": sorted(set(p["categories"]))}, separators=(",", ":"), sort_keys=True)
        add("spawn", subtype, p["zoneName"], p["zoneName"], p.get("position"))
    for p in m.get("hazards", []):
        point_id = add("hazard", p["hazardType"], p["name"], p["name"], p.get("position"))
        geometry(point_id, p)
    for boss in m.get("bosses", []):
        for location in boss.get("spawnLocations", []):
            for position in location.get("positions", []):
                add("boss", location["name"], boss["mob"], boss["mob"], position)
    for p in m.get("btrStops", []):
        add("btr", "", p["name"], p["name"], p)
    for index, p in enumerate((m.get("artillery") or {}).get("zones", [])):
        add("artillery", "", f"artillery-{index}", "Artillery zone", p.get("position"), ("炮击区", "Artillery zone"))
    if tasks:
        if not task_locales:
            raise ValueError("task locales required")
        def task_names(key):
            key = text(key)
            en = text(task_locales["en"].get(key, key))
            zh = text(task_locales["zh"].get(key, en))
            return zh or en, en
        for task_id, task in tasks["data"]["tasks"].items():
            task_id = identity(task_id)
            task_zh, task_en = task_names(task["name"])
            for objective in task.get("objectives", []):
                objective_id = identity(objective["id"])
                desc_zh, desc_en = task_names(objective["description"])
                labels = (f"{task_zh} · {desc_zh}", f"{task_en} · {desc_en}")
                for zone in objective.get("zones") or []:
                    if zone.get("map") == map_id:
                        source_id = f"{task_id}_{objective_id}_{text(zone['id'])}"
                        add("task", "objective", source_id, labels[1], zone.get("position"), labels)
                for location in objective.get("possibleLocations") or []:
                    if location.get("map") != map_id:
                        continue
                    for position in location.get("positions") or []:
                        source_id = f"{task_id}_{objective_id}"
                        add("task", "item", source_id, labels[1], position, labels)
    rows["points"] = sorted(seen.values())
    for key in ("outlines", "conditions"):
        rows[key] = sorted(set(rows[key]))
    if not rows["points"]:
        raise ValueError("no positioned data")
    return rows, dict(sorted(missing.items()))


def build(source, locales, tasks=None, task_locales=None, all_maps=False, mode="regular"):
    if mode not in ("regular", "pve"):
        raise ValueError("unsupported structural mode")
    if all_maps:
        # 按上游地图身份合并，变体保持独立；排序与生成顺序无关。
        # Merge by upstream map identity, keeping variants independent and order deterministic.
        rows = {key: [] for key in HEADERS}
        missing = {}
        slugs = [m["normalizedName"] for m in source["data"]["maps"].values()]
        if len(set(slugs)) != len(slugs):
            raise ValueError("duplicate map slug")
        for slug in sorted(slugs):
            part, absent = normalize(source, locales, tasks, task_locales, slug)
            for key in rows:
                rows[key].extend(part[key])
            if absent:
                missing[slug] = absent
        for values in rows.values():
            values.sort()
    else:
        rows, missing = normalize(source, locales, tasks, task_locales)
    outputs = {}
    for key, header in HEADERS.items():
        outputs[f"map_{key}.tsv"] = header + "\n" + "".join("\t".join(escape(v) for v in row) + "\n" for row in rows[key])
    meta = {
        "schemaVersion": 1, "source": BASE, "structureMode": mode, "map": "all" if all_maps else "interchange",
        "mapCount": len(rows["maps"]), "pointCount": len(rows["points"]),
        "pointKinds": dict(sorted(Counter(row[2] for row in rows["points"]).items())),
        "missingPositions": missing,
        "coordinateSpace": "upstream world x/y/z; y is height, not screen y",
        "floorAssignment": "not supplied by this API; requires verified map-layer extents",
        "imagesIncluded": False,
        "outlineVertexCount": len(rows["outlines"]), "conditionCount": len(rows["conditions"]),
        "conditionBoundary": "source switch/switches/transferItem only; no inferred live eligibility",
        "omittedFields": [],
        "licensing": "API provenance is not map-image redistribution permission; no images are bundled.",
    }
    return outputs, meta


def fetch(path, cache=None, offline=False):
    file = cache / (path.replace("/", "_") + ".json") if cache else None
    if offline:
        if file is None:
            raise ValueError("offline fetch requires cache")
        raw = file.read_bytes()
    else:
        request = urllib.request.Request(BASE + path, headers={"User-Agent": "Noven-maps-generator/1.0"})
        with urllib.request.urlopen(request, timeout=30) as response:
            raw = response.read()
    parsed = json.loads(raw.decode("utf-8"), object_pairs_hook=unique_object)
    if file and not offline:
        file.write_bytes(raw)
    return parsed, hashlib.sha256(raw).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("assets/data"))
    parser.add_argument("--all-maps", action="store_true")
    parser.add_argument("--mode", choices=("regular", "pve"), default="regular")
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    if args.offline and not args.cache:
        parser.error("--offline requires --cache")
    if args.cache:
        args.cache.mkdir(parents=True, exist_ok=True)
    paths = [f"{args.mode}/{kind}{suffix}" for kind in ("maps", "tasks") for suffix in ("", "_en", "_zh")]
    def cached_fetch(path):
        return fetch(path, args.cache, args.offline)
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        fetched = dict(zip(paths, pool.map(cached_fetch, paths)))
    locales = {lang: fetched[f"{args.mode}/maps_{lang}"][0]["data"] for lang in ("en", "zh")}
    task_locales = {lang: fetched[f"{args.mode}/tasks_{lang}"][0]["data"] for lang in ("en", "zh")}
    outputs, meta = build(fetched[f"{args.mode}/maps"][0], locales, fetched[f"{args.mode}/tasks"][0], task_locales, args.all_maps, args.mode)
    meta["sourceHashes"] = {BASE + path: digest for path, (_, digest) in fetched.items()}
    args.output.mkdir(parents=True, exist_ok=True)
    for name, content in outputs.items():
        (args.output / name).write_text(content, encoding="utf-8", newline="\n")
    (args.output / "map_catalog.meta.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(meta, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
