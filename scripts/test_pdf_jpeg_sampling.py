#!/usr/bin/env python3
"""Check JPEG sampling grids and restart intervals with authored constant blocks."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_jpeg_decoder import SOI, EOI, QT, frame, scan, segment
from test_pdf_images import fixture


def block_value(component, x, y):
    return ((component * 7 + x * 3 + y * 5) % 25) - 12


def encoded(sampling, width, height, progressive, restart, separate=False,
            quantizers=None, distinct_tables=False):
    factors = [(v >> 4, v & 15) for v in sampling]
    maximum_x = max(h for h, v in factors)
    maximum_y = max(v for h, v in factors)
    counts = bytes((0, 0, 0, 12)) + bytes(12)
    huffman = segment(0xc4, b'\0' + counts + bytes(range(12)) + b'\x10\x01' + bytes(15) + b'\0')
    header = bytearray(frame(len(sampling), progressive, width, height, sampling=sampling))
    if distinct_tables:
        for component in range(len(sampling)):
            header[12 + component * 3] = component
    data = SOI + QT + header + huffman
    data += segment(0xee, b'Adobe\x00\x64\x00\x00\x00\x00\x00')
    if restart:
        data += segment(0xdd, struct.pack('>H', restart))

    def flush(bits):
        bits += '1' * (-len(bits) % 8)
        return bytes(int(bits[i:i+8], 2) for i in range(0, len(bits), 8)).replace(b'\xff', b'\xff\0')

    def units(group):
        if len(group) == 1:
            component = group[0]; h, v = factors[component]
            bx = (width * h + maximum_x * 8 - 1) // (maximum_x * 8)
            by = (height * v + maximum_y * 8 - 1) // (maximum_y * 8)
            return [[(component, x, y)] for y in range(by) for x in range(bx)]
        result = []
        for my in range((height + maximum_y * 8 - 1) // (maximum_y * 8)):
            for mx in range((width + maximum_x * 8 - 1) // (maximum_x * 8)):
                result.append([(c, mx * factors[c][0] + x, my * factors[c][1] + y)
                               for c in group for y in range(factors[c][1]) for x in range(factors[c][0])])
        return result

    def encode_scan(group, ac=False):
        output = scan(tuple(c + 1 for c in group), ss=1 if ac else 0,
                      se=63 if ac or not progressive else 0)
        pending = ''; predictors = [0] * len(sampling); marker = 0
        scan_units = units(group)
        for index, unit in enumerate(scan_units):
            for component, bx, by in unit:
                if ac:
                    pending += '0'
                    continue
                value = block_value(component, bx, by)
                difference = value - predictors[component]; predictors[component] = value
                size = abs(difference).bit_length(); pending += f'{size:04b}'
                if size:
                    amplitude = difference if difference >= 0 else difference + (1 << size) - 1
                    pending += f'{amplitude:0{size}b}'
                if not progressive:
                    pending += '0'
            if restart and (index + 1) % restart == 0 and index + 1 < len(scan_units):
                output += flush(pending) + bytes((0xff, 0xd0 + marker % 8))
                pending = ''; predictors = [0] * len(sampling); marker += 1
        return output + flush(pending)

    if separate:
        groups = [(component,) for component in range(len(sampling))] if separate is True else separate
        for group in groups:
            if quantizers:
                assert distinct_tables or len(group) == 1
                for component in group:
                    slot = component if distinct_tables else 0
                    data += segment(0xdb, bytes([slot]) + bytes([quantizers[component]]) * 64)
            data += encode_scan(group)
            if progressive:
                for component in group:
                    data += encode_scan((component,), ac=True)
    else:
        data += encode_scan(tuple(range(len(sampling))))
        if progressive:
            for component in range(len(sampling)):
                data += encode_scan((component,), ac=True)
    data += EOI
    expected = bytes(128 + block_value(c, (x * h // maximum_x) // 8,
                                      (y * v // maximum_y) // 8) * (quantizers[c] // 8 if quantizers else 1)
                     for y in range(height) for x in range(width) for c, (h, v) in enumerate(factors))
    return data, expected


def sample_cases():
    layouts = [(0x41, 0x11, 0x11), (0x14, 0x11, 0x11), (0x31, 0x11, 0x11),
               (0x13, 0x11, 0x11), (0x22, 0x11, 0x11), (0x41, 0x12, 0x22),
               (0x32, 0x21, 0x11), (0x11, 0x31, 0x13), (0x44,),
               (0x41, 0x11), (0x41, 0x11, 0x11, 0x11)]
    for sampling in layouts:
        for width, height in ((8, 8), (33, 17), (65, 9), (129, 9)):
            for progressive in (False, True):
                for restart in (0, 2):
                    yield sampling, width, height, progressive, restart, False, None, False
    for sampling in ((0x44, 0x11, 0x11), (0x44, 0x44, 0x44), (0x44, 0x44, 0x44, 0x44)):
        for width, height in ((8, 8), (33, 17)):
            for restart in (0, 2):
                for quantizers, distinct in ((None, False), (tuple(8 * (i + 1) for i in range(len(sampling))), False),
                                             (tuple(8 * (i + 1) for i in range(len(sampling))), True)):
                    for progressive in (False, True):
                        yield sampling, width, height, progressive, restart, True, quantizers, distinct
    for groups in (((0, 1), (2,)), ((0,), (1, 2)), ((2,), (0, 1))):
        for width, height in ((33, 17), (129, 9)):
            for restart in (0, 2):
                for progressive in (False, True):
                    yield (0x41, 0x11, 0x11), width, height, progressive, restart, groups, None, False


def main():
    native = str(Path(sys.argv[1]).resolve())
    positional = [arg for arg in sys.argv[2:] if not arg.startswith('--')]
    pdf = str(Path(positional[0]).resolve()) if positional else None
    oom = str(Path(positional[1]).resolve()) if len(positional) > 1 else None
    checks = pdf_checks = references = allocations = 0
    if '--reference' in sys.argv:
        from PIL import Image
    with tempfile.TemporaryDirectory(prefix='cupidimage-jpeg-sampling-') as tmp:
        path = Path(tmp) / 'case.jpg'
        for label in sample_cases():
            sampling, width, height, progressive, restart, separate, quantizers, distinct = label
            components = len(sampling)
            data, expected = encoded(*label)
            path.write_bytes(data)
            result = subprocess.run([native, str(path), str(width), str(height), str(components), '0'],
                                    capture_output=True, timeout=30)
            assert result.returncode == 0 and result.stdout == expected, (label, result.stderr)
            checks += 1
            factors = [(v >> 4, v & 15) for v in sampling]
            hx, vy = max(h for h, v in factors), max(v for h, v in factors)
            reference_grid = all(hx % h == 0 and vy % v == 0 and
                                 hx // h != 2 and vy // v != 2 for h, v in factors)
            if '--reference' in sys.argv and components in (1, 3) and reference_grid:
                with Image.open(path) as reference:
                    assert reference.size == (width, height)
                    assert reference.tobytes() == expected, label
                references += 1
            if pdf and components != 2:
                space = {1: 'DeviceGray', 3: 'DeviceRGB', 4: 'DeviceCMYK'}[components]
                attrs = f'/Width {width} /Height {height} /BitsPerComponent 8 /ColorSpace /{space}'
                outputs = []
                for payload, extra in ((expected, ''), (data, ' /Filter /DCTDecode')):
                    page = Path(tmp) / 'case.pdf'; page.write_bytes(fixture(payload, attrs + extra))
                    rendered = subprocess.run([pdf, str(page), '0'], capture_output=True, timeout=30)
                    assert rendered.returncode == 0, (label, rendered.stderr)
                    outputs.append(rendered.stdout)
                assert outputs[0] == outputs[1], label
                pdf_checks += 1
                if oom:
                    faults = subprocess.run([oom, str(page), '0'], capture_output=True, text=True, timeout=60)
                    assert faults.returncode == 0, (label, faults.stdout, faults.stderr)
                    allocations += int(faults.stdout.split(': ')[1].split()[0])
        for sampling in ((0x01, 0x11, 0x11), (0x10, 0x11, 0x11),
                         (0x51, 0x11, 0x11), (0x44, 0x11, 0x11)):
            for progressive in (False, True):
                data, _ = encoded(sampling, 8, 8, progressive, 0)
                path.write_bytes(data)
                result = subprocess.run([native, str(path), '8', '8', '3', '0'], capture_output=True, timeout=30)
                assert result.returncode == 3, (sampling, progressive, result.stderr)
                checks += 1
        for groups in (((0,), (1,)), ((0,), (1,), (0,), (2,))):
            data, _ = encoded((0x41, 0x11, 0x11), 33, 17, False, 0, groups)
            path.write_bytes(data)
            result = subprocess.run([native, str(path), '33', '17', '3', '0'], capture_output=True, timeout=30)
            assert result.returncode == 3, (groups, result.stderr)
            checks += 1
    print(f'JPEG sampling layouts: {checks} native and {pdf_checks} PDF checks; {references} independent matches; {allocations} allocation checks')


if __name__ == '__main__':
    main()
