"""开发期生成立交桥本地底图；运行时不依赖 SVG/Python 或网络。
Generate local Interchange backgrounds at development time, not runtime.
"""
import argparse
import hashlib
import json
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

SVG_URL = "https://assets.tarkov.dev/maps/svg/Interchange.svg"
LAYOUT_URL = "https://raw.githubusercontent.com/the-hideout/tarkov-dev/main/src/data/maps.json"
LAYERS = ("Ground_Level", "First_Floor", "Second_Floor")
NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", NS)


def variants(raw):
    root = ET.fromstring(raw)
    view = [float(v) for v in root.attrib["viewBox"].split()]
    if len(view) != 4 or view[2] <= 0 or view[3] <= 0:
        raise ValueError("invalid SVG bounds")
    groups = [g.attrib.get("id") for g in root if g.tag == f"{{{NS}}}g"]
    if sorted(groups) != sorted(LAYERS):
        raise ValueError("unexpected Interchange layers")
    result = {}
    for layer in LAYERS:
        copy = ET.fromstring(raw)
        for group in list(copy):
            if group.tag == f"{{{NS}}}g" and group.attrib["id"] not in ("Ground_Level", layer):
                copy.remove(group)
        result[layer] = ET.tostring(copy, encoding="unicode")
    return result, view


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "Noven-maps-generator/1.0"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("assets/maps/interchange"))
    parser.add_argument("--offline", action="store_true", help="Reuse recorded SVG/config; never access network")
    args = parser.parse_args()
    if args.offline:
        raw = (args.output / "Interchange.svg").read_bytes()
        config = json.loads((args.output / "reference.json").read_text(encoding="utf-8"))
    else:
        raw = fetch(SVG_URL)
        records = json.loads(fetch(LAYOUT_URL))
        record = next(m for m in records if m["normalizedName"] == "interchange")
        config = next(m for m in record["maps"] if m["projection"] == "interactive")
    # 投影/楼层合同变化必须先核对原生适配，不静默生成错误坐标。
    # Projection/layer contract changes require native adapter review first.
    if config["bounds"] != [[598, -442], [-433, 426]] or config["coordinateRotation"] != 180:
        raise ValueError("projection changed; review native Interchange adapter")
    if [layer["svgLayer"] for layer in config["layers"]] != list(LAYERS[1:]):
        raise ValueError("floor layers changed")
    expected = [(25, 34), (34, 1000)]
    for layer, heights in zip(config["layers"], expected):
        if layer["extents"] != [{"height": list(heights), "bounds": [[[120, 218], [-222, -327], "mall"]]}]:
            raise ValueError("floor extents changed; review native adapter")
    images, view = variants(raw)
    if view != [0, 0, 1127.6852, 947.02582]:
        raise ValueError("SVG bounds changed; review native projection")
    import resvg_py
    outputs = {f"{layer}.png": resvg_py.svg_to_bytes(svg_string=svg, width=2048, skip_system_fonts=True)
               for layer, svg in images.items()}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "Interchange.svg").write_bytes(raw)
    (args.output / "reference.json").write_text(json.dumps(config, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8", newline="\n")
    for name, data in outputs.items():
        (args.output / name).write_bytes(data)
    manifest = {"source": SVG_URL, "layoutSource": LAYOUT_URL, "author": config["author"],
                "authorLink": config["authorLink"], "license": "CC BY-NC-SA 4.0",
                "licenseUrl": "https://creativecommons.org/licenses/by-nc-sa/4.0/",
                "svgSha256": hashlib.sha256(raw).hexdigest(), "viewBox": view,
                "renderer": "resvg-py 0.5.0; width 2048; system fonts disabled",
                "pngHashes": {name: hashlib.sha256(data).hexdigest() for name, data in outputs.items()}}
    (args.output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(manifest, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
