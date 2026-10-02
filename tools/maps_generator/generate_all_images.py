"""开发期生成全部 DEV 地图资产；生产端只消费 PNG/NVTILES1/TSV。
Build real DEV map assets; native runtime needs no Python or remote conversion.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
import math
from pathlib import Path
import re
import struct
import threading
import time
import xml.etree.ElementTree as ET

from PIL import Image
import requests
from generate_images import tile_pack

ROOT = Path(__file__).resolve().parents[2]
LAYOUT_URL = 'https://raw.githubusercontent.com/the-hideout/tarkov-dev/main/src/data/maps.json'
API_URL = 'https://json.tarkov.dev/regular/maps'
SVG_RIGHTS_URL = 'https://raw.githubusercontent.com/the-hideout/tarkov-dev-svg-maps/main/README.md'
HEADERS = {
    'map_references.tsv': 'mapId\tslug\tbaseFloor\twidth\theight\trotation\tminX\tmaxX\tminZ\tmaxZ\tauthor',
    'map_floors.tsv': 'mapId\tfloorId\tnameZh\tnameEn\torder\tabstractPath\tsatellitePath',
    'map_extents.tsv': 'mapId\tfloorId\tbottom\ttop\tminX\tmaxX\tminZ\tmaxZ',
    'map_update_assets.tsv': 'relativePath\turl\tsha256',
    'map_compositions.tsv': 'outputPath\tsourcePath\twidth\theight\ttileSize\ttileX\ttileY\tleft\ttop\tright\tbottom\tblend',
}
ZH = {'Ground Level': '地面', 'Ground Floor': '地面', '2nd Floor': '二层',
      '3rd Floor': '三层', '4th Floor': '四层', '5th Floor': '五层',
      'Underground': '地下', 'Garage': '车库', 'Tunnels': '隧道',
      'Bunkers': '地堡', 'Second Level': '二层', 'Technical': '技术层',
      'Infirmary': '医务室', 'Helipad': '直升机坪', 'Gym/Canteen': '健身房/食堂',
      'Accommodation (lower)': '住宿区（下层）', 'Accommodation (mid)': '住宿区（中层）',
      'Accommodation (upper)': '住宿区（上层）', "Officers' Deck": '军官甲板',
      'Stairs (blocked)': '楼梯（封闭）', 'Bridge': '驾驶室', 'Bridge Roof': '驾驶室屋顶',
      'Control Room': '控制室', 'Engine Room': '轮机舱', 'Engine Room (upper)': '轮机舱（上层）',
      'Fuel Pumps (lower)': '燃油泵（下层）', 'Fuel Pumps': '燃油泵', 'Storage/Security': '储藏/安保'}


class SourceUnavailable(ValueError):
    """真实来源覆盖缺失，可记录缺图；不包含离线缓存错误。
    Real source coverage is absent; excludes offline cache/integrity errors.
    """


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + '\n').encode('utf-8')


def identity(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9_-]+', value):
        raise ValueError(f'invalid identity: {value!r}')
    return value


def rectangle(bounds):
    a, b = bounds[:2]
    values = (min(a[0], b[0]), max(a[0], b[0]), min(a[1], b[1]), max(a[1], b[1]))
    if not all(type(v) in (float, int) and math.isfinite(v) for v in values):
        raise ValueError('non-finite bounds')
    if values[0] >= values[1] or values[2] >= values[3]:
        raise ValueError('empty bounds')
    return values


def rotated_rectangle(bounds, rotation):
    x0, x1, z0, z1 = rectangle(bounds)
    theta = math.radians(rotation)
    c, s = math.cos(theta), math.sin(theta)
    corners = [(x*c-z*s, x*s+z*c) for x in (x0, x1) for z in (z0, z1)]
    return (min(p[0] for p in corners), max(p[0] for p in corners),
            min(p[1] for p in corners), max(p[1] for p in corners))


def project(x, z, bounds, rotation, width, height):
    x0, x1, z0, z1 = rotated_rectangle(bounds, rotation)
    theta = math.radians(rotation)
    rx, rz = x*math.cos(theta)-z*math.sin(theta), x*math.sin(theta)+z*math.cos(theta)
    return ((rx-x0)/(x1-x0)*width, (z1-rz)/(z1-z0)*height)


class Sources:
    """缓存是来源账本；离线时逐个核对字节，不悄悄联网。
    Cache is a source ledger; offline verifies bytes and never accesses the network.
    """
    def __init__(self, directory, offline=False):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.offline = offline
        self.used = {}
        self.transport = threading.local()

    def get(self, url, optional=False, seed=None):
        key = digest(url.encode())
        path, meta = self.directory / key, self.directory / (key + '.json')
        if meta.exists():
            record = json.loads(meta.read_bytes())
            if record['url'] != url:
                raise ValueError('cache URL mismatch')
            if record['status'] == 404:
                if not optional:
                    raise ValueError('required source unavailable: ' + url)
                self.used[url] = record
                return None
            raw = (self.directory / record['cachePath']).read_bytes()
            if digest(raw) != record['sha256']:
                raise ValueError('source hash mismatch: ' + url)
        else:
            if seed is not None:
                raw = seed
            elif self.offline:
                raise ValueError('offline source missing: ' + url)
            else:
                for attempt in range(3):
                    try:
                        if not hasattr(self.transport, 'session'):
                            self.transport.session = requests.Session()
                        with self.transport.session.get(url, headers={'User-Agent': 'Noven-map-assets/1.0'},
                                                        timeout=30, stream=True) as response:
                            if response.status_code == 404 and optional:
                                raw = None
                                break
                            response.raise_for_status()
                            stream = io.BytesIO()
                            for chunk in response.iter_content(64 * 1024):
                                stream.write(chunk)
                                if stream.tell() > 32 * 1024 * 1024:
                                    raise ValueError('source exceeds 32 MiB: ' + url)
                            raw = stream.getvalue()
                        if len(raw) > 32 * 1024 * 1024:
                            raise ValueError('source exceeds 32 MiB: ' + url)
                        break
                    except requests.HTTPError as error:
                        if error.response.status_code not in (429, 500, 502, 503, 504) or attempt == 2:
                            raise
                    except (requests.ConnectionError, requests.Timeout, requests.exceptions.ChunkedEncodingError):
                        if attempt == 2:
                            raise
                    time.sleep(attempt + 1)
            record = {'url': url, 'status': 200 if raw is not None else 404,
                      'sha256': digest(raw) if raw is not None else None,
                      'cachePath': key if raw is not None else None}
            if raw is not None:
                path.write_bytes(raw)
            meta.write_bytes(json_bytes(record))
        self.used[url] = record
        return raw

    def pin(self, url, raw):
        key = digest(url.encode())
        content = key + '.' + digest(raw)
        (self.directory / content).write_bytes(raw)
        record = {'url': url, 'status': 200, 'sha256': digest(raw), 'cachePath': content,
                  'pinnedSnapshot': True}
        (self.directory / (key + '.json')).write_bytes(json_bytes(record))
        self.used[url] = record
        return raw


def references(layout):
    result = {}
    for record in layout:
        for config in record['maps']:
            if config.get('projection') != 'interactive':
                continue
            if not config.get('svgPath') and not config.get('tilePath'):
                continue
            for slug in [record['normalizedName'], *config.get('altMaps', [])]:
                if slug in result:
                    raise ValueError('ambiguous DEV reference: ' + slug)
                result[identity(slug)] = (identity(record['normalizedName']), config)
    return result


def floors(config):
    base = config.get('svgLayer', 'Base')
    result = [(base, {'name': 'Ground Level', 'svgLayer': config.get('svgLayer'),
                      'tilePath': config.get('tilePath'), 'extents': []})]
    for layer in config.get('layers', []):
        # 同底图的显式 base 别名不生成重复楼层。 Do not duplicate an explicit base alias.
        if not config.get('svgLayer') and layer.get('tilePath') == config.get('tilePath'):
            result[0] = (base, layer)
            continue
        floor = layer.get('svgLayer') or re.sub(r'[^A-Za-z0-9_-]+', '_', layer['name']).strip('_')
        result.append((identity(floor), layer))
    if len({floor for floor, _ in result}) != len(result):
        raise ValueError('duplicate floor identity')
    if config.get('key') == 'interchange':
        accepted = {'Ground_Level': 'B1', 'First_Floor': '1F', 'Second_Floor': '2F'}
        result = [(floor, {**layer, 'name': accepted[floor]}) for floor, layer in result]
    return result


def extent_rows(map_id, config, floor_list):
    rows = []
    # overlay → extent → rectangle；不能按显示 order 排序。
    # Preserve upstream overlay/extent/rectangle priority, independently of display order.
    for floor, layer in floor_list[1:]:
        for extent in layer.get('extents', []):
            bottom, top = extent['height']
            if not math.isfinite(bottom) or not math.isfinite(top) or bottom >= top:
                raise ValueError('invalid height interval')
            for bounds in extent.get('bounds', [config['bounds']]):
                rows.append((map_id, floor, bottom, top, *rectangle(bounds)))
    return rows


def svg_variant(raw, config, layer, overlay=False):
    root = ET.fromstring(raw)
    view = [float(v) for v in root.attrib['viewBox'].replace(',', ' ').split()]
    if len(view) != 4 or view[2] <= 0 or view[3] <= 0:
        raise ValueError('invalid SVG viewBox')
    selected = layer.get('svgLayer')
    groups = [g for g in root if g.tag.endswith('}g')]
    if selected and selected not in [g.attrib.get('id') for g in groups]:
        raise ValueError('DEV SVG layer absent: ' + selected)
    keep = {selected} if overlay else {config.get('svgLayer'), selected}
    for group in groups:
        if group.attrib.get('id') not in keep and group.attrib.get('data-keep-with-group') not in keep:
            root.remove(group)
    # 将 SVG 的世界覆盖范围映射到主 bounds，保留原 viewBox 内的仿射变换。
    # Map SVG world coverage into the main bounds, retaining its internal viewBox transform.
    target = rotated_rectangle(config['bounds'], config['coordinateRotation'])
    source = rotated_rectangle(config.get('svgBounds', config['bounds']), config['coordinateRotation'])
    x0, x1, z0, z1 = target
    sx0, sx1, sz0, sz1 = source
    crop = (view[0] + (x0-sx0)/(sx1-sx0)*view[2],
            view[1] + (sz1-z1)/(sz1-sz0)*view[3],
            (x1-x0)/(sx1-sx0)*view[2], (z1-z0)/(sz1-sz0)*view[3])
    root.set('viewBox', ' '.join(format(v, '.15g') for v in crop))
    root.set('preserveAspectRatio', 'none')
    root.attrib.pop('width', None)
    root.attrib.pop('height', None)
    return ET.tostring(root, encoding='unicode'), view, crop


def png(image):
    stream = io.BytesIO()
    image.save(stream, format='PNG')
    return stream.getvalue()


def source_path(record):
    suffix = '.png' if record['url'].split('?')[0].endswith('.png') else '.source'
    return 'maps/sources/' + record['cachePath'] + suffix


def materialize_sources(output, sources):
    plan = []
    for url, record in sorted(sources.used.items()):
        if record['status'] != 200:
            continue
        relative = source_path(record)
        target = Path(output) / relative
        raw = (sources.directory / record['cachePath']).read_bytes()
        if digest(raw) != record['sha256']:
            raise ValueError('source hash mismatch: ' + url)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            target.write_bytes(raw)
        elif digest(target.read_bytes()) != record['sha256']:
            raise ValueError('staged source hash mismatch: ' + relative)
        # 图标仍按独立 manifest 固定版本；启动更新只处理有重拼配方的地图瓦片。
        # Icons remain pinned by their own manifest; startup updates cover composable map tiles only.
        if relative.endswith('.png') and url.startswith('https://assets.tarkov.dev/maps/'):
            plan.append((relative, url, ''))
    return plan


def composition_rows(output_path, metadata, sources, output_size):
    result = []
    for order, layer in enumerate(metadata['compositionLayers']):
        for entry in layer['inputs']:
            path = entry.get('inputPath')
            if not path:
                path = source_path(sources.used[entry['url']])
            tile_size = layer['tileSize']
            tile_x = entry['position'][0] // tile_size + layer['tileOrigin'][0]
            tile_y = entry['position'][1] // tile_size + layer['tileOrigin'][1]
            result.append((output_path, path, *output_size, tile_size, tile_x, tile_y,
                           *metadata['pixelBounds'], 'replace' if order == 0 else 'over'))
    return result


def overlay_tiles(output, canonical, floor, overlay, base_metadata, urls, derived):
    base = base_metadata['compositionLayers'][0]
    left, top, width, height = base['crop']
    sx, sy = overlay.width/width, overlay.height/height
    atlas = overlay.transform(tuple(base['atlasSize']), Image.Transform.AFFINE,
                              (sx, 0, -left*sx, 0, sy, -top*sy), Image.Resampling.BICUBIC)
    inputs = []
    tile_size = base['tileSize']
    for y in range(0, atlas.height, tile_size):
        for x in range(0, atlas.width, tile_size):
            tile = atlas.crop((x, y, x+tile_size, y+tile_size))
            if tile.getchannel('A').getbbox() is None:
                continue
            relative = f'maps/{canonical}/composition_sources/{floor}/{x//tile_size}_{y//tile_size}.png'
            path = output / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            raw = png(tile)
            path.write_bytes(raw)
            derived[relative] = {'sha256': digest(raw), 'sourceUrls': urls}
            inputs.append({'inputPath': relative, 'position': [x, y], 'size': [tile_size, tile_size]})
    return {**base, 'inputs': inputs}


def raster_svg(svg, size):
    import resvg_py
    root = ET.fromstring(svg)
    root.set('width', str(size[0]))
    root.set('height', str(size[1]))
    raw = resvg_py.svg_to_bytes(svg_string=ET.tostring(root, encoding='unicode'),
                              width=size[0], height=size[1], skip_system_fonts=True)
    return Image.open(io.BytesIO(raw)).convert('RGBA')


def tile_geometry(config, detail_width):
    x0, x1, z0, z1 = rotated_rectangle(config['bounds'], config['coordinateRotation'])
    a, b, c, d = config['transform']
    if a <= 0 or c <= 0:
        raise ValueError('unsupported tile transform')
    zoom = min(config['maxZoom'], max(config['minZoom'], math.ceil(math.log2(detail_width/((x1-x0)*a)))))
    scale = 2**zoom
    return zoom, (scale*(x0*a+b), scale*(-z1*c+d), scale*(x1*a+b), scale*(-z0*c+d))


def satellite(sources, config, template, size, overlay=False, workers=12):
    zoom, bounds = tile_geometry(config, size[0])
    tile_size = config.get('tileSize', 256)
    x0, y0 = (math.floor(v/tile_size) for v in bounds[:2])
    x1, y1 = (math.ceil(v/tile_size)-1 for v in bounds[2:])
    pairs = [(x, y) for y in range(y0, y1+1) for x in range(x0, x1+1)]
    print(f'  Source tiles: {len(pairs)} at zoom {zoom}: {template}', flush=True)

    def fetch(pair):
        x, y = pair
        url = template.format(z=zoom, x=x, y=y)
        return x, y, url, sources.get(url, optional=True)

    with ThreadPoolExecutor(max_workers=workers) as pool:
        tiles = list(pool.map(fetch, pairs))
    atlas = Image.new('RGBA', ((x1-x0+1)*tile_size, (y1-y0+1)*tile_size))
    gaps, present, inputs = [], 0, []
    for x, y, url, raw in tiles:
        if raw is None:
            gaps.append(url)
            if not overlay and x not in (x0, x1) and y not in (y0, y1):
                raise SourceUnavailable('interior base tile unavailable: ' + url)
            continue
        with Image.open(io.BytesIO(raw)) as tile:
            if tile.width != tile.height or tile.width > 512:
                raise ValueError('unsupported source PNG dimensions: ' + url)
            source_size = tile.size
            logical = tile.convert('RGBA')
            if logical.size != (tile_size, tile_size):
                logical = logical.resize((tile_size, tile_size), Image.Resampling.BICUBIC)
            atlas.paste(logical, ((x-x0)*tile_size, (y-y0)*tile_size))
        present += 1
        inputs.append({'url': url, 'size': [tile_size, tile_size], 'encodedSize': list(source_size),
                       'position': [(x-x0)*tile_size, (y-y0)*tile_size]})
    if not present:
        raise SourceUnavailable('all floor tiles unavailable: ' + template)
    # subpixel crop + resampling；卫星与 SVG 必须覆盖同一投影矩形。
    # Subpixel crop/resampling; satellite and SVG cover the same projection rectangle.
    left, top, right, bottom = bounds
    image = atlas.transform(size, Image.Transform.AFFINE,
                            ((right-left)/size[0], 0, left-x0*tile_size,
                             0, (bottom-top)/size[1], top-y0*tile_size), Image.Resampling.BICUBIC)
    return image, {'zoom': zoom, 'pixelBounds': bounds, 'missingTiles': gaps,
                   'sourceUrls': [url for _, _, url, _ in tiles], 'nativeTileSize': tile_size,
                   'compositionLayers': [{'inputs': inputs, 'atlasSize': list(atlas.size),
                                           'crop': [left-x0*tile_size, top-y0*tile_size, right-left, bottom-top],
                                           'tileSize': tile_size, 'tileOrigin': [x0, y0]}]}


def available_satellite(sources, config, template, size, overlay=False, workers=12):
    # 高层级源瓦片不完整时使用真实的较低完整层级，不把缺块当作透明底图。
    # Fall back to a real complete lower source level, never transparent holes in a basemap.
    requested, _ = tile_geometry(config, size[0])
    unavailable = []
    for zoom in range(requested, config['minZoom'] - 1, -1):
        try:
            image, metadata = satellite(sources, {**config, 'minZoom': zoom, 'maxZoom': zoom},
                                        template, size, overlay, workers)
            if unavailable:
                metadata['unavailableHigherLevels'] = unavailable
            return image, metadata
        except SourceUnavailable as error:
            unavailable.append({'zoom': zoom, 'reason': str(error)})
    raise SourceUnavailable(str(unavailable))


def write_tsv(path, header, rows):
    def cell(value):
        value = format(value, '.15g') if isinstance(value, float) else str(value)
        return value.replace('\\', '\\\\').replace('\t', '\\t').replace('\r', '\\r').replace('\n', '\\n')
    text = header + '\n' + ''.join('\t'.join(cell(v) for v in row) + '\n' for row in rows)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode('utf-8'))


def emit_image(output, relative, image, preview, source_urls, derived):
    print(f'  Packing {relative}: {image.width}x{image.height}', flush=True)
    target = output / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    small = image.copy()
    small.thumbnail((preview, preview), Image.Resampling.LANCZOS)
    for path, raw in ((target, png(small)), (target.with_suffix('.tiles'), tile_pack(png(image)))):
        path.write_bytes(raw)
        derived[path.relative_to(output).as_posix()] = {'sha256': digest(raw), 'sourceUrls': sorted(set(source_urls))}
    return relative


def reuse_map(output, sources, canonical, config, floor_list, size, preview, derived, recipes):
    staged = []
    for floor, layer in floor_list:
        path = output / f'maps/{canonical}/{floor}.manifest.json'
        if not path.exists():
            return None
        meta = json.loads(path.read_bytes())
        if meta['floorId'] != floor or meta['name'] != layer['name']:
            return None
        abstract = f'maps/{canonical}/{floor}.png' if meta['abstract'] else ''
        sat = f'maps/{canonical}/{floor}.satellite.png' if meta['satellite'] else ''
        if not staged and config.get('tilePath') and meta.get('satelliteUnavailable'):
            return None
        if not staged and not config.get('svgPath') and any(
                entry.get('tilePath') == config.get('tilePath') for entry in config.get('layers', [])):
            if not meta['satellite'] or not meta['satellite'].get('sparseFloorBase'):
                return None
        if meta['abstract'] and meta['abstract'].get('kind') == 'tile-fallback':
            abstract = sat
        for relative in set(filter(None, (abstract, sat))):
            if not (output / relative).exists() or not (output / relative).with_suffix('.tiles').exists():
                return None
            with Image.open(output / relative) as image:
                if max(image.size) > preview:
                    return None
            header = (output / relative).with_suffix('.tiles').read_bytes()[:24]
            if header[:8] != b'NVTILES1' or struct.unpack_from('<2I', header, 8) != size:
                return None
        staged.append((floor, layer, meta, abstract, sat))
    assets = []
    for index, (floor, layer, meta, abstract, sat) in enumerate(staged):
        if abstract and abstract != sat:
            urls = sorted({LAYOUT_URL, config['svgPath'], SVG_RIGHTS_URL})
            for suffix in ('.png', '.tiles'):
                relative = str(Path(abstract).with_suffix(suffix)).replace('\\', '/')
                derived[relative] = {'sha256': digest((output / relative).read_bytes()), 'sourceUrls': urls}
        if sat:
            sat_meta = meta['satellite']
            zoom = sat_meta['zoom']
            if not config['minZoom'] <= zoom <= config['maxZoom']:
                raise ValueError('staged source zoom differs: ' + canonical)
            expected = tile_geometry({**config, 'minZoom': zoom, 'maxZoom': zoom}, size[0])[1]
            if not all(math.isclose(a, b, abs_tol=1e-8) for a, b in zip(sat_meta['pixelBounds'], expected)):
                raise ValueError('staged projection differs: ' + canonical)
            for url in sat_meta['sourceUrls']:
                sources.get(url, optional=True)
            for component in sat_meta['compositionLayers']:
                for entry in component['inputs']:
                    if entry.get('inputPath'):
                        relative = entry['inputPath']
                        derived[relative] = {'sha256': digest((output / relative).read_bytes()),
                                             'sourceUrls': [LAYOUT_URL, config['svgPath'], SVG_RIGHTS_URL]}
            urls = sorted(set([LAYOUT_URL, *sat_meta['sourceUrls']]))
            for suffix in ('.png', '.tiles'):
                relative = str(Path(sat).with_suffix(suffix)).replace('\\', '/')
                derived[relative] = {'sha256': digest((output / relative).read_bytes()), 'sourceUrls': urls}
            recipes.extend(composition_rows(sat, sat_meta, sources, size))
        rank = max((e['height'][0] for e in layer.get('extents', [])),
                   default=config.get('heightRange', config.get('_heightRange', [0]))[0] if index == 0 else 0)
        assets.append((floor, layer, abstract, sat, rank))
    print('Reusing validated staged images ' + canonical, flush=True)
    return assets


def image_size(canonical, width, height, detail_width):
    # 保留已验收立交桥的8K矢量细节；其他地图有界生成，长边不超 native 限额。
    # Preserve accepted Interchange 8K vector detail; bound other maps to the native long-edge limit.
    pixels = max(8192, detail_width) if canonical == 'interchange' and detail_width >= 4096 else detail_width
    result = (pixels, max(1, round(pixels * height / width)))
    if max(result) > 8192:
        ratio = 8192 / max(result)
        result = tuple(max(1, round(value * ratio)) for value in result)
    return result


def build(output, sources, api, layout, detail_width=4096, preview=1024, only=None, workers=12, reuse_existing=False):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    refs = references(layout)
    rows = {name: [] for name in HEADERS}
    derived, statuses, generated = {}, [], {}
    for map_record in sorted(api['data']['maps'].values(), key=lambda m: m['id']):
        map_id, slug = identity(map_record['id']), identity(map_record['normalizedName'])
        status = {'mapId': map_id, 'slug': slug}
        statuses.append(status)
        if slug not in refs:
            status['status'] = 'missing_metadata'
            continue
        canonical, config = refs[slug]
        if only and canonical not in only:
            status['status'] = 'not_selected'
            continue
        floor_list = floors(config)
        if canonical not in generated:
            print('Generating ' + canonical, flush=True)
            raw_svg = sources.get(config['svgPath']) if config.get('svgPath') else None
            view = None
            if raw_svg:
                _, view, _ = svg_variant(raw_svg, config, floor_list[0][1])
            if canonical == 'interchange':
                if view != [0, 0, 1127.6852, 947.02582] or config['bounds'] != [[598, -442], [-433, 426]] or config['coordinateRotation'] != 180:
                    raise ValueError('accepted Interchange projection changed')
                width, height = 1127.6852, 947.02582
            else:
                x0, x1, z0, z1 = rotated_rectangle(config['bounds'], config['coordinateRotation'])
                width, height = x1-x0, z1-z0
            size = image_size(canonical, width, height, detail_width)
            floor_assets = []
            if reuse_existing:
                floor_assets = reuse_map(output, sources, canonical, config, floor_list, size, preview,
                                          derived, rows['map_compositions.tsv']) or []
            base_satellite = None
            base_sat_meta = None
            for index, (floor, layer) in enumerate(floor_list):
                if len(floor_assets) == len(floor_list):
                    break
                urls = [LAYOUT_URL]
                abstract_path = satellite_path = ''
                metadata = {'floorId': floor, 'name': layer['name'], 'abstract': None, 'satellite': None}
                abstract_image = None
                if raw_svg and layer.get('svgLayer'):
                    svg, _, crop = svg_variant(raw_svg, config, layer)
                    abstract_image = raster_svg(svg, size)
                    abstract_path = emit_image(output, f'maps/{canonical}/{floor}.png', abstract_image, preview,
                                               [*urls, config['svgPath'], SVG_RIGHTS_URL], derived)
                    metadata['abstract'] = {'kind': 'svg', 'source': config['svgPath'], 'croppedViewBox': crop,
                                            'author': config.get('author'), 'authorLink': config.get('authorLink'),
                                            'rights': {'declaredLicense': 'CC BY-NC-SA 4.0', 'source': SVG_RIGHTS_URL,
                                                       'additionalTerms': 'See cached upstream README restrictions'}}
                sat_image, sat_meta = None, None
                if layer.get('tilePath'):
                    try:
                        # DEV 把破冰船等独立楼层同时指定为 base；它仍是可稀疏的楼层图，不是完整底图。
                        # DEV may designate a standalone floor as base; it remains a sparse floor image, not a full basemap.
                        sparse_base = index == 0 and not raw_svg and any(
                            entry.get('tilePath') == config.get('tilePath') for entry in config.get('layers', []))
                        sat_image, sat_meta = available_satellite(sources, config, layer['tilePath'], size,
                                                       overlay=index>0 or sparse_base, workers=workers)
                        if sparse_base:
                            sat_meta['sparseFloorBase'] = True
                    except SourceUnavailable as error:
                        metadata['satelliteUnavailable'] = str(error)
                        print(f'Unavailable satellite {canonical}/{floor}: {error}', flush=True)
                    if index == 0:
                        base_satellite, base_sat_meta = sat_image, sat_meta
                    elif base_satellite is not None and sat_image is not None:
                        sat_image = Image.alpha_composite(base_satellite, sat_image)
                        sat_meta['sourceUrls'] += base_sat_meta['sourceUrls']
                        sat_meta['compositionLayers'] = base_sat_meta['compositionLayers'] + sat_meta['compositionLayers']
                elif base_satellite is not None and raw_svg and layer.get('svgLayer'):
                    svg, _, _ = svg_variant(raw_svg, config, layer, overlay=True)
                    overlay = raster_svg(svg, size)
                    sat_image = Image.alpha_composite(base_satellite, overlay)
                    overlay_layer = overlay_tiles(output, canonical, floor, overlay, base_sat_meta,
                                                   [LAYOUT_URL, config['svgPath'], SVG_RIGHTS_URL], derived)
                    sat_meta = {**base_sat_meta, 'sourceUrls': [*base_sat_meta['sourceUrls'], config['svgPath']],
                                'composition': 'base satellite + SVG floor overlay',
                                'compositionLayers': [*base_sat_meta['compositionLayers'], overlay_layer]}
                if sat_image is not None:
                    if config.get('svgPath') in sat_meta['sourceUrls']:
                        sat_meta['sourceUrls'].append(SVG_RIGHTS_URL)
                    satellite_path = emit_image(output, f'maps/{canonical}/{floor}.satellite.png', sat_image, preview,
                                                [*urls, *sat_meta['sourceUrls']], derived)
                    rows['map_compositions.tsv'].extend(composition_rows(satellite_path, sat_meta, sources, size))
                    metadata['satellite'] = {**sat_meta, 'layoutAuthor': config.get('author'), 'layoutAuthorLink': config.get('authorLink'),
                                             'imageryRightsHolder': 'not specified in DEV layout',
                                             'rights': 'Tile imagery rights not specified by maps.json; not assumed to share SVG license'}
                if not abstract_path and satellite_path:
                    abstract_path = satellite_path
                    metadata['abstract'] = {'kind': 'tile-fallback', 'sameAsSatellite': True}
                # 缺失真实 floor 图像保留空路径，不能假装底图代表该楼层。
                # Missing real floor imagery keeps an empty path, never a fabricated base-floor substitute.
                rank = max((e['height'][0] for e in layer.get('extents', [])),
                           default=config.get('heightRange', config.get('_heightRange', [0]))[0] if index == 0 else 0)
                floor_assets.append((floor, layer, abstract_path, satellite_path, rank))
                (output / f'maps/{canonical}').mkdir(parents=True, exist_ok=True)
                (output / f'maps/{canonical}/{floor}.manifest.json').write_bytes(json_bytes(metadata))
            ranks = {entry[0]: order for order, entry in enumerate(sorted(floor_assets, key=lambda f: -f[4]))}
            generated[canonical] = (width, height, floor_assets, ranks)
        width, height, assets, ranks = generated[canonical]
        rows['map_references.tsv'].append((map_id, slug, floor_list[0][0], width, height,
                                            config['coordinateRotation'], *rectangle(config['bounds']), config.get('author', '')))
        for floor, layer, abstract, sat, _ in assets:
            name = layer['name']
            if canonical == 'interchange':
                label = {'Ground_Level': 'B1', 'First_Floor': '1F', 'Second_Floor': '2F'}[floor]
                zh, en = label, label
            else:
                zh, en = ZH.get(name, name), name
            rows['map_floors.tsv'].append((map_id, floor, zh, en, ranks[floor], abstract, sat))
        rows['map_extents.tsv'].extend(extent_rows(map_id, config, floor_list))
        missing_floors = [floor for floor, _, abstract, sat, _ in assets if not abstract and not sat]
        status.update(status='missing_images' if not assets[0][2] and not assets[0][3] else 'ready',
                      canonical=canonical, author=config.get('author'), authorLink=config.get('authorLink'),
                      missingFloors=missing_floors, satelliteFloors=[f for f, _, _, sat, _ in assets if sat])
        for name in ('map_references.tsv', 'map_floors.tsv', 'map_extents.tsv', 'map_compositions.tsv'):
            write_tsv(output / 'data' / name, HEADERS[name], rows[name])
        rows['map_update_assets.tsv'] = materialize_sources(output, sources)
        write_tsv(output / 'data/map_update_assets.tsv', HEADERS['map_update_assets.tsv'], rows['map_update_assets.tsv'])
    # 只有真实来源 URL 入更新表；本地转换产物由 source→derived manifest 跟踪。
    # Only actual source URLs enter the updater; local conversions are tracked by provenance.
    rows['map_update_assets.tsv'] = materialize_sources(output, sources)
    for name, values in rows.items():
        write_tsv(output / 'data' / name, HEADERS[name], values)
    import importlib.metadata
    manifest = {'schema': 1, 'layoutSource': LAYOUT_URL, 'apiSource': API_URL,
                'detailWidth': detail_width, 'previewBound': preview,
                'renderer': 'resvg-py; system fonts disabled; Pillow',
                'pillowVersion': Image.__version__,
                'resvgVersion': importlib.metadata.version('resvg-py'),
                'maps': statuses, 'sources': [sources.used[url] for url in sorted(sources.used)], 'derived': derived,
                'extentsPolicy': 'upstream overlay/extent/rectangle sequence; y half-open; x/z inclusive; base fallback',
                'rights': 'Layout, SVG, tile imagery and icons have separate rights; no common license asserted'}
    (output / 'maps' / 'all.manifest.json').parent.mkdir(parents=True, exist_ok=True)
    (output / 'maps' / 'all.manifest.json').write_bytes(json_bytes(manifest))
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/maps-all-assets')
    parser.add_argument('--cache', type=Path, default=ROOT / 'build/map-all-sources')
    parser.add_argument('--offline', action='store_true')
    parser.add_argument('--width', type=int, choices=(4096, 8192), default=4096)
    parser.add_argument('--preview', type=int, choices=(1024, 2048), default=1024)
    parser.add_argument('--maps', nargs='+', help='Canonical DEV slugs; omitted means every API map')
    parser.add_argument('--workers', type=int, default=12)
    parser.add_argument('--reuse-existing', action='store_true', help='Resume completed staged maps after validating projection, dimensions and source hashes')
    args = parser.parse_args()
    if args.workers < 1:
        parser.error('--workers must be positive')
    if args.output.resolve() == (ROOT / 'assets').resolve():
        parser.error('generate to build/maps-all-assets first; production publication is a separate review step')
    sources = Sources(args.cache, args.offline)
    layout = json.loads(sources.get(LAYOUT_URL))
    api_path = ROOT / 'build/map-api-sources/regular_maps.json'
    api = json.loads(sources.pin(API_URL, api_path.read_bytes()))
    svg_rights = sources.get(SVG_RIGHTS_URL).decode('utf-8')
    if 'creativecommons.org/licenses/by-nc-sa/4.0/' not in svg_rights:
        raise ValueError('upstream SVG rights changed; review cached README')
    if api.get('errors'):
        raise ValueError('DEV API errors')
    # 复用现有图标契约，缓存原始 PNG 并核对已验收转换图的来源。
    # Reuse existing icon contract, caching raw PNG and verifying accepted conversion provenance.
    icon_manifest = json.loads((ROOT / 'assets/maps/icons/manifest.json').read_bytes())
    icon_outputs = {}
    for name, record in sorted(icon_manifest['images'].items()):
        raw = sources.get(record['url'])
        if digest(raw) != record['sourceSha256']:
            raise ValueError('pinned icon source changed: ' + name)
        image = (ROOT / 'assets/maps/icons' / (name + '.png')).read_bytes()
        if digest(image) != record['pngSha256']:
            raise ValueError('accepted icon output changed: ' + name)
        target = args.output / 'maps/icons' / (name + '.png')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(image)
        icon_outputs[target.relative_to(args.output).as_posix()] = {'sha256': digest(image), 'sourceUrls': [record['url']]}
    manifest = build(args.output, sources, api, layout, args.width, args.preview, args.maps, args.workers, args.reuse_existing)
    manifest['derived'].update(icon_outputs)
    manifest['iconsRights'] = {'source': 'assets/maps/icons/manifest.json', 'rights': 'See existing icon SOURCE.md/LICENSE.tarkov-dev.txt'}
    (args.output / 'maps/all.manifest.json').write_bytes(json_bytes(manifest))
    print(json.dumps({'maps': manifest['maps'], 'sourceCount': len(sources.used), 'derivedCount': len(manifest['derived'])}, ensure_ascii=False))


if __name__ == '__main__':
    main()
