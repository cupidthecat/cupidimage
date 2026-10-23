#!/usr/bin/env python3
"""Check the complete asset inventory, format signatures, and pinned content."""
import hashlib
import json
from pathlib import Path
import struct
import zlib
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SUFFIXES = {'.png': 'png', '.jpg': 'jpeg', '.jpeg': 'jpeg', '.gif': 'gif',
            '.webp': 'webp', '.bmp': 'bmp', '.ico': 'ico', '.cur': 'cur',
            '.tif': 'tiff', '.tiff': 'tiff', '.tga': 'tga', '.pdf': 'pdf', '.svg': 'svg'}


def check_png(data):
    position = 8
    bits = color = None
    saw_end = False
    while position < len(data):
        assert position + 12 <= len(data), 'Truncated PNG chunk'
        length = struct.unpack_from('>I', data, position)[0]
        tag = data[position+4:position+8]
        end = position + 8 + length
        assert end + 4 <= len(data), 'Truncated PNG payload'
        payload = data[position+8:end]
        assert zlib.crc32(tag + payload) == struct.unpack_from('>I', data, end)[0], 'PNG CRC'
        if tag == b'IHDR':
            assert position == 8 and length == 13
            bits, color = payload[8:10]
        if tag == b'PLTE':
            assert length and length % 3 == 0 and length <= 768
            assert color != 3 or length // 3 <= 1 << bits, 'Oversized PNG palette'
        position = end + 4
        if tag == b'IEND':
            assert length == 0 and position == len(data)
            saw_end = True
    assert bits is not None and saw_end


def identify(data, extension):
    if extension == '.tga':
        assert len(data) >= 18 and data[1] in (0, 1), 'TGA header'
        assert data[2] in (0, 1, 2, 3, 9, 10, 11), 'TGA image type'
        assert data[16] in (0, 8, 15, 16, 24, 32), 'TGA pixel depth'
        return 'tga'
    for prefix, fmt in [(b'\x89PNG\r\n\x1a\n', 'png'), (b'\xff\xd8\xff', 'jpeg'),
                        (b'GIF87a', 'gif'), (b'GIF89a', 'gif'), (b'BM', 'bmp'),
                        (b'II*\0', 'tiff'), (b'MM\0*', 'tiff'),
                        (b'\0\0\1\0', 'ico'), (b'\0\0\2\0', 'cur'), (b'%PDF-', 'pdf')]:
        if data.startswith(prefix):
            if fmt == 'png': check_png(data)
            return fmt
    if data[:4] == b'RIFF' and data[8:12] == b'WEBP':
        assert len(data) == struct.unpack_from('<I', data, 4)[0] + 8, 'RIFF length'
        return 'webp'
    if extension == '.svg':
        assert ET.fromstring(data).tag in ('svg', '{http://www.w3.org/2000/svg}svg'), 'SVG root'
        return 'svg'
    return None


def main():
    manifest = ROOT / 'assets/manifest.json'
    rows = json.loads(manifest.read_text())['files']
    actual = {str(p.relative_to(ROOT)) for p in (ROOT / 'assets').rglob('*')
              if p.is_file() and p not in (manifest, ROOT / 'assets/README.md')}
    assert len(rows) == len({r['path'] for r in rows}), 'Duplicate inventory entry'
    assert actual == {r['path'] for r in rows}, 'Inventory missing or stale paths'
    images = negative = 0
    for row in rows:
        path = ROOT / row['path']; data = path.read_bytes()
        assert hashlib.sha256(data).hexdigest() == row['sha256'], f'Changed asset: {path}'
        expected = SUFFIXES.get(path.suffix)
        if row.get('negative_reason'):
            assert 'negative' in path.parts and expected, f'Unmarked negative fixture: {path}'
            negative += 1
        elif expected:
            assert identify(data, path.suffix) == expected, f'Extension/content mismatch: {path}'
            assert path.relative_to(ROOT / 'assets').parts[0] == ('ico' if expected == 'cur' else expected), f'Wrong format folder: {path}'
            images += 1
        else:
            assert path.suffix in ('.txt', '.tex'), f'Unknown asset type: {path}'
            data.decode('utf-8')
    print(f'Asset inventory: {images} image/document signatures, {negative} explicit negative fixtures, {len(rows)} files checked')


if __name__ == '__main__':
    main()
