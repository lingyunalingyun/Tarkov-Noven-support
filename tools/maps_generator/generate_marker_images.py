"""Bundle official DEV marker/category images for offline native rendering."""
import argparse
import concurrent.futures
import hashlib
import io
import json
from pathlib import Path
import urllib.request
from PIL import Image

DETAIL = {
    'toolbox': 'container_toolbox', 'duffle': 'container_duffle-bag',
    'ammo_box': 'container_wooden-ammo-box', 'grenade_box': 'container_grenade-box',
    'weapon_box': 'container_weapon-box', 'wood_crate': 'container_wooden-crate',
    'suitcase': 'container_plastic-suitcase', 'dead_scav': 'container_dead-scav',
    'ground_cache': 'container_ground-cache', 'barrel_cache': 'container_buried-barrel-cache',
    'pc': 'container_pc-block', 'jacket': 'container_jacket', 'drawer': 'container_drawer',
    'cash_register': 'container_cash-register', 'medbag': 'container_medbag', 'medcase': 'container_medcase',
    'technical_crate': 'container_crate', 'ration_crate': 'container_crate', 'medical_crate': 'container_crate',
    'safe': 'container_safe', 'stash': 'container_weapon-box', 'pmc_body': 'container_dead-scav',
    'civilian_body': 'container_dead-scav', 'lab_body': 'container_dead-scav',
    'task_item': 'quest_item', 'task_objective': 'quest_objective',
}
HANDBOOK = {
    'keycard': 'electronic-keys', 'key': 'mechanical-keys', 'battlepass': 'battle-pass-documents',
    'drink': 'drinks', 'other': 'others', 'electronics': 'electronics', 'valuable': 'valuables',
    'injector': 'injectors', 'flammable': 'flammable-materials', 'intel': 'info-items',
    'medical': 'medical-supplies', 'energy': 'energy-elements', 'food': 'food',
    'building': 'building-materials', 'tools': 'tools', 'ammo': 'rounds', 'weapon': 'weapons',
    'special': 'special-equipment',
}
# 对齐 DEV map-images.mjs 的别名；无专用资源的分类明确使用官方通用图标。
# Follow DEV map-images.mjs aliases; categories without dedicated assets use official generic icons.
CATEGORY = {
    'container': 'container_crate', 'loose_loot': 'loose_loot', 'lock': 'lock', 'switch': 'switch',
    'stationary': 'stationarygun', 'mine': 'hazard', 'artillery': 'hazard_mortar', 'boss': 'spawn_boss',
    'task': 'quest_objective', 'pmc_extract': 'extract_pmc', 'scav_extract': 'extract_scav',
    'coop_extract': 'extract_shared', 'transit': 'extract_transit', 'hidden_extract': 'extract_pmc',
    'sniper': 'hazard', 'spawn': 'spawn_pmc', 'scav_spawn': 'spawn_scav', 'btr': 'btr_stop',
    'easter_egg': 'loose_loot',
}


def image_urls(commit, metadata):
    base = f'https://raw.githubusercontent.com/the-hideout/tarkov-dev/{commit}/public/maps/interactive/'
    result = {f'detail_{key}': base + value + '.png' for key, value in DETAIL.items()}
    result.update({f'category_{key}': base + value + '.png' for key, value in CATEGORY.items()})
    categories = {r['normalizedName']: r for r in metadata['handbookCategories'].values()}
    for key, slug in HANDBOOK.items():
        url = categories[slug]['imageLink']
        if not url or not url.startswith('https://assets.tarkov.dev/'):
            raise ValueError('missing/unexpected official category image')
        result[f'detail_{key}'] = url
    return dict(sorted(result.items()))


def png(raw):
    image = Image.open(io.BytesIO(raw))
    if not 0 < image.width <= 256 or not 0 < image.height <= 256:
        raise ValueError('unexpected marker dimensions')
    image = image.convert('RGBA')
    side = max(image.size)
    square = Image.new('RGBA', (side, side))
    square.paste(image, ((side - image.width) // 2, (side - image.height) // 2))
    out = io.BytesIO()
    square.save(out, format='PNG', compress_level=9)
    return out.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('assets/maps/icons'))
    parser.add_argument('--cache', type=Path, default=Path('build/map-icon-sources'))
    parser.add_argument('--offline', action='store_true')
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)

    def fetch(url, maximum=2 * 1024 * 1024):
        path = args.cache / hashlib.sha256(url.encode()).hexdigest()
        if path.exists():
            return path.read_bytes()
        if args.offline:
            raise ValueError('offline source missing: ' + url)
        request = urllib.request.Request(url, headers={'User-Agent': 'Noven-map-icons/1.0'})
        with urllib.request.urlopen(request, timeout=30) as response:
            raw = response.read(maximum + 1)
        if len(raw) > maximum:
            raise ValueError('source too large')
        path.write_bytes(raw)
        return raw

    if args.offline:
        commit = (args.cache / 'commit.txt').read_text().strip()
    else:
        commit = json.loads(fetch('https://api.github.com/repos/the-hideout/tarkov-dev/commits/main'))['sha']
        (args.cache / 'commit.txt').write_text(commit)
    metadata_url = 'https://json.tarkov.dev/regular/items'
    metadata_raw = fetch(metadata_url, 64 * 1024 * 1024)
    urls = image_urls(commit, json.loads(metadata_raw)['data'])
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        images = dict(zip(urls, pool.map(lambda url: fetch(url), urls.values())))
    license_url = f'https://raw.githubusercontent.com/the-hideout/tarkov-dev/{commit}/LICENSE'
    license_text = fetch(license_url)
    outputs = {key: png(raw) for key, raw in images.items()}
    args.output.mkdir(parents=True, exist_ok=True)
    for key, raw in outputs.items():
        (args.output / (key + '.png')).write_bytes(raw)
    (args.output / 'LICENSE.tarkov-dev.txt').write_bytes(license_text)
    manifest = {'repositoryCommit': commit, 'metadataSource': metadata_url,
                'metadataSha256': hashlib.sha256(metadata_raw).hexdigest(),
                'conversion': 'Pillow RGBA PNG with transparent square padding; no resizing or redrawing',
                'images': {key: {'url': urls[key], 'sourceSha256': hashlib.sha256(images[key]).hexdigest(),
                                 'pngSha256': hashlib.sha256(raw).hexdigest()} for key, raw in outputs.items()}}
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    print(f'{len(outputs)} official image bindings generated')


if __name__ == '__main__':
    main()
