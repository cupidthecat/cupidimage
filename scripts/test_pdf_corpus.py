#!/usr/bin/env python3
"""Check vendored and cached, hash-pinned real PDF samples."""
import argparse
import hashlib
import json
import pathlib
import struct
import subprocess
import urllib.parse
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('probe', type=pathlib.Path)
parser.add_argument('--cache', type=pathlib.Path, default=pathlib.Path('.cache/pdf-corpus'))
parser.add_argument('--download', action='store_true')
args = parser.parse_args()
manifest = pathlib.Path(__file__).resolve().parents[1] / 'tests/fixtures/pdf-corpus.json'
rows = json.loads(manifest.read_text())['files']
args.cache.mkdir(parents=True, exist_ok=True)
checks = 0
for row in rows:
    name = row['name']
    assert pathlib.Path(name).name == name
    path = manifest.parents[2] / row['asset'] if 'asset' in row else args.cache / name
    if not path.exists():
        if not args.download:
            raise SystemExit(f'Missing {name}; use --download or provide a verified local cache.')
        url = urllib.parse.urlsplit(row['url'])
        assert url.scheme == 'https' and url.hostname == 'raw.githubusercontent.com'
        with urllib.request.urlopen(row['url'], timeout=30) as response:
            data = response.read(1024 * 1024 + 1)
        if len(data) > 1024 * 1024:
            raise SystemExit(f'{name}: corpus size limit exceeded')
        if hashlib.sha256(data).hexdigest() != row['sha256']:
            raise SystemExit(f'{name}: download hash mismatch')
        path.write_bytes(data)
    data = path.read_bytes()
    assert hashlib.sha256(data).hexdigest() == row['sha256'], name
    for page in range(row['pages']):
        result = subprocess.run([str(args.probe.resolve()), str(path), str(page)],
                                capture_output=True, timeout=30)
        assert result.returncode == 0, (name, page, result.stderr)
        assert len(result.stdout) >= 12, name
        width, height, pages = struct.unpack('=III', result.stdout[:12])
        assert pages == row['pages'] and width > 0 and height > 0, name
        assert len(result.stdout) == 12 + width * height * 4, name
        if 'blank_dimensions' in row:
            assert [width, height] == row['blank_dimensions'], name
            assert result.stdout[12:] == b'\xff' * (width * height * 4), name
        checks += 1
print(f'Real PDF corpus: {len(rows)} files, {checks} pages passed (loading; not font/visual parity)')
