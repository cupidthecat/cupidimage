#!/usr/bin/env python3
"""Verify the pinned JPEG 2000 component sources and recorded adapter patches."""
import hashlib
import json
from pathlib import Path


def main():
    root=Path(__file__).resolve().parents[1]/'third_party/openjpeg'
    manifest=json.loads((root/'manifest.json').read_text())
    assert manifest['revision']=='6c4a29b00211eb0430fa0e5e890f1ce5c80f409f'
    for row in manifest['files']:
        data=(root/row['file']).read_bytes()
        assert hashlib.sha256(data).hexdigest()==row['sha256'],row['file']
        for patch in reversed(row.get('patches',[])):
            before=patch['before'].encode();after=patch['after'].encode()
            assert data.count(after)==1,row['file']
            data=data.replace(after,before,1)
        if 'upstream_sha256' in row:
            assert hashlib.sha256(data).hexdigest()==row['upstream_sha256'],row['file']
    print(f'JPEG 2000 components: {len(manifest["files"])} pinned files and recorded patches verified')


if __name__=='__main__':main()
