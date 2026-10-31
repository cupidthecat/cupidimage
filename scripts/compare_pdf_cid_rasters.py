#!/usr/bin/env python3
"""Compare complete Japanese glyph rasters without tolerances or image alignment."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from audit_pdf_corpus import read_ppm
from test_pdf_cid_substitution import fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--report', type=Path, default=Path('.cache/cid-raster-comparison.json'))
    args = parser.parse_args()
    if not shutil.which('pdftoppm'):
        parser.error('The independent PDF rasterizer is not installed')
    probe = str(args.probe.resolve())
    rows = []
    environment = dict(os.environ, FREETYPE_PROPERTIES='cff:no-stem-darkening=1 type1:no-stem-darkening=1')
    with tempfile.TemporaryDirectory(prefix='cupidimage-cid-raster-') as tmp:
        path = Path(tmp) / 'page.pdf'
        prefix = Path(tmp) / 'reference'
        for serif in (False, True):
            name = 'HaranoAjiMincho-Regular' if serif else 'HaranoAjiGothic-Regular'
            for size in (12, 18, 24):
                for vertical in (False, True):
                    content = f'BT /F {size} Tf 60 90 Td <0cd40e8a07a0> Tj ET'
                    path.write_bytes(fixture(serif=serif, embedded=True, name=name,
                                             encoding='/Identity-V' if vertical else '/Identity-H',
                                             content=content))
                    result = subprocess.run([probe, str(path), '0', '1', '0', '1', '0', '0'],
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
                                 'different_channels': sum(value != 0 for value in differences),
                                 'maximum_channel_difference': max(differences),
                                 'mean_absolute_error': sum(differences) / len(differences)})
    mismatches = sum(row['different_channels'] != 0 for row in rows)
    report = {'requested_profile': '96 DPI, grayscale text, native CFF hints, no stem darkening',
              'exact_matches': len(rows) - mismatches, 'pixel_differences': mismatches, 'pages': rows}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Japanese CID rasters: {len(rows) - mismatches} exact matches; {mismatches} pixel differences')
    print(f'Report: {args.report}')
    return int(mismatches != 0)


if __name__ == '__main__':
    raise SystemExit(main())
