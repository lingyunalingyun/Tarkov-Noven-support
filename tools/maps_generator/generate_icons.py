"""Generate stable point detail-icon identities from official DEV metadata."""
import argparse
import csv
import hashlib
import json
import urllib.request
from pathlib import Path

# 上游 normalizedName 映射到图标身份；不通过点位名称猜测类型。
# Upstream normalized names map to icon identities, never guessed from point titles.
CONTAINERS = {
    'toolbox': 'toolbox', 'duffle-bag': 'duffle', 'wooden-ammo-box': 'ammo_box',
    'grenade-box': 'grenade_box', 'weapon-box': 'weapon_box', 'wooden-crate': 'wood_crate',
    'plastic-suitcase': 'suitcase', 'dead-scav': 'dead_scav', 'scav-body': 'dead_scav',
    'ground-cache': 'ground_cache', 'buried-barrel-cache': 'barrel_cache', 'pc-block': 'pc',
    'jacket': 'jacket', 'drawer': 'drawer', 'cash-register': 'cash_register',
    'bank-cash-register': 'cash_register', 'medbag': 'medbag', 'medcase': 'medcase',
    'technical-supply-crate': 'technical_crate', 'ration-supply-crate': 'ration_crate',
    'medical-supply-crate': 'medical_crate', 'safe': 'safe', 'bank-safe': 'safe',
    'shturmans-stash': 'stash', 'pmc-body': 'pmc_body', 'civilian-body': 'civilian_body',
    'lab-technician-body': 'lab_body',
}
CATEGORIES = {
    'keycard': 'keycard', 'electronic-key': 'keycard', 'electronic-keys': 'keycard',
    'mechanical-key': 'key', 'mechanical-keys': 'key', 'key': 'key', 'keys': 'key',
    'drink': 'drink', 'drinks': 'drink', 'food': 'food', 'food-and-drinks': 'food',
    'injector': 'injector', 'injectors': 'injector', 'stimulator': 'injector',
    'electronics': 'electronics', 'electronic': 'electronics',
    'valuables': 'valuable', 'jewelry': 'valuable', 'jewelry-and-other': 'valuable',
    'flammable-materials': 'flammable', 'flammable': 'flammable',
    'info': 'intel', 'information-items': 'intel', 'intelligence': 'intel',
    'medical-supplies': 'medical', 'medical': 'medical', 'meds': 'medical', 'medicine': 'medical',
    'energy-elements': 'energy', 'energy': 'energy', 'batteries': 'energy',
    'tools': 'tools', 'tool': 'tools', 'building-materials': 'building',
    'construction': 'building', 'ammo': 'ammo', 'ammunition': 'ammo',
    'bullet': 'ammo', 'rounds': 'ammo', 'weapons': 'weapon', 'weapon': 'weapon',
    'battle-pass-documents': 'battlepass', 'special-equipment': 'special',
}


def ancestors(ids, definitions):
    seen = set()
    for identity in ids:
        while identity and identity not in seen:
            seen.add(identity)
            record = definitions.get(identity, {})
            yield record.get('normalizedName', '')
            identity = record.get('parent')


def item_icon(item, data):
    # 最具体类别先匹配，再沿官方父类别回退；未知类别明确归入 Other。
    # Match specific categories first, then official ancestors; unknown types remain Other.
    for field, definitions in (('categories', 'itemCategories'), ('handbookCategories', 'handbookCategories')):
        for slug in ancestors(item.get(field, []), data.get(definitions, {})):
            if slug in CATEGORIES:
                return CATEGORIES[slug]
    return 'other'


def build(points, maps, items):
    output = ['id\ticons']
    for point in sorted(points, key=lambda p: p['id']):
        icons = set()
        if point['kind'] == 'container':
            slug = maps['lootContainers'].get(point['sourceId'], {}).get('normalizedName', '')
            icons.add(CONTAINERS.get(slug, 'container'))
        elif point['kind'] == 'loose':
            for identity in point['sourceId'].split(','):
                icons.add(item_icon(items.get('items', {}).get(identity, {}), items))
        elif point['kind'] == 'task':
            icons.add('task_item' if point['subtype'] == 'item' else 'task_objective')
        if icons:
            output.append(point['id'] + '\t' + ','.join(sorted(icons)))
    return '\n'.join(output) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=Path('assets/data'))
    args = parser.parse_args()
    sources = {}
    records = {}
    for name in ('maps', 'items'):
        url = 'https://json.tarkov.dev/regular/' + name
        request = urllib.request.Request(url, headers={'User-Agent': 'Noven-map-icons/1.0'})
        with urllib.request.urlopen(request, timeout=60) as response:
            raw = response.read()
        records[name] = json.loads(raw)['data']
        sources[url] = hashlib.sha256(raw).hexdigest()
    with (args.assets / 'map_points.tsv').open(encoding='utf-8', newline='') as file:
        points = list(csv.DictReader(file, delimiter='\t'))
    text = build(points, records['maps'], records['items'])
    (args.assets / 'map_point_icons.tsv').write_text(text, encoding='utf-8', newline='\n')
    meta = {'sourceHashes': sources, 'pointCount': len(text.splitlines()) - 1,
            'pointSourceSha256': hashlib.sha256((args.assets / 'map_points.tsv').read_bytes()).hexdigest(),
            'unknownPolicy': 'unmapped containers remain container; unmapped item types remain other'}
    (args.assets / 'map_point_icons.meta.json').write_text(json.dumps(meta, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps(meta, indent=2))


if __name__ == '__main__':
    main()
