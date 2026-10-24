#!/usr/bin/env python3
"""Audit every image asset, all exposed pages/frames, and terminal output samples."""
import argparse
import base64
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from audit_pdf_corpus import process
from terminal_streams import decode_ansi
from test_sixel import decode as decode_sixel
from test_assets import SUFFIXES

ROOT = Path(__file__).resolve().parents[1]


def references():
    output = {}
    for filename in ('format-reference.json', 'vp8-reference.json', 'jpeg-reference.json', 'pdf-image-reference.json'):
        for row in json.loads((ROOT / 'tests/fixtures' / filename).read_text())['cases']:
            output[row['asset']] = row
    return output


def read_pixels(source):
    header = source.read(28)
    if len(header) != 28: raise ValueError('Truncated probe header')
    width, height, count, hx, hy, delay, loops = struct.unpack('=7I', header)
    size = width * height * 4
    if not width or not height or size > 256 * 1024 * 1024:
        raise ValueError('Invalid probe dimensions')
    data = source.read(size)
    if len(data) != size: raise ValueError('Truncated pixel data')
    return {'width': width, 'height': height, 'count': count, 'hotspot': [hx, hy], 'delay_ms': delay, 'loops': loops}, data


def bundle(path):
    with path.open('rb') as source:
        metadata, rgba = read_pixels(source)
        sample_metadata, sample = read_pixels(source)
        if sample_metadata['width'] > 16 or sample_metadata['height'] > 16:
            raise ValueError('Oversized terminal sample')
        streams = {}
        for mode in ('ansi', 'kitty', 'sixel'):
            header = source.read(4)
            if len(header) != 4: raise ValueError('Truncated stream header')
            length, = struct.unpack('=I', header)
            if length > 65536: raise ValueError('Oversized terminal stream')
            streams[mode] = source.read(length)
            if len(streams[mode]) != length: raise ValueError('Truncated terminal stream')
        if source.read(1): raise ValueError('Trailing probe bytes')
    return metadata, rgba, sample_metadata, sample, streams


def check_reference(row, metadata, actual):
    assert (metadata['width'], metadata['height']) == (row['width'], row['height']), 'Reference dimensions differ'
    assert metadata['hotspot'] == row.get('hotspot', [0, 0]), 'Cursor hotspot differs'
    expected = base64.b64decode(row.get('rgba', row.get('rgba_zlib')), validate=True)
    if 'rgba_zlib' in row: expected = zlib.decompress(expected)
    assert len(actual) == len(expected), 'Reference byte length differs'
    difference = max(abs(a-b) for a,b in zip(actual, expected))
    assert difference <= row.get('tolerance', 0), f'Maximum channel error {difference}'
    return difference


def check_terminal(mode, data, metadata, rgba):
    width, height = metadata['width'], metadata['height']
    rgb = [tuple((rgba[i+c]*rgba[i+3] + 255*(255-rgba[i+3])) // 255 for c in range(3))
           for i in range(0, len(rgba), 4)]
    if mode == 'ansi':
        widths, painted = decode_ansi(data)
        assert widths == [width]*height and painted == rgb, 'ANSI sample differs'
    elif mode == 'sixel':
        transparent, dimensions, painted = decode_sixel(data)
        assert not transparent and dimensions == (width, height), 'Sixel dimensions/transparency differ'
        assert painted == [tuple(c*100//255 for c in pixel) for pixel in rgb], 'Sixel palette sample differs'
    elif mode == 'kitty':
        chunks = re.findall(rb'\x1b_G([^\x1b]*)\x1b\\', data)
        assert chunks and b''.join(b'\x1b_G'+c+b'\x1b\\' for c in chunks)+b'\n' == data
        encoded = bytearray()
        for i, chunk in enumerate(chunks):
            control, payload = chunk.split(b';', 1)
            fields = dict(field.split(b'=', 1) for field in control.split(b','))
            if i == 0: first = fields
            assert fields[b'm'] == (b'0' if i == len(chunks)-1 else b'1')
            assert len(payload) <= 4096 and len(payload) % 4 == 0
            encoded.extend(payload)
        assert first[b's'] == str(width).encode() and first[b'v'] == str(height).encode()
        decoded = base64.b64decode(encoded, validate=True)
        if first.get(b'o') == b'z': decoded = zlib.decompress(decoded)
        assert decoded == rgba, 'Kitty sample differs'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, default=ROOT / 'bin/test_asset_probe')
    parser.add_argument('--report', type=Path, default=ROOT / '.cache/asset-tests/results.jsonl')
    parser.add_argument('--timeout', type=float, default=10)
    parser.add_argument('--memory-mib', type=int, default=1024)
    parser.add_argument('--max-pages', type=int, default=1000)
    parser.add_argument('--require-reference', action='store_true', help='Fail decoded pages without independent pixel expectations')
    parser.add_argument('--diagnostic', action='store_true', help='Write failures without returning a failing exit status')
    args = parser.parse_args()
    if args.timeout <= 0 or args.memory_mib < 0 or args.max_pages < 1: parser.error('Invalid resource limits')
    probe = args.probe.resolve()
    if not probe.is_file(): parser.error('Build bin/test_asset_probe first')
    # Fail before decoding if any asset is missing, changed, or mislabeled.
    subprocess.run([sys.executable, str(ROOT/'scripts/test_assets.py')], check=True)
    refs = references()
    rows = [r for r in json.loads((ROOT/'assets/manifest.json').read_text())['files'] if Path(r['path']).suffix in SUFFIXES]
    counts = Counter(); failures = 0
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with args.report.open('w') as report, tempfile.TemporaryDirectory(prefix='cupidimage-assets-') as temporary:
        directory = Path(temporary)
        snapshot = directory / 'probe'
        shutil.copy2(probe, snapshot)
        probe = snapshot
        def emit(row): report.write(json.dumps(row, sort_keys=True)+'\n'); report.flush()
        emit({'kind': 'run', 'assets': len(rows), 'timeout_seconds': args.timeout,
              'memory_mib': args.memory_mib, 'probe_sha256': hashlib.sha256(probe.read_bytes()).hexdigest()})
        for row in rows:
            path = ROOT/row['path']; fmt = path.relative_to(ROOT/'assets').parts[0]
            command = [str(probe), str(path), fmt]
            negative = bool(row.get('negative_reason'))
            mode = 'pixels' if negative else 'count'
            result, output = process(command+['0', mode], directory, args.timeout, args.memory_mib, 'initial')
            if negative:
                tolerated = row.get('expected_decode') == 'accept'
                ok = result['status'] == ('ok' if tolerated else 'error') and result['returncode'] == (0 if tolerated else 3)
                status = ('tolerated-negative' if tolerated else 'expected-rejection') if ok else 'unexpected-negative-result'
                counts[status] += 1; failures += not ok
                emit({'kind': 'asset', 'file': row['path'], **result, 'status': status, 'reason': row['negative_reason']})
                continue
            if result['status'] != 'ok':
                counts['decode-'+result['status']] += 1; failures += 1
                emit({'kind': 'asset', 'file': row['path'], **result}); continue
            try: count = int(output.read_text())
            except ValueError: count = -1
            if count == 0 and fmt == 'tga' and path.name == 'type0-no-image.tga':
                emit({'kind': 'asset', 'file': row['path'], 'status': 'metadata-only'}); counts['metadata-only'] += 1; continue
            if count < 1 or count > args.max_pages:
                emit({'kind': 'asset', 'file': row['path'], 'status': 'invalid-or-capped-page-count', 'count': count})
                counts['invalid-or-capped-page-count'] += 1; failures += 1; continue
            for page in range(count):
                result, output = process(command+[str(page), 'bundle'], directory, args.timeout, args.memory_mib, 'pixels')
                details = {'kind': 'page', 'file': row['path'], 'page': page+1, **result}
                if result['status'] != 'ok':
                    counts['decode-'+result['status']] += 1; failures += 1; emit(details); continue
                try:
                    metadata, rgba, sample_metadata, sample_pixels, streams = bundle(output); details.update(metadata)
                    details['rgba_sha256'] = hashlib.sha256(rgba).hexdigest()
                    if row['path'] in refs and page == 0:
                        details['max_channel_difference'] = check_reference(refs[row['path']], metadata, rgba)
                        details['status'] = 'reference-pass'
                    else:
                        details['status'] = 'decoded-unverified'
                        if args.require_reference: failures += 1
                    assert sample_metadata['width'] == min(16, metadata['width'])
                    assert sample_metadata['height'] == min(16, metadata['height'])
                    expected_sample = bytearray()
                    for y in range(sample_metadata['height']):
                        for x in range(sample_metadata['width']):
                            sx = x * metadata['width'] // sample_metadata['width']
                            sy = y * metadata['height'] // sample_metadata['height']
                            offset = (sy * metadata['width'] + sx) * 4
                            expected_sample.extend(rgba[offset:offset+4])
                    assert sample_pixels == expected_sample, 'Terminal sample differs from full decoded pixels'
                    del rgba
                    for mode in ('ansi', 'kitty', 'sixel'):
                        check_terminal(mode, streams[mode], sample_metadata, sample_pixels)
                    details['terminal_samples'] = 'ansi/kitty/sixel verified (at most 16x16)'
                    counts['terminal-round-trips'] += 3
                except Exception as error:
                    details['status'] = 'verification-error'; details['error'] = str(error); failures += 1
                counts[details['status']] += 1; emit(details)
            print(row['path'], flush=True)
        emit({'kind': 'summary', 'counts': dict(counts), 'failures': failures})
    print(json.dumps({'counts': dict(counts), 'failures': failures}, sort_keys=True))
    print('Report:', args.report)
    return int(failures > 0 and not args.diagnostic)


if __name__ == '__main__':
    raise SystemExit(main())
