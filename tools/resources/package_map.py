"""Explicit approved inputs -> deterministic NVR1; never uploads or grants rights."""
import argparse
import csv
import hashlib
import json
import re
import struct
import stat
from pathlib import Path


def safe_name(name):
    if not re.fullmatch(r'maps/[A-Za-z0-9._/-]{1,235}', name):
        return False
    for part in name.split('/'):
        stem = part.split('.')[0].lower()
        if not part or part in ('.', '..') or part.endswith('.') or len(part) > 80:
            return False
        if stem in ('con', 'prn', 'aux', 'nul') or re.fullmatch(r'(com|lpt)[1-9]', stem):
            return False
    return Path(name).suffix.lower() in ('.png', '.tiles', '.json', '.svg', '.md', '.txt')


def reject_reparse(path):
    info = path.lstat()
    if path.is_symlink() or getattr(info, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
        raise ValueError('link/reparse input')


def package(source, inventory_path, output, map_id, version, zh, en, synthetic=False):
    if not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', map_id) or len(map_id) > 64:
        raise ValueError('invalid stable map ID')
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', version):
        raise ValueError('invalid version')
    with (Path(__file__).resolve().parents[2] / 'assets/data/map_maps.tsv').open(encoding='utf-8-sig') as stream:
        if map_id not in {row['id'] for row in csv.DictReader(stream, delimiter='\t')}:
            raise ValueError('unknown MapCatalog identity')
    if any(not title or len(title.encode('utf-8')) > 256 or any(ord(c) < 32 or ord(c) == 127 for c in title) for title in (zh, en)):
        raise ValueError('title bound')
    inventory = json.loads(inventory_path.read_text(encoding='utf-8-sig'))
    if inventory['classification'] != ('original-synthetic' if synthetic else 'approved'):
        raise ValueError('redistribution is not approved')
    if not inventory.get('approvalReference'):
        raise ValueError('missing approval evidence')
    source = source.absolute()
    for parent in (source, *source.parents):
        reject_reparse(parent)
    files = []
    destinations = set()
    for path in sorted(source.rglob('*')):
        reject_reparse(path)
        if not path.is_file():
            continue
        name = path.relative_to(source).as_posix()
        if not safe_name(name):
            raise ValueError('unsafe/non-resource entry: ' + name)
        if name.lower() in destinations:
            raise ValueError('duplicate destination')
        destinations.add(name.lower())
        if not 0 < path.stat().st_size <= 512 * 1024 * 1024:
            raise ValueError('entry size bound')
        files.append((name, path))
    if not 0 < len(files) <= 30000 or set(inventory['files']) != {n for n, _ in files}:
        raise ValueError('every shipped file must have provenance')
    if not any(n.endswith(('LICENSE.txt', 'SOURCE.md')) for n, _ in files):
        raise ValueError('package must carry license/source notice')
    if any(not v.get('source') or not v.get('license') for v in inventory['files'].values()):
        raise ValueError('missing per-file source/license identity')
    identity = 'maps.' + map_id
    artifact = f'{map_id}-{version}.nvr'
    installed = sum(p.stat().st_size for _, p in files)
    if installed > 8 * 1024 ** 3:
        raise ValueError('installed size bound')
    expected_size = 12 + len(identity) + sum(12 + len(name) + path.stat().st_size for name, path in files)
    if expected_size > 2 * 1024 ** 3:
        raise ValueError('package size bound')
    output.mkdir(parents=True, exist_ok=False)
    with (output / artifact).open('xb') as stream:
        stream.write(b'NVR1' + struct.pack('<I', len(identity)) + identity.encode('ascii') + struct.pack('<I', len(files)))
        for name, path in files:
            stream.write(struct.pack('<IQ', len(name), path.stat().st_size) + name.encode('ascii'))
            with path.open('rb') as file:
                while block := file.read(65536):
                    stream.write(block)
    path = output / artifact
    if path.stat().st_size > 2 * 1024 ** 3:
        raise ValueError('package size bound')
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    record = dict(resourceId=identity, stableMapId=map_id, titleZh=zh, titleEn=en, type='map', required=False,
                  version=version, downloadSize=path.stat().st_size, installedSize=installed, sha256=digest, artifact=artifact)
    (output / 'catalog-input.json').write_text(json.dumps(dict(schemaVersion=1, audience='synthetic' if synthetic else 'production', components=[record]), ensure_ascii=False, sort_keys=True), encoding='utf-8')
    (output / 'source-inventory.json').write_text(json.dumps(inventory, ensure_ascii=False, sort_keys=True), encoding='utf-8')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for flag in ('source', 'inventory', 'output'):
        parser.add_argument('--' + flag, type=Path, required=True)
    for flag in ('map-id', 'version', 'title-zh', 'title-en'):
        parser.add_argument('--' + flag, required=True)
    parser.add_argument('--synthetic', action='store_true')
    args = parser.parse_args()
    print(json.dumps(package(args.source, args.inventory, args.output, args.map_id, args.version, args.title_zh, args.title_en, args.synthetic), ensure_ascii=False))
