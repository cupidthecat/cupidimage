#!/usr/bin/env python3
"""Compare arithmetic DCT samples and PDF rendering with paired Huffman streams."""
from pathlib import Path
import base64
import io
import json
import subprocess
import sys
import tempfile
import zlib
from test_jpeg_decoder import frame, segment, scan, SOI, EOI, QT
from test_pdf_images import fixture


def main():
    native, pdf, oom = (str(Path(arg).resolve()) for arg in sys.argv[1:4])
    cases = json.loads(Path('tests/fixtures/pdf-dct-arithmetic.json').read_text())['cases']
    native_checks = pdf_checks = references = allocations = 0
    if '--reference' in sys.argv:
        from PIL import Image
    with tempfile.TemporaryDirectory(prefix='cupidimage-pdf-arithmetic-') as directory:
        root = Path(directory)
        path = root / 'image.jpg'
        page = root / 'image.pdf'
        for case in cases:
            width, height, components = (case[k] for k in ('width', 'height', 'components'))
            encoded = [base64.b64decode(case[k]) for k in ('huffman', 'arithmetic')]
            for transform in (0, 1):
                results = []
                for payload in encoded:
                    path.write_bytes(payload)
                    result = subprocess.run([native, str(path), str(width), str(height),
                                             str(components), str(transform)], capture_output=True, timeout=30)
                    assert result.returncode == 0, (case['name'], result.stderr)
                    results.append(result.stdout)
                assert results[0] == results[1], case['name']
                native_checks += 1
            if not case['progressive']:
                path.write_bytes(encoded[0].replace(b'\xff\xc0', b'\xff\xc1', 1))
                extended = subprocess.run([native, str(path), str(width), str(height),
                                           str(components), '1'], capture_output=True, timeout=30)
                assert extended.returncode == 0 and extended.stdout == results[0], case['name']
                native_checks += 1
            if '--reference' in sys.argv:
                images = [Image.open(io.BytesIO(payload)) for payload in encoded]
                assert images[0].tobytes() == images[1].tobytes(), case['name']
                references += 1
            space = {1: 'DeviceGray', 3: 'DeviceRGB', 4: 'DeviceCMYK'}[components]
            attrs = f'/Width {width} /Height {height} /BitsPerComponent 8 /ColorSpace /{space}'
            for chain in (False, True):
                for inverse in (False, True):
                    controls = ' /Decode [' + '1 0 ' * components + ']' if inverse else ''
                    controls += ' /Filter [/FlateDecode /DCTDecode]' if chain else ' /Filter /DCTDecode'
                    outputs = []
                    for payload in encoded:
                        page.write_bytes(fixture(zlib.compress(payload) if chain else payload, attrs + controls))
                        result = subprocess.run([pdf, str(page), '0'], capture_output=True, timeout=30)
                        assert result.returncode == 0, (case['name'], result.stderr)
                        outputs.append(result.stdout)
                    assert outputs[0] == outputs[1], (case['name'], chain, inverse)
                    pdf_checks += 1
                    faults = subprocess.run([oom, str(page), '0'], capture_output=True, text=True, timeout=60)
                    assert faults.returncode == 0, (case['name'], faults.stdout, faults.stderr)
                    allocations += int(faults.stdout.split(': ')[1].split()[0])
        # Reuse one independently encoded grayscale block in separate scans.
        source = base64.b64decode(cases[0]['arithmetic'])
        position = 2
        tables = b''
        while source[position + 1] != 0xda:
            marker = source[position + 1]
            length = int.from_bytes(source[position + 2:position + 4], 'big')
            if marker in (0xdb, 0xcc):
                tables += source[position:position + 2 + length]
            position += 2 + length
        length = int.from_bytes(source[position + 2:position + 4], 'big')
        entropy = source[position + 2 + length:-2]
        path.write_bytes(source)
        gray = subprocess.run([native, str(path), '8', '8', '1', '0'],
                              capture_output=True, timeout=30)
        assert gray.returncode == 0
        for components in (1, 2, 3, 4):
            sof = frame(components).replace(b'\xff\xc0', b'\xff\xc9')
            for selector in range(4):
                # The fixture uses the default DC/AC conditioning values.
                payload = SOI + tables + sof
                for component in range(1, components + 1):
                    payload += scan((component,), selectors=[selector * 17]) + entropy
                payload += EOI
                path.write_bytes(payload)
                result = subprocess.run([native, str(path), '8', '8', str(components), '0'],
                                        capture_output=True, timeout=30)
                expected = bytes(value for value in gray.stdout for _ in range(components))
                assert result.returncode == 0 and result.stdout == expected, (components, selector, result.stderr)
                native_checks += 1
        for parameters in (b'\x00', b'\x04\x10', b'\x20\x10', b'\x00\x01',
                           b'\x10\x00', b'\x10\x40'):
            path.write_bytes(source[:2] + segment(0xcc, parameters) + source[2:])
            result = subprocess.run([native, str(path), '8', '8', '1', '0'],
                                    capture_output=True, timeout=30)
            assert result.returncode == 3, parameters
            native_checks += 1
    print(f'PDF arithmetic DCT: {native_checks} native, {pdf_checks} PDF, '
          f'{references} independent paired comparisons, {allocations} allocation checks')


if __name__ == '__main__':
    main()
