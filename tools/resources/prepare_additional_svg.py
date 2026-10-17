"""Pinned Lab/Labyrinth SVG audit and NVR1 preparation; offline, never publishes."""
import argparse
import csv
import hashlib
import json
import math
import shutil
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from package_map import package, reject_reparse

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/maps_generator'))
from generate_all_images import project, raster_svg, png
from generate_images import tile_pack

REF = '4944764f5f6c42d152dca6bd1b5371c4f6212a9e'
SHEBUKA_REF = '5a8b6115d1c0cf56f2ebaac1a96fa5ae3074d178'
CASES = (
    dict(id='5b0fc42d86f7744a585f9105', slug='the-lab', file='Labs.svg', config='Labs.jsonc',
         sha='099a9eaabc305431d906151e03c45d6f06bb885ec46234735e93cc2ab02a222a',
         configSha='d7929df2f686a01dcd7c13a608c9cc98f8c4e0866a668994828df7ddeeb342ad',
         author='Shebuka', license='Shebuka-LICENSE.md',
         url=f'https://raw.githubusercontent.com/the-hideout/tarkov-dev-svg-maps/{SHEBUKA_REF}/Labs.svg',
         floors={'Technical': 'Technical_Level', 'Base': 'First_Level', 'Second_Level': 'Second_Level'}),
    dict(id='6733700029c367a3d40b02af', slug='the-labyrinth', file='Labyrinth.svg', config='Labyrinth.jsonc',
         sha='6b9448b2c51a61f52d68fcd11de806d0c93653c37f77cb7d5f53103233822c94',
         configSha='6633fd2ea1806b8d274c59af22cf88d4d99288f34f111e787b8578c428458841',
         author='acidphantasm', license='acidphantasm-LICENSE.md',
         url=f'https://raw.githubusercontent.com/acidphantasm/SPT-DynamicMaps/{REF}/Plugin/Resources/Maps/Labyrinth/Layers/Labyrinth.svg',
         floors={'Base': None}),
)


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def rows(assets, name):
    with (assets / 'data' / name).open(encoding='utf-8-sig') as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def inspect_svg(raw):
    root = ET.fromstring(raw)
    for node in root.iter():
        if node.tag.split('}')[-1].lower() in ('image', 'script', 'foreignobject'):
            raise ValueError('embedded raster/active content')
        for key, value in node.attrib.items():
            if key.split('}')[-1].lower().startswith('on') or (key.endswith('href') and not value.startswith('#')):
                raise ValueError('external reference/event handler')
        if node.tag.split('}')[-1] == 'style' and node.text and ('@import' in node.text or 'http' in node.text):
            raise ValueError('external stylesheet')
    return root


def convert(raw, source, target, floor, size):
    root = inspect_svg(raw)
    view = [float(n) for n in root.attrib['viewBox'].split()]
    if len(view) != 4 or min(view[2:]) <= 0:
        raise ValueError('invalid viewBox')
    bounds = [[source['Min']['x'], source['Min']['y']], [source['Max']['x'], source['Max']['y']]]
    target_bounds = [[float(target['minX']), float(target['minZ'])], [float(target['maxX']), float(target['maxZ'])]]
    # SVG 像素与世界坐标的仿射对应；源/目标旋转不同也不改变客户端投影。
    # Fit the image-to-world affine mapping, preserving the existing native target projection.
    rotation = source['rotation']
    points = [(bounds[0][0], bounds[0][1]), (bounds[1][0], bounds[0][1]), (bounds[0][0], bounds[1][1])]
    src = [project(x, z, bounds, rotation, view[2], view[3]) for x, z in points]
    dst = [project(x, z, target_bounds, float(target['rotation']), *size) for x, z in points]
    def solve(values):
        x0, y0 = src[0]
        x1, y1 = src[1][0] - x0, src[1][1] - y0
        x2, y2 = src[2][0] - x0, src[2][1] - y0
        determinant = x1*y2-x2*y1
        if abs(determinant) < 1e-9:
            raise ValueError('degenerate projection')
        a = ((values[1]-values[0])*y2-(values[2]-values[0])*y1)/determinant
        b = (x1*(values[2]-values[0])-x2*(values[1]-values[0]))/determinant
        return a, b, values[0]-a*(x0+view[0])-b*(y0+view[1])
    ax = solve([x for x, y in dst])
    ay = solve([y for x, y in dst])
    if floor:
        groups = [g for g in root if g.tag.endswith('}g')]
        if floor not in [g.get('id') for g in groups]:
            raise ValueError('missing SVG floor')
        for g in groups:
            if g.get('id') != floor:
                root.remove(g)
    wrapper = ET.Element('{http://www.w3.org/2000/svg}g', transform='matrix(' + ' '.join(format(v, '.15g') for v in (ax[0], ay[0], ax[1], ay[1], ax[2], ay[2])) + ')')
    for child in list(root):
        root.remove(child)
        wrapper.append(child)
    root.append(wrapper)
    root.set('viewBox', f'0 0 {size[0]} {size[1]}')
    root.set('preserveAspectRatio', 'none')
    image = raster_svg(ET.tostring(root, encoding='unicode'), size)
    if image.getchannel('A').getbbox() is None or not any(a != b for a, b in image.convert('RGB').getextrema()):
        raise ValueError('blank render')
    return image


def path_checks(image, target, points):
    """Static landmark-to-vector proximity; not a claim of live game validation."""
    bounds = [[float(target['minX']), float(target['minZ'])], [float(target['maxX']), float(target['maxZ'])]]
    unit_x = (float(target['maxZ'])-float(target['minZ']))/image.width
    unit_y = (float(target['maxX'])-float(target['minX']))/image.height
    radius = math.ceil(1/min(unit_x, unit_y))
    checks = []
    for p in points:
        x, y = project(float(p['x']), float(p['z']), bounds, float(target['rotation']), *image.size)
        best = math.inf
        for yy in range(max(0, int(y)-radius), min(image.height, int(y)+radius+1)):
            for xx in range(max(0, int(x)-radius), min(image.width, int(x)+radius+1)):
                red, green, blue, alpha = image.getpixel((xx, yy))
                if alpha > 240 and min(red, green, blue) > 40:
                    best = min(best, math.hypot((xx-x)*unit_x, (yy-y)*unit_y))
        if best > 1:
            raise ValueError('static marker not aligned with vector paths: '+p['nameEn'])
        checks.append(dict(id=p['id'], kind=p['kind'], distanceToVector=round(best, 6)))
    return checks


def prepare(assets, upstream, output, version):
    for directory in (assets, upstream):
        for parent in (directory.absolute(), *directory.absolute().parents):
            reject_reparse(parent)
    output.mkdir(parents=True, exist_ok=False)
    references = {r['mapId']: r for r in rows(assets, 'map_references.tsv')}
    maps = {r['id']: r for r in rows(assets, 'map_maps.tsv')}
    points = rows(assets, 'map_points.tsv')
    floors = rows(assets, 'map_floors.tsv')
    results, reports = [], []
    for case in CASES:
        source = upstream / case['file']
        reject_reparse(source)
        if sha(source) != case['sha']:
            raise ValueError('pinned source hash mismatch')
        for name, expected_hash in ((case['config'], case['configSha']), (case['license'], '1349a4b6148492b44f629e64eed676612e234fe9a839e4f3b277c1482c8849f1')):
            reject_reparse(upstream / name)
            if sha(upstream / name) != expected_hash:
                raise ValueError('pinned calibration/license hash mismatch')
        config_raw = (upstream / case['config']).read_text(encoding='utf-8')
        # Only the JSON prefix before StaticMarkers is needed; later Labs entries include JSONC comments.
        config = json.loads(config_raw.split('"StaticMarkers"')[0].rstrip().rstrip(',') + '}')
        target = references[case['id']]
        expected = {r['floorId'] for r in floors if r['mapId'] == case['id']}
        if expected != set(case['floors']):
            raise ValueError('native floor contract changed')
        for row in rows(assets, 'pve/map_floors.tsv'):
            if row['mapId'] == case['id'] and row['floorId'] not in expected:
                raise ValueError('uncovered PvE floor')
        b = config['Bounds']
        outside = [dict(name=p['nameEn'], x=p['x'], z=p['z']) for p in points
                   if p['mapId'] == case['id'] and p['kind'] in ('extract', 'transit')
                   and not (b['Min']['x'] <= float(p['x']) <= b['Max']['x'] and b['Min']['y'] <= float(p['z']) <= b['Max']['y'])]
        report = dict(mapId=case['id'], source=case['url'], sourceSha256=case['sha'], sourceRotation=config['CoordinateRotation'],
                      creator=case['author'], sourceConfigSha256=case['configSha'], calibrationRevision=REF,
                      license='CC BY-NC-SA 4.0', sourceBounds=b, target=target, floors=case['floors'], outsideExtracts=outside)
        base = output / case['slug']
        base.mkdir()
        size = (4096, round(4096 * float(target['height']) / float(target['width'])))
        for floor_id, svg_id in case['floors'].items():
            image = convert(source.read_bytes(), {**b, 'rotation': config['CoordinateRotation']}, target, svg_id, size)
            (base / f'{floor_id}.preview.png').write_bytes(png(image.resize((1024, round(1024 * size[1] / size[0])))))
        if outside:
            report['decision'] = 'withheld: source layout does not cover current extraction positions; no satellite fallback'
            reports.append(report)
            continue
        report['staticMarkerChecks'] = path_checks(image.resize((1024, round(1024 * size[1] / size[0]))), target,
            [p for p in points if p['mapId'] == case['id'] and p['kind'] in ('extract', 'switch')])
        report['decision'] = 'eligible: source vector only, transformed into unchanged client projection'
        root = base / 'input'
        dest = root / 'maps' / case['slug']
        dest.mkdir(parents=True)
        for floor_id, svg_id in case['floors'].items():
            image = convert(source.read_bytes(), {**b, 'rotation': config['CoordinateRotation']}, target, svg_id, size)
            row = next(r for r in floors if r['mapId'] == case['id'] and r['floorId'] == floor_id)
            path = root / row['abstractPath']
            path.write_bytes((base / f'{floor_id}.preview.png').read_bytes())
            path.with_suffix('.tiles').write_bytes(tile_pack(png(image)))
        shutil.copyfile(source, dest / 'SOURCE.svg')
        shutil.copyfile(upstream / case['license'], dest / 'LICENSE.txt')
        if case['author'] == 'Shebuka':
            shutil.copyfile(upstream / 'Shebuka-README.md', dest / 'UPSTREAM-README.md')
        (dest / 'SOURCE.md').write_text(f'# {maps[case["id"]]["nameEn"]} (SVG)\n\nAuthor: {case["author"]}. Source: {case["url"]}\nOriginal SHA-256: {case["sha"]}\nLicense: CC BY-NC-SA 4.0 https://creativecommons.org/licenses/by-nc-sa/4.0/; full text in LICENSE.txt.\n\nNoven modifications: affine world-coordinate calibration, PNG previews, NVTILES1 tiling. Original SVG is unchanged. Noven adaptations use CC BY-NC-SA 4.0. Actual use is noncommercial, official features free, cheating prohibited; no endorsement. No satellite images, game screenshots, icons, plugin code or executables included.\n\nOriginal upstream optimized SVG and its co-located asset-specific license were added by acidphantasm in commit 24cac5e5a1c48d010aaa225d86cba2ef3efda1df (Labyrinth only). Calibration metadata: SPT-DynamicMaps commit {REF}, Plugin/Resources/Maps/{"Labyrinth/Labyrinth_TarkovData.jsonc" if case["author"] == "acidphantasm" else "Labs_TarkovDev/Labs_TarkovDev.jsonc"}.\n', encoding='utf-8')
        (dest / 'PROVENANCE.json').write_text(json.dumps(report, sort_keys=True, indent=2), encoding='utf-8')
        inventory = dict(classification='approved', approvalReference=case['url'] + '; co-located CC license; PROJECT_POLICY.md',
                         files={p.relative_to(root).as_posix(): dict(source=case['url'], creator=case['author'], license='CC BY-NC-SA 4.0', sha256=sha(p)) for p in root.rglob('*') if p.is_file()})
        license_url = (f'https://raw.githubusercontent.com/the-hideout/tarkov-dev-svg-maps/{SHEBUKA_REF}/LICENSE.md'
                       if case['author'] == 'Shebuka' else f'https://raw.githubusercontent.com/acidphantasm/SPT-DynamicMaps/{REF}/Plugin/Resources/Maps/Labyrinth/Layers/acidphantasm-LICENSE.md')
        inventory['files'][f'maps/{case["slug"]}/LICENSE.txt'].update(source=license_url, creator='Creative Commons license text')
        inv = base / 'inventory.json'
        inv.write_text(json.dumps(inventory, sort_keys=True), encoding='utf-8')
        result = package(root, inv, base / 'package', case['id'], version, maps[case['id']]['nameZh'] + ' (SVG)', maps[case['id']]['nameEn'] + ' (SVG)')
        results.append(result)
        reports.append(report)
    (output / 'eligibility.json').write_text(json.dumps(reports, ensure_ascii=False, indent=2), encoding='utf-8')
    (output / 'catalog-input.json').write_text(json.dumps(dict(schemaVersion=1, audience='production', components=results), ensure_ascii=False, sort_keys=True), encoding='utf-8')
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('assets', 'upstream', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.assets, args.upstream, args.output, args.version)))
