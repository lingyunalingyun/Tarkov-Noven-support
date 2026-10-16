"""Prepare SVG-only NVR1 packages; no network, upload, or rights in other imagery."""
import argparse
import csv
import hashlib
import json
import shutil
import re
import xml.etree.ElementTree as ET
from pathlib import Path
from package_map import package, reject_reparse, safe_name


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def prepare(assets, sources, upstream, output, version, upstream_ref):
    if not re.fullmatch('[0-9a-f]{40}', upstream_ref):
        raise ValueError('explicit upstream notice commit required')
    for root in (assets, sources, upstream):
        for parent in (root.absolute(), *root.absolute().parents):
            reject_reparse(parent)
    ledger = json.loads((assets / 'maps/all.manifest.json').read_text(encoding='utf-8'))
    records = {row['url']: row for row in ledger['sources'] if row.get('status') == 200}
    with (assets / 'data/map_maps.tsv').open(encoding='utf-8-sig') as stream:
        maps = list(csv.DictReader(stream, delimiter='\t'))
    floors = []
    for name in ('map_floors.tsv', 'pve/map_floors.tsv'):
        with (assets / 'data' / name).open(encoding='utf-8-sig') as stream:
            floors.extend(csv.DictReader(stream, delimiter='\t'))
    output.mkdir(parents=True, exist_ok=False)
    results, withheld = [], []
    for map_row in maps:
        selected = {row['abstractPath'] for row in floors if row['mapId'] == map_row['id']}
        metadata = []
        for name in sorted(selected):
            if not safe_name(name):
                raise ValueError('unsafe map image path')
            path = assets / name.replace('.satellite.png', '.png')
            meta = json.loads(path.with_suffix('.manifest.json').read_text(encoding='utf-8'))
            metadata.append((name, meta))
        if not metadata or any(meta['abstract']['kind'] != 'svg' or meta['abstract'].get('author') != 'Shebuka' for _, meta in metadata):
            withheld.append(dict(mapId=map_row['id'], name=map_row['nameEn'], reason='No complete SVG-only floor coverage; exclude satellite/tile-fallback imagery.'))
            continue
        urls = {meta['abstract']['source'] for _, meta in metadata}
        if len(urls) != 1:
            raise ValueError('map source identity is ambiguous')
        url = next(iter(urls))
        source = records[url]
        if not re.fullmatch('[0-9a-f]{64}', source['cachePath']):
            raise ValueError('invalid source cache identity')
        raw = sources / source['cachePath']
        reject_reparse(raw)
        if digest(raw) != source['sha256']:
            raise ValueError('original SVG identity mismatch')
        svg = ET.fromstring(raw.read_bytes())
        for node in svg.iter():
            if node.tag.split('}')[-1].lower() in ('image', 'script', 'foreignobject'):
                raise ValueError('SVG contains independently reviewable embedded content: ' + url)
            for key, value in node.attrib.items():
                if key.split('}')[-1].lower().startswith('on') or (key.split('}')[-1] == 'href' and not value.startswith('#')):
                    raise ValueError('active/external SVG content')
        slug = ledger['maps'][next(i for i, row in enumerate(ledger['maps']) if row['mapId'] == map_row['id'])]['canonical']
        root = output / map_row['id'] / 'input'
        base = root / 'maps' / slug
        base.mkdir(parents=True)
        inventory = dict(classification='approved', approvalReference='PROJECT_POLICY.md and docs/MAP_ATTRIBUTION.md; owner noncommercial/non-cheating policy, published CC BY-NC-SA 4.0 grant', files={})

        def add(name, path, source_url, creator='Shebuka / tarkov-dev-svg-maps'):
            if not safe_name(name):
                raise ValueError('unsafe package destination')
            reject_reparse(path)
            destination = root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
            inventory['files'][name] = dict(source=source_url, license='CC BY-NC-SA 4.0', sha256=digest(destination), creator=creator, modifications='SVG floor selection, affine crop, PNG rasterization and NVTILES1 tiling; notices/original SVG unchanged. No satellite imagery or third-party icons.')

        for name, meta in metadata:
            for image in (name, str(Path(name).with_suffix('.tiles')).replace('\\', '/')):
                expected = ledger['derived'][image]
                if url not in expected['sourceUrls'] or digest(assets / image) != expected['sha256']:
                    raise ValueError('derived content/source identity mismatch: ' + image)
                if image.endswith('.tiles'):
                    with (assets / image).open('rb') as stream:
                        if stream.read(8) != b'NVTILES1':
                            raise ValueError('unsupported runtime tile format')
                add(image, assets / image, url)
        add(f'maps/{slug}/SOURCE.svg', raw, url)
        for name, filename in (('LICENSE.txt', 'LICENSE.md'), ('UPSTREAM-README.md', 'README.md')):
            add(f'maps/{slug}/{name}', upstream / filename, f'https://raw.githubusercontent.com/the-hideout/tarkov-dev-svg-maps/{upstream_ref}/{filename}', 'Creative Commons license text / tarkov-dev-svg-maps notice authors')
        notice = base / 'SOURCE.md'
        notice.write_text(f'# {map_row["nameEn"]} — SVG-only resource\n\nCreator: Shebuka. Upstream: tarkov-dev-svg-maps.\nOriginal URL: {url}\nOriginal SHA-256: {source["sha256"]}\nLicense: CC BY-NC-SA 4.0, https://creativecommons.org/licenses/by-nc-sa/4.0/\nFull license and upstream additional anti-cheating statement: LICENSE.txt and UPSTREAM-README.md.\n\nChanges: select recorded SVG floor groups, affine crop, rasterize previews and NVTILES1 high-resolution tiles. Original SVG is unchanged in SOURCE.svg, retaining supplied earlier notices. Noven copyrightable adaptations, if any, use the same CC BY-NC-SA 4.0. No endorsement or warranty is claimed.\n\nAll official Noven functionality is free; actual NC-material use is noncommercial. No satellite imagery, game screenshots or independently licensed marker/handbook icons are included. Satellite mode may fall back to these SVG views; this package supplies all catalogued SVG floors, not a satellite map.\n', encoding='utf-8')
        inventory['files'][f'maps/{slug}/SOURCE.md'] = dict(source=url, license='CC BY-NC-SA 4.0', sha256=digest(notice), creator='Noven attribution record; original artwork by Shebuka')
        provenance = base / 'PROVENANCE.json'
        provenance.write_text(json.dumps(inventory, ensure_ascii=False, sort_keys=True), encoding='utf-8')
        inventory['files'][f'maps/{slug}/PROVENANCE.json'] = dict(source=url, license='CC BY-NC-SA 4.0', sha256=digest(provenance), creator='Noven source/license inventory')
        inv = root.parent / 'inventory.json'
        inv.write_text(json.dumps(inventory, ensure_ascii=False, sort_keys=True), encoding='utf-8')
        result = package(root, inv, root.parent / 'package', map_row['id'], version, map_row['nameZh'] + ' (SVG)', map_row['nameEn'] + ' (SVG)')
        results.append(result)
    (output / 'catalog-input.json').write_text(json.dumps(dict(schemaVersion=1, audience='production', components=results), ensure_ascii=False, sort_keys=True), encoding='utf-8')
    (output / 'withheld.json').write_text(json.dumps(withheld, ensure_ascii=False, sort_keys=True), encoding='utf-8')
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('assets', 'sources', 'upstream', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--upstream-ref', required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.assets, args.sources, args.upstream, args.output, args.version, args.upstream_ref), ensure_ascii=True))
