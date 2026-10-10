"""Original synthetic PNGs and text; never copies third-party map content."""
import argparse
import csv
import json
from pathlib import Path
from create_review_fixture import png
from package_map import package


def create(data, output):
    output.mkdir(parents=True, exist_ok=False)
    source = output / 'source'
    source.mkdir()
    with (data / 'map_maps.tsv').open(encoding='utf-8-sig') as stream:
        map_id = next(row['id'] for row in csv.DictReader(stream, delimiter='\t') if row['normalizedName'] == 'factory')
    with (data / 'map_floors.tsv').open(encoding='utf-8-sig') as stream:
        names = sorted({row[key] for row in csv.DictReader(stream, delimiter='\t') if row['mapId'] == map_id for key in ('abstractPath', 'satellitePath') if row[key]})
    assert names
    for name in names:
        path = source / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(png())
    for name, content in {
        'maps/phase14-synthetic/SOURCE.md': b'Original Noven synthetic test image and text. Not an EFT map. No third-party imagery. Only transport review.\n',
        'maps/phase14-synthetic/range-padding.txt': b'Original synthetic HTTP Range test data.\n' * 180000,
    }.items():
        path = source / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        names.append(name)
    inventory = output / 'inventory.json'
    inventory.write_text(json.dumps(dict(classification='original-synthetic', approvalReference='Original Noven synthetic fixture source code; non-production test', files={name: dict(source='tools/resources/create_online_fixture.py', license='Original synthetic test fixture; no third-party content') for name in names}), sort_keys=True), encoding='utf-8')
    return package(source, inventory, output / 'artifacts', map_id, '0.0.1', '工厂（合成网络测试，非真实地图）', 'Factory (SYNTHETIC INTERNET TEST, NOT A REAL MAP)', True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--data', type=Path, default=Path(__file__).resolve().parents[2] / 'assets/data')
    args = parser.parse_args()
    print(json.dumps(create(args.data, args.output), ensure_ascii=False))
