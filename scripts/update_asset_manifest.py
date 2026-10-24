#!/usr/bin/env python3
"""Refresh hashes after reviewing asset changes; never called by the tests."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
manifest = root / 'assets/manifest.json'
previous = {row['path']: row for row in json.loads(manifest.read_text())['files']}
rows = []
for path in sorted((root / 'assets').rglob('*')):
    if not path.is_file() or path in (manifest, root / 'assets/README.md'):
        continue
    name = str(path.relative_to(root))
    row = {'path': name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    if 'negative' in path.relative_to(root).parts:
        reason = previous.get(name, {}).get('negative_reason')
        if not reason:
            raise SystemExit(f'Add a reviewed negative_reason to the manifest first: {name}')
        row['negative_reason'] = reason
        if previous[name].get('expected_decode'):
            row['expected_decode'] = previous[name]['expected_decode']
    rows.append(row)
manifest.write_text(json.dumps({'files': rows}, indent=2) + '\n')
print(f'Updated {len(rows)} asset hashes; run make test-assets to verify formats')
