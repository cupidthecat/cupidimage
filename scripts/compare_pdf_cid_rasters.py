#!/usr/bin/env python3
"""Compare complete Japanese glyph rasters without tolerances or image alignment."""
import argparse
import base64
import json
import itertools
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib
from audit_pdf_corpus import read_ppm
from test_pdf_cid_substitution import fixture
from test_pdf_type3 import stream


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--report', type=Path, default=Path('.cache/cid-raster-comparison.json'))
    parser.add_argument('--placement', choices=('all', 'grid', 'fractional'), default='all')
    parser.add_argument('--collection', choices=('all', 'Japan1', 'GB1', 'CNS1', 'Korea1'), default='Japan1')
    args = parser.parse_args()
    if not shutil.which('pdftoppm'):
        parser.error('The independent PDF rasterizer is not installed')
    probe = str(args.probe.resolve())
    rows = []
    environment = dict(os.environ, FREETYPE_PROPERTIES='cff:no-stem-darkening=1 type1:no-stem-darkening=1')
    with tempfile.TemporaryDirectory(prefix='cupidimage-cid-raster-') as tmp:
        path = Path(tmp) / 'page.pdf'
        prefix = Path(tmp) / 'reference'
        placements = ('grid', 'fractional') if args.placement == 'all' else (args.placement,)
        manifest = json.loads(Path('third_party/cid-standard-fonts/manifest.json').read_text())['files']
        for row, placement in itertools.product(manifest, placements):
            if args.collection != 'all' and row['ordering'] != args.collection:
                continue
            serif = row['face'].startswith('mincho')
            name = row['file'].removesuffix('.otf')
            suffix = '' if row['ordering'] == 'Japan1' else '-' + row['face'].split('_', 1)[1]
            oracle = json.loads(Path('tests/fixtures/pdf-cid-substitute-mapping' + suffix + '.json').read_text())
            glyphs = {r[0]: r[1:] for r in struct.iter_unpack(
                '>I4H', zlib.decompress(base64.b64decode(oracle['records_zlib'])))}
            words = {'Japan1': '日本語', 'GB1': '中文人', 'CNS1': '繁體中', 'Korea1': '한국어'}
            codes = ''.join(f'{glyphs[ord(c)][int(serif) * 2]:04x}' for c in words[row['ordering']])
            compressed = zlib.compress((Path('third_party/cid-standard-fonts') / row['file']).read_bytes())
            for size in (12, 18, 24):
                for vertical in (False, True):
                    x, y = 60, 90
                    if placement == 'grid':
                        # Author a distinct input with integer device origins,
                        # accounting for the PDF's default vertical origin.
                        # The fractional input and its failures are retained.
                        x = 60 + (.5 * size if vertical else 0)
                        y = 89 + (.88 * size if vertical else 0)
                    content = f'BT /F {size} Tf {x:.12g} {y:.12g} Td <{codes}> Tj ET'
                    path.write_bytes(fixture(serif=serif, name=name,
                                             ordering='(' + row['ordering'] + ')',
                                             supplement=str(row['supplement']),
                                             descriptor='/FontFile3 9 0 R',
                                             extra={9: stream(compressed, '/Subtype /OpenType /Filter /FlateDecode')},
                                             encoding='/Identity-V' if vertical else '/Identity-H',
                                             content=content))
                    # Match the command-line comparator's unhinted default.
                    # Hinted outlines have separate native/independent tests.
                    result = subprocess.run([probe, str(path), '0', '1', '0', '0', '0', '0'],
                                            capture_output=True, check=True, timeout=30)
                    actual_width, actual_height, _ = struct.unpack('=3I', result.stdout[:12])
                    actual = bytes(v for i, v in enumerate(result.stdout[12:]) if i % 4 != 3)
                    subprocess.run(['pdftoppm', '-r', '96', '-aa', 'yes', '-aaVector', 'no',
                                    '-singlefile', str(path), str(prefix)], env=environment,
                                   capture_output=True, check=True, timeout=30)
                    width, height, expected = read_ppm(prefix.with_suffix('.ppm'))
                    assert (actual_width, actual_height) == (width, height)
                    assert len(actual) == len(expected)
                    differences = [abs(a - b) for a, b in zip(actual, expected)]
                    rows.append({'font': name, 'size': size, 'vertical': vertical,
                                 'placement': placement, 'collection': row['ordering'],
                                 'different_channels': sum(value != 0 for value in differences),
                                 'maximum_channel_difference': max(differences),
                                 'mean_absolute_error': sum(differences) / len(differences)})
    mismatches = sum(row['different_channels'] != 0 for row in rows)
    report = {'requested_profile': '96 DPI, grayscale unhinted text, no stem darkening',
              'exact_matches': len(rows) - mismatches, 'pixel_differences': mismatches, 'pages': rows}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'CID rasters: {len(rows) - mismatches} exact matches; {mismatches} pixel differences')
    print(f'Report: {args.report}')
    return int(mismatches != 0)


if __name__ == '__main__':
    raise SystemExit(main())
