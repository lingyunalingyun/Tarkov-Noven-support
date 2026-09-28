"""开发时生成藏身处离线数据；运行时不依赖 Python。
Generate offline hideout assets at development time; no runtime Python.
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import re
import urllib.request
from pathlib import Path

BASE = "https://json.tarkov.dev/"
def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON identity/key: {key}")
        result[key] = value
    return result

HEADERS = {
    "station_images": "mode\tstationId\timageKey",
    "crafts": "mode\tid\tstationId\tlevel\tseconds\titemId\tcount\trestrictions",
    "craft_materials": "mode\tcraftId\titemId\tcount\ttool\tfunctional",
    "stations": "mode\tid\tnameZh\tnameEn",
    "levels": "mode\tid\tstation\tlevel\tseconds",
    "item_requirements": "mode\tlevelId\titemId\tcount",
    "station_requirements": "mode\tlevelId\tstationId\tlevel",
    "skill_requirements": "mode\tlevelId\tskillId\tnameZh\tnameEn\tlevel",
    "trader_requirements": "mode\tlevelId\ttraderId\tnameZh\tnameEn\tlevel",
}

def identity(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_-]+", value):
        raise ValueError(f"invalid identity: {value!r}")
    return value

def number(value, minimum=1):
    if type(value) is not int or not minimum <= value <= 2**63-1:
        raise ValueError(f"invalid integer: {value!r}")
    return value

def escape(value):
    text = str(value)
    text.encode("utf-8", errors="strict")
    if "\0" in text:
        raise ValueError("NUL in text")
    return text.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")

def craft_quantity(value):
    # 部分配方按资源比例消耗（例如 0.66 个滤水器），不可取整为零。
    # Some recipes consume a resource fraction (e.g. 0.66 filters); never truncate to zero.
    if type(value) not in (int, float) or not math.isfinite(value) or not 0 < value <= 2**53:
        raise ValueError("invalid craft quantity")
    return str(value)

def normalize(source, locales, traders, crafts=None):
    rows = {key: [] for key in HEADERS}
    stations = source["data"]
    if not isinstance(stations, dict) or not stations:
        raise ValueError("empty/invalid stations")
    def names(key):
        en = locales["en"].get(key, key)
        zh = locales["zh"].get(key, "")
        if not isinstance(en, str) or not isinstance(zh, str):
            raise ValueError("invalid localized text")
        return zh, en
    level_ids, station_levels = set(), set()
    for sid, station in stations.items():
        identity(sid)
        if station["id"] != sid:
            raise ValueError("station ID mismatch")
        rows["stations"].append((sid, *names(station["name"])))
        image = station.get("imageLink", "")
        if image:
            match = re.fullmatch(r"https://assets\.tarkov\.dev/(station-[a-z0-9-]+)\.png", image)
            if not match:
                raise ValueError("unsupported station image URL")
            rows["station_images"].append((sid, match[1]))
        for level in station["levels"]:
            lid, n = identity(level["id"]), number(level["level"])
            if lid in level_ids or (sid, n) in station_levels:
                raise ValueError("duplicate station/level")
            level_ids.add(lid)
            station_levels.add((sid, n))
            seconds = level.get("constructionTime")
            rows["levels"].append((lid, sid, n, "" if seconds is None else number(seconds, 0)))
            seen = set()
            for req in level["itemRequirements"]:
                iid = identity(req["item"])
                if iid in seen:
                    raise ValueError("duplicate item requirement")
                seen.add(iid)
                rows["item_requirements"].append((lid, iid, number(req["count"])))
            for req in level["stationLevelRequirements"]:
                rows["station_requirements"].append((lid, identity(req["station"]), number(req["level"])))
            for req in level["skillRequirements"]:
                skill = identity(req["skill"])
                rows["skill_requirements"].append((lid, skill, *names(skill), number(req["level"])))
            for req in level["traderRequirements"]:
                if req.get("requirementType") != "level" or req.get("compareMethod") != ">=":
                    raise ValueError("unsupported trader requirement semantics")
                tid = identity(req["trader"])
                rows["trader_requirements"].append((lid, tid, *names(traders[tid]["name"]), number(req["value"])))
    for _, sid, n in rows["station_requirements"]:
        if (sid, n) not in station_levels:
            raise ValueError(f"unresolved station prerequisite {sid}:{n}")
    seen_crafts = set()
    for craft in crafts or []:
        cid, sid, n = identity(craft["id"]), identity(craft["station"]), number(craft["level"])
        if cid in seen_crafts or (sid, n) not in station_levels:
            raise ValueError("duplicate craft/unresolved craft station level")
        seen_crafts.add(cid)
        product = craft["productItem"]
        # 解锁限制仅保留并提示，不能据此推断玩家可制造。
        # Preserve restrictions for display; never infer player craft eligibility.
        restrictions = {key: craft[key] for key in ("taskUnlock", "gameEditions", "requiredQuestItems") if craft.get(key)}
        rows["crafts"].append((cid, sid, n, number(craft["duration"], 0), identity(product["item"]),
            number(product["count"]), json.dumps(restrictions, sort_keys=True, ensure_ascii=False) if restrictions else ""))
        seen_items = set()
        for req in craft["requiredItems"]:
            iid = identity(req["item"])
            if iid in seen_items:
                raise ValueError("duplicate craft material")
            seen_items.add(iid)
            attr = req.get("attributes", {})
            if any(key not in ("tool", "functional") for key in attr) or any(type(v) is not bool for v in attr.values()):
                raise ValueError("unsupported craft material attributes")
            rows["craft_materials"].append((cid, iid, craft_quantity(req["count"]), int(attr.get("tool", False)), int(attr.get("functional", False))))
    for key in rows:
        rows[key].sort()
        if len(rows[key]) != len(set(rows[key])):
            raise ValueError(f"duplicate row in {key}")
    return rows

def build(regular, pve, item_ids):
    # 比较全部所用结构字段，而不是只比较数量；不同模式绝不静默合并。
    # Compare every consumed structural field, not counts; never flatten mode differences.
    differences = sum(len(set(regular[k]) ^ set(pve[k])) for k in HEADERS)
    modes = {"regular": regular}
    if differences:
        modes["pve"] = pve
    outputs = {}
    for key, header in HEADERS.items():
        lines = [header]
        for mode, tables in modes.items():
            lines.extend("\t".join(escape(v) for v in (mode, *row)) for row in tables[key])
        outputs[f"hideout_{key}.tsv"] = "\n".join(lines) + "\n"
    missing = sorted({r[1] for tables in modes.values() for r in tables["item_requirements"]} - item_ids)
    meta = {"schemaVersion": 1, "source": BASE,
            "stationCount": len(regular["stations"]), "levelCount": len(regular["levels"]),
            "itemRequirementCount": len(regular["item_requirements"]),
            "craftCount": len(regular["crafts"]),
            "craftMaterialCount": len(regular["craft_materials"]),
            "regularPveStructuralDifferenceCount": differences,
            "structureModes": list(modes), "seasonalStructureFallback": "regular",
            "supportedSourceLocales": ["en", "zh"], "missingItemIds": missing,
            "licensing": "Upstream attribution is not redistribution permission; review before public distribution."}
    return outputs, meta

def fetch(path):
    for attempt in range(3):
        try:
            request = urllib.request.Request(BASE + path, headers={"User-Agent": "Noven-hideout-generator/1.0"})
            with urllib.request.urlopen(request, timeout=40) as response:
                raw = response.read()
            return json.loads(raw.decode("utf-8"), object_pairs_hook=unique_object), hashlib.sha256(raw).hexdigest()
        except Exception:
            if attempt == 2:
                raise

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("assets/data"))
    parser.add_argument("--items", type=Path, default=Path("assets/data/items_catalog.tsv"))
    args = parser.parse_args()
    paths = [f"{mode}/{kind}" for mode in ("regular", "pve")
             for kind in ("hideout", "hideout_en", "hideout_zh", "traders", "traders_en", "traders_zh", "crafts")]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        fetched = dict(zip(paths, pool.map(fetch, paths)))
    variants = {}
    for mode in ("regular", "pve"):
        locales = {lang: {**fetched[f"{mode}/hideout_{lang}"][0]["data"],
                          **fetched[f"{mode}/traders_{lang}"][0]["data"]} for lang in ("en", "zh")}
        variants[mode] = normalize(fetched[f"{mode}/hideout"][0], locales, fetched[f"{mode}/traders"][0]["data"], fetched[f"{mode}/crafts"][0]["data"])
    ids = {line.split("\t")[0] for line in args.items.read_text(encoding="utf-8").splitlines()
           if re.match(r"^[0-9a-f]{24}\t", line)}
    outputs, meta = build(variants["regular"], variants["pve"], ids)
    meta["sourceHashes"] = {BASE + path: digest for path, (_, digest) in fetched.items()}
    # 用输入哈希代替墙钟时间，确保相同输入产生逐字节相同输出。
    # Input hashes replace wall-clock timestamps to keep identical inputs byte deterministic.
    args.output.mkdir(parents=True, exist_ok=True)
    for name, content in outputs.items():
        (args.output / name).write_text(content, encoding="utf-8", newline="\n")
    (args.output / "hideout_catalog.meta.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2, sort_keys=True)+"\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in meta.items() if k != "sourceHashes"}, ensure_ascii=False))

if __name__ == "__main__":
    main()
