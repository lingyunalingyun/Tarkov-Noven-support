"""开发时生成离线任务数据；运行时不依赖 Python 或网络。
Generate offline task assets at development time; runtime uses neither Python nor network.
"""
import argparse
import concurrent.futures
import hashlib
import json
import re
import urllib.request
from pathlib import Path

BASE = "https://json.tarkov.dev/"
HEADERS = {
    "traders": "mode\tid\tnameZh\tnameEn\timageKey",
    "tasks": "mode\tid\ttraderId\tnameZh\tnameEn\tlocationZh\tlocationEn\tminLevel\texperience\tfaction\tkappaRequired\tlightkeeperRequired",
    "requirements": "mode\ttaskId\trequiredTaskId",
    "objectives": "mode\ttaskId\tid\ttype\tdescriptionZh\tdescriptionEn\tcount\toptional",
    "objective_items": "mode\tobjectiveId\titemId",
    "rewards": "mode\ttaskId\ttype\ttargetId\tvalue",
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


def integer(value, minimum=0):
    if type(value) is not int or not minimum <= value <= 2**63 - 1:
        raise ValueError(f"invalid integer: {value!r}")
    return value


def number(value):
    if type(value) not in (int, float):
        raise ValueError(f"invalid number: {value!r}")
    return format(value, ".15g")


def escape(value):
    text = str(value)
    text.encode("utf-8", errors="strict")
    if "\0" in text:
        raise ValueError("NUL in text")
    return text.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


def normalize(source, locales, traders, maps):
    rows = {key: [] for key in HEADERS}
    tasks = source["data"]["tasks"]
    if not isinstance(tasks, dict) or not tasks:
        raise ValueError("empty tasks")

    def names(key):
        en = locales["en"].get(key, key)
        zh = locales["zh"].get(key, "")
        if not isinstance(en, str) or not isinstance(zh, str):
            raise ValueError("invalid localized text")
        return zh, en

    used_traders = sorted({identity(task["trader"]) for task in tasks.values()})
    for trader_id in used_traders:
        trader = traders.get(trader_id)
        if not trader:
            raise ValueError(f"missing trader {trader_id}")
        image = trader.get("imageLink", "")
        expected = f"https://assets.tarkov.dev/{trader_id}.webp"
        if image and image != expected:
            raise ValueError(f"unsupported trader image URL: {image}")
        rows["traders"].append((trader_id, *names(trader["name"]), f"trader-{trader_id}" if image else ""))

    objective_ids = set()
    for task_id, task in tasks.items():
        identity(task_id)
        if task["id"] != task_id:
            raise ValueError("task ID mismatch")
        map_id = task.get("map")
        location = names(maps[map_id]["name"]) if map_id and map_id in maps else ("", "")
        rows["tasks"].append((task_id, identity(task["trader"]), *names(task["name"]),
                              *location, integer(task.get("minPlayerLevel", 0)),
                              integer(task.get("experience") or 0), str(task.get("factionName") or "Any"),
                              int(bool(task.get("kappaRequired", False))),
                              int(bool(task.get("lightkeeperRequired", False)))))
        seen_requirements = set()
        for requirement in task.get("taskRequirements", []):
            required = identity(requirement["task"])
            if required in seen_requirements:
                raise ValueError("duplicate task requirement")
            seen_requirements.add(required)
            rows["requirements"].append((task_id, required))
        for objective in task.get("objectives", []):
            source_objective_id = identity(objective["id"])
            objective_id = task_id + "_" + source_objective_id
            if objective_id in objective_ids:
                raise ValueError("duplicate task objective")
            objective_ids.add(objective_id)
            count = objective.get("count") or 0
            if type(count) not in (int, float) or count < 0:
                raise ValueError("invalid objective count")
            rows["objectives"].append((task_id, objective_id, str(objective.get("type", "unknown")),
                                       *names(objective["description"]), number(count),
                                       int(bool(objective.get("optional", False)))))
            for item_id in sorted(set(objective.get("items", []))):
                rows["objective_items"].append((objective_id, identity(item_id)))
        rewards = task.get("finishRewards", {})
        for reward in rewards.get("items", []):
            rows["rewards"].append((task_id, "item", identity(reward["item"]), number(reward["count"])))
        for reward in rewards.get("traderStanding", []):
            rows["rewards"].append((task_id, "standing", identity(reward["trader"]), number(reward["standing"])))
        for reward_type in ("offerUnlock", "craftUnlock", "traderUnlock", "achievement", "locationUnlock"):
            for reward in rewards.get(reward_type, []):
                target = reward if isinstance(reward, str) else reward.get("item") or reward.get("trader") or reward.get("id")
                if target:
                    rows["rewards"].append((task_id, reward_type, identity(target), "1"))

    task_ids = set(tasks)
    for _, required in rows["requirements"]:
        if required not in task_ids:
            raise ValueError(f"unresolved task requirement {required}")
    for key in rows:
        rows[key].sort()
        if key == "rewards":
            rows[key] = sorted(set(rows[key]))
        elif len(rows[key]) != len(set(rows[key])):
            raise ValueError(f"duplicate row in {key}")
    return rows


def build(regular, pve):
    # 比较所有运行时消费字段；两种模式不同就完整保留，绝不按数量猜测相同。
    # Compare every consumed field; preserve both modes whenever structure differs.
    differences = sum(len(set(regular[key]) ^ set(pve[key])) for key in HEADERS)
    modes = {"regular": regular}
    if differences:
        modes["pve"] = pve
    outputs = {}
    for key, header in HEADERS.items():
        lines = [header]
        for mode, tables in modes.items():
            lines.extend("\t".join(escape(value) for value in (mode, *row)) for row in tables[key])
        outputs[f"task_{key}.tsv"] = "\n".join(lines) + "\n"
    meta = {
        "schemaVersion": 1,
        "source": BASE,
        "regularTaskCount": len(regular["tasks"]),
        "pveTaskCount": len(pve["tasks"]),
        "regularPveStructuralDifferenceCount": differences,
        "structureModes": list(modes),
        "seasonalStructureFallback": "regular",
        "supportedSourceLocales": ["en", "zh"],
        "licensing": "Upstream attribution is not redistribution permission; review before public distribution.",
    }
    return outputs, meta


def fetch(path):
    request = urllib.request.Request(BASE + path, headers={"User-Agent": "Noven-tasks-generator/1.0"})
    with urllib.request.urlopen(request, timeout=40) as response:
        raw = response.read()
    return json.loads(raw.decode("utf-8"), object_pairs_hook=unique_object), hashlib.sha256(raw).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("assets/data"))
    args = parser.parse_args()
    paths = [f"{mode}/{kind}" for mode in ("regular", "pve")
             for kind in ("tasks", "tasks_en", "tasks_zh", "traders", "traders_en", "traders_zh",
                          "maps", "maps_en", "maps_zh")]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        fetched = dict(zip(paths, pool.map(fetch, paths)))
    variants = {}
    for mode in ("regular", "pve"):
        locales = {lang: {**fetched[f"{mode}/tasks_{lang}"][0]["data"],
                          **fetched[f"{mode}/traders_{lang}"][0]["data"],
                          **fetched[f"{mode}/maps_{lang}"][0]["data"]} for lang in ("en", "zh")}
        variants[mode] = normalize(fetched[f"{mode}/tasks"][0], locales,
                                   fetched[f"{mode}/traders"][0]["data"],
                                   fetched[f"{mode}/maps"][0]["data"]["maps"])
    outputs, meta = build(variants["regular"], variants["pve"])
    meta["sourceHashes"] = {BASE + path: digest for path, (_, digest) in fetched.items()}
    args.output.mkdir(parents=True, exist_ok=True)
    for name, content in outputs.items():
        (args.output / name).write_text(content, encoding="utf-8", newline="\n")
    (args.output / "task_catalog.meta.json").write_text(
        json.dumps(meta, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in meta.items() if key != "sourceHashes"}, ensure_ascii=False))


if __name__ == "__main__":
    main()
