"""Bundle the DEV Interchange satellite atlas with the verified world projection."""
import argparse
import concurrent.futures
import hashlib
import io
import json
import math
from pathlib import Path
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from PIL import Image
from generate_images import tile_pack

ZOOM = 4
TILE = 256


def upper_overlay(raw, floor):
    root = ET.fromstring(raw)
    for group in list(root):
        if group.tag.endswith('}g') and group.attrib.get('id') != floor:
            root.remove(group)
    return ET.tostring(root, encoding='unicode')


def projected_bounds(config, zoom=ZOOM):
    if config['bounds'] != [[598, -442], [-433, 426]] or config['coordinateRotation'] != 180:
        raise ValueError('review changed satellite projection')
    if config['transform'] != [0.265, 150.6, 0.265, 134.6]:
        raise ValueError('review changed satellite transform')
    scale = 2 ** zoom
    return ((-598 * .265 + 150.6) * scale, (-442 * .265 + 134.6) * scale,
            (433 * .265 + 150.6) * scale, (426 * .265 + 134.6) * scale)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('assets/maps/interchange'))
    parser.add_argument('--cache', type=Path, default=Path('build/map-satellite-sources'))
    parser.add_argument('--offline', action='store_true')
    args = parser.parse_args()
    config = json.loads((args.output / 'reference.json').read_text(encoding='utf-8'))
    bounds = projected_bounds(config)
    x0, y0, x1, y1 = (math.floor(v / TILE) for v in bounds)
    args.cache.mkdir(parents=True, exist_ok=True)

    def fetch(pair):
        x, y = pair
        url = config['tilePath'].format(z=ZOOM, x=x, y=y)
        name = hashlib.sha256(url.encode()).hexdigest()
        file = args.cache / name
        absent = args.cache / (name + '.404')
        if file.exists():
            raw = file.read_bytes()
        elif absent.exists():
            raw = None
        elif args.offline:
            raise ValueError('offline tile missing: ' + url)
        else:
            try:
                request = urllib.request.Request(url, headers={'User-Agent': 'Noven-map-assets/1.0'})
                with urllib.request.urlopen(request, timeout=30) as response:
                    raw = response.read(2 * 1024 * 1024 + 1)
                if len(raw) > 2 * 1024 * 1024:
                    raise ValueError('tile too large')
                file.write_bytes(raw)
            except urllib.error.HTTPError as error:
                if error.code != 404:
                    raise
                raw = None
                absent.touch()
        # 边缘缺图保留透明；内部缺图拒绝生成，不伪造卫星图。
        # Keep missing edge coverage transparent; reject interior gaps, never invent imagery.
        if raw is None and x not in (x0, x1) and y not in (y0, y1):
            raise ValueError('interior satellite tile unavailable: ' + url)
        return x, y, url, raw

    pairs = [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        sources = list(pool.map(fetch, pairs))
    atlas = Image.new('RGBA', ((x1 - x0 + 1) * TILE, (y1 - y0 + 1) * TILE))
    manifest = []
    for x, y, url, raw in sources:
        manifest.append({'url': url, 'sha256': hashlib.sha256(raw).hexdigest() if raw else None})
        if raw:
            image = Image.open(io.BytesIO(raw)).convert('RGBA')
            if image.size != (TILE, TILE):
                raise ValueError('unexpected tile dimensions')
            atlas.paste(image, ((x - x0) * TILE, (y - y0) * TILE))
    width, height = math.ceil(bounds[2] - bounds[0]), math.ceil(bounds[3] - bounds[1])
    atlas = atlas.transform((width, height), Image.Transform.AFFINE,
                            (1, 0, bounds[0] - x0 * TILE, 0, 1, bounds[1] - y0 * TILE), Image.Resampling.BICUBIC)
    detail = io.BytesIO()
    atlas.save(detail, format='PNG')
    preview = atlas.resize((2048, round(height * 2048 / width)), Image.Resampling.LANCZOS)
    preview_bytes = io.BytesIO()
    preview.save(preview_bytes, format='PNG')
    outputs = {'Satellite.png': preview_bytes.getvalue(), 'Satellite.tiles': tile_pack(detail.getvalue())}
    # DEV 上层无卫星瓦片时使用 SVG 上层覆盖；地面不重复覆盖卫星底图。
    # DEV uses SVG upper overlays when upper satellite tiles are absent; never repaint the ground.
    import resvg_py
    svg = (args.output / 'Interchange.svg').read_bytes()
    for floor in ('First_Floor', 'Second_Floor'):
        overlay = upper_overlay(svg, floor)
        outputs[f'{floor}.overlay.png'] = resvg_py.svg_to_bytes(svg_string=overlay, width=2048, skip_system_fonts=True)
        outputs[f'{floor}.overlay.tiles'] = tile_pack(resvg_py.svg_to_bytes(svg_string=overlay, width=8192, skip_system_fonts=True))
    for name, raw in outputs.items():
        (args.output / name).write_bytes(raw)
    result = {'source': config['tilePath'], 'zoom': ZOOM, 'pixelBounds': bounds,
              'projection': 'same rotated world bounds as local SVG; subpixel edge crop',
              'sources': manifest, 'outputs': {name: hashlib.sha256(raw).hexdigest() for name, raw in outputs.items()}}
    (args.output / 'satellite.manifest.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(f'Satellite: {width}x{height}, {sum(raw is not None for _, _, _, raw in sources)}/{len(sources)} tiles')


if __name__ == '__main__':
    main()
