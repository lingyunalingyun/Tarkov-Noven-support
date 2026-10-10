"""Synthetic NVR1 review packages only; no network or third-party map imagery."""
import argparse
import csv
import hashlib
import json
import struct
import zlib
from pathlib import Path


def png():
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    width = height = 64
    pixels = b''.join(b'\0' + bytes((80, 100, 120)) * width for _ in range(height))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b'')


def create(source, output):
    output.mkdir(parents=True, exist_ok=True)
    records = []
    with (source / 'map_maps.tsv').open(encoding='utf-8-sig') as file:
        maps = list(csv.DictReader(file, delimiter='\t'))[:2]
    with (source / 'map_floors.tsv').open(encoding='utf-8-sig') as file:
        floors = list(csv.DictReader(file, delimiter='\t'))
    image = png()
    for item in maps:
        identity = 'maps.' + item['id']
        names = sorted({row[key] for row in floors if row['mapId'] == item['id'] for key in ('abstractPath', 'satellitePath') if row[key]})
        assert names and all(name.startswith('maps/') and '..' not in name and '\\' not in name for name in names)
        data = b'NVR1' + struct.pack('<I', len(identity)) + identity.encode('ascii') + struct.pack('<I', len(names))
        for name in names:
            encoded = name.encode('ascii')
            data += struct.pack('<IQ', len(encoded), len(image)) + encoded + image
        artifact = item['id'] + '.nvr'
        (output / artifact).write_bytes(data)
        records.append(dict(resourceId=identity, stableMapId=item['id'], type='map', required=False,
                            titleZh=item['nameZh'] + '（合成测试）', titleEn=item['nameEn'] + ' (SYNTHETIC TEST)',
                            version='0.0.1', downloadSize=len(data), installedSize=len(image)*len(names),
                            sha256=hashlib.sha256(data).hexdigest(), artifact=artifact))
    (output / 'manifest.json').write_text(json.dumps(dict(schemaVersion=1, components=records), ensure_ascii=False, sort_keys=True), encoding='utf-8')
    return records


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--data', type=Path, default=Path(__file__).resolve().parents[2] / 'assets/data')
    args = parser.parse_args()
    print(json.dumps(create(args.data, args.output), ensure_ascii=False, indent=2))
