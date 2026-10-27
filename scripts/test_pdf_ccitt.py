#!/usr/bin/env python3
"""Compare fax image rendering with independently encoded, fixed bilevel rasters."""
import base64
import io
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
from test_pdf_images import fixture

FIXTURES = Path(__file__).resolve().parents[1] / 'tests/fixtures/pdf-ccitt.json'


def generate():
    # Fixture generation only: ordinary tests do not need an external codec.
    from PIL import Image
    rng = random.Random(823)
    cases = []
    for mode, k in [('group3', 0), ('group4', -1)]:
        for w, h in [(3, 2), (24, 16), (31, 17), (64, 32), (1729, 2)]:
            for pattern in range(6):
                im = Image.new('1', (w, h))
                pixels = im.load()
                for y in range(h):
                    for x in range(w):
                        pixels[x, y] = 255 if (pattern == 0 or
                            pattern == 2 and x % 2 or pattern == 3 and y % 2 or
                            pattern == 4 and (x+y) % 4 < 2 or
                            pattern == 5 and rng.randrange(2)) else 0
                stream = io.BytesIO()
                im.save(stream, format='TIFF', compression=mode)
                raw = stream.getvalue()
                stream.seek(0)
                with Image.open(stream) as encoded:
                    offsets, sizes = encoded.tag_v2[273], encoded.tag_v2[279]
                    assert len(offsets) == len(sizes) == 1
                cases.append(dict(name=f'{mode}-{w}-{h}-{pattern}', width=w, height=h, k=k,
                    data=base64.b64encode(raw[offsets[0]:offsets[0]+sizes[0]]).decode(),
                    raster=base64.b64encode(im.tobytes()).decode()))
    FIXTURES.write_text(json.dumps(dict(description='Generated bilevel rasters encoded through Pillow TIFF fax compression; no third-party document content.', cases=cases), indent=2)+'\n')


def main():
    if '--generate' in sys.argv:
        generate()
        return
    probe = str(Path(sys.argv[1]).resolve())
    decoder = str(Path(sys.argv[2]).resolve())
    count = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-fax-') as directory:
        for case in json.loads(FIXTURES.read_text())['cases']:
            w, h, k = case['width'], case['height'], case['k']
            raw = base64.b64decode(case['raster'])
            data = base64.b64decode(case['data'])
            for eob in ([False, True] if k < 0 else [False]):
                for black_one in [False, True]:
                    encoded_path = Path(directory) / 'fax.bin'
                    encoded_path.write_bytes(data)
                    decoded = subprocess.run([decoder, str(encoded_path), str(w), str(h),
                        str(k), str(int(eob)), str(int(black_one))],
                        capture_output=True, timeout=15)
                    want = raw if black_one else bytes(255-v for v in raw)
                    assert decoded.returncode == 0 and decoded.stdout == want, (case['name'], 'full raster', eob, black_one)
                    attrs = (f'/Width {w} /Height {h} /BitsPerComponent 1 /ColorSpace /DeviceGray '
                             f'/Filter /CCITTFaxDecode /DecodeParms << /K {k} /Columns {w} '
                             f'/Rows {h} /EndOfBlock {str(eob).lower()} /BlackIs1 {str(black_one).lower()} >>')
                    path = Path(directory) / 'case.pdf'
                    path.write_bytes(fixture(data, attrs))
                    run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=15)
                    assert run.returncode == 0, (case['name'], eob, run.stderr)
                    assert struct.unpack('=III', run.stdout[:12]) == (24, 16, 1)
                    expected = bytearray()
                    for y in range(16):
                        for x in range(24):
                            sx, sy = min(w-1, int((x+.5)*w/24)), min(h-1, int((y+.5)*h/16))
                            v = 255*((raw[sy*((w+7)//8)+sx//8] >> (7-sx%8)) & 1)
                            if not black_one:
                                v = 255-v
                            expected.extend((v, v, v, 255))
                    assert run.stdout[12:] == expected, (case['name'], eob, black_one)
                    count += 1
    print(f'PDF fax images: {count} full-raster and PDF-image comparisons passed')


if __name__ == '__main__':
    main()
