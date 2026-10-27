#!/usr/bin/env python3
"""Exercise LZW width transitions, clear codes, and chained predictors."""
import base64
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_images import fixture
from test_pdf_packed_images import encode
from audit_pdf_corpus import read_ppm


def codes_bytes(codes, early):
    # Pack explicit code sequences, advancing the decoder's table once per
    # noninitial data code. Independent raster comparisons check this schedule.
    width, next_code, previous = 9, 258, False
    buffer = bits = 0
    result = bytearray()
    for code in codes:
        buffer = (buffer << width) | code
        bits += width
        while bits >= 8:
            bits -= 8
            result.append((buffer >> bits) & 255)
        if code == 256:
            width, next_code, previous = 9, 258, False
        elif code != 257:
            if previous and next_code < 4096:
                next_code += 1
                if width < 12 and next_code == (1 << width)-early:
                    width += 1
            previous = True
    if bits:
        result.append((buffer << (8-bits)) & 255)
    return bytes(result)


def literals(data, early):
    return codes_bytes([256, *data, 257], early)


def main():
    probe = str(Path(sys.argv[1]).resolve())
    count = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-lzw-') as directory:
        def check(name, data, attrs, expected, reject=False, reference=True, secondary=False):
            nonlocal count
            count += 1
            path = Path(directory) / (name + '.pdf')
            path.write_bytes(fixture(data, attrs))
            run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=15)
            assert run.returncode == (3 if reject else 0), (name, run.stderr)
            if reject:
                return
            assert struct.unpack('=III', run.stdout[:12]) == (24, 16, 1)
            want = bytes(c for y in range(16) for x in range(24) for c in (*expected(x, y), 255))
            assert run.stdout[12:] == want, (name, 'analytic pixels differ')
            if '--reference' in sys.argv and reference:
                if secondary:
                    import fitz
                    with fitz.open(path) as document:
                        pixmap = document[0].get_pixmap(matrix=fitz.Matrix(96/72, 96/72), alpha=False)
                        w, h, rgb = pixmap.width, pixmap.height, pixmap.samples
                else:
                    prefix = Path(directory) / 'reference' 
                    subprocess.run(['pdftoppm', '-singlefile', '-r', '96', '-aaVector', 'no', str(path), str(prefix)], check=True, capture_output=True, timeout=15)
                    w, h, rgb = read_ppm(prefix.with_suffix('.ppm'))
                actual = bytes(c for i, c in enumerate(run.stdout[12:]) if i % 4 != 3)
                assert (w, h) == (24, 16) and actual == rgb, (name, 'reference differs')

        for early in (0, 1):
            # Both images traverse 9/10/11/12-bit codes and then fill the table.
            data = bytes(4800)
            attrs = f'/Width 24 /Height 200 /BitsPerComponent 8 /ColorSpace /DeviceGray /Filter /LZWDecode /DecodeParms << /EarlyChange {early} >>'
            check(f'width-transitions-{early}', literals(data, early), attrs,
                  lambda x, y: (0,)*3)
            check(f'clear-midstream-{early}', codes_bytes([256, *data[:2400], 256, *data[2400:], 257], early), attrs,
                  lambda x, y: (0,)*3)
            # Code equal to next dictionary index expands previous phrase plus
            # its first byte: A, AA, AAA, AAAA.
            attrs_small = f'/Width 10 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray /Filter /LZWDecode /DecodeParms << /EarlyChange {early} >>'
            check(f'kwkwk-{early}', codes_bytes([256, 65, 258, 259, 260, 257], early), attrs_small, lambda x, y: (65,)*3)
            for codes in ([256, 258, 257], [256, 65, 260, 257], [256, 65]):
                check(f'invalid-{early}-{len(codes)}-{codes[-1]}', codes_bytes(codes, early), attrs_small, None, reject=True)
        for bits in (1, 2, 4, 8, 16):
            maximum = (1 << bits)-1
            rows = [[0, maximum, 0], [maximum, 0, maximum]]
            for predictor in (1, 2, 10, 11, 12, 13, 14, 15):
                encoded = encode(rows, 1, bits, predictor)
                params = f'<< /Predictor {predictor} /Columns 3 /Colors 1 /BitsPerComponent {bits} >>'
                attrs = f'/Width 3 /Height 2 /BitsPerComponent {bits} /ColorSpace /DeviceGray'
                expected = lambda x, y: (255*((y//8+x//8)%2),)*3
                check(f'lzw-{bits}-{predictor}', literals(encoded, 1), attrs + ' /Filter /LZWDecode /DecodeParms '+params, expected,
                      secondary=(bits == 16 and predictor == 2))
                payload = base64.a85encode(zlib.compress(encoded)) + b'~>'
                check(f'chain-{bits}-{predictor}', payload, attrs + ' /Filter [/ASCII85Decode /FlateDecode] /DecodeParms [null '+params+']', expected,
                      secondary=(bits == 16 and predictor == 2))
        colors = [(1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 1, 1), (1, 0, 1), (1, 1, 0)]
        for components, space in ((3, 'DeviceRGB'), (4, 'DeviceCMYK')):
            for bits in (1, 2, 4, 8, 16):
                maximum = (1 << bits)-1
                pixels = colors if components == 3 else [tuple(1-c for c in rgb) + (0,) for rgb in colors]
                values = [v*maximum for pixel in pixels for v in pixel]
                rows = [values[:3*components], values[3*components:]]
                for predictor in (1, 2, 10, 11, 12, 13, 14, 15):
                    encoded = encode(rows, components, bits, predictor)
                    params = f'<< /Predictor {predictor} /Columns 3 /Colors {components} /BitsPerComponent {bits} >>'
                    attrs = f'/Width 3 /Height 2 /BitsPerComponent {bits} /ColorSpace /{space}'
                    expected = lambda x, y: tuple(c*255 for c in colors[(y//8)*3+x//8])
                    # DeviceCMYK uses the library's unmanaged device conversion;
                    # its exact expectations are analytic, not color-managed.
                    for label, payload, filters, decode_params in (
                        ('lzw', literals(encoded, 1), '/LZWDecode', params),
                        ('chain', base64.a85encode(zlib.compress(encoded)) + b'~>', '[/ASCII85Decode /FlateDecode]', '[null '+params+']'),
                    ):
                        check(f'{label}-{space}-{bits}-{predictor}', payload, attrs+' /Filter '+filters+' /DecodeParms '+decode_params,
                              expected, reference=components == 3, secondary=bits == 16 and predictor == 2)
        attrs = '/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray /Filter /LZWDecode'
        for params in ('/EarlyChange 2', '/EarlyChange -1', '/EarlyChange 1.5', '/Predictor 3',
                       '/Predictor 2 /Columns 0', '/Predictor 2 /Colors 2147483647 /Columns 2147483647 /BitsPerComponent 16'):
            check('bad-params-' + str(count), literals(b'\x80', 1), attrs + ' /DecodeParms << '+params+' >>', None, reject=True)
    print(f'PDF LZW and chained predictors: {count} passed')


if __name__ == '__main__':
    main()
