#!/usr/bin/env python3
"""Compare native JPEG 2000 samples with equivalent packed PDF images."""
import base64
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

from test_pdf_images import fixture, image_stream
from test_pdf_jpx_containers import jp2, source_values, palette, cmap, cdef, box, colr, signature, ihdr
from test_pdf_icc import profile, gamma
from test_pdf_type3 import stream


def main():
    probe, allocator = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    rows = json.loads(Path('tests/fixtures/pdf-jpx-samples.json').read_text())['cases']
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupid-jpx-images-') as directory:
        path = Path(directory) / 'case.pdf'
        def render(data, reject=False):
            nonlocal checks
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert result.returncode == (3 if reject else 0), (result.returncode, result.stderr)
            checks += 1
            if not reject:
                width, height, pages = struct.unpack('=III', result.stdout[:12])
                assert pages == 1 and len(result.stdout) == 12 + width * height * 4
                return result.stdout
        def fault(data, reject=False):
            nonlocal allocations
            path.write_bytes(data)
            result = subprocess.run([allocator, str(path), str(int(reject))],
                                    capture_output=True, text=True, check=True, timeout=180)
            allocations += int(result.stdout.split('checks')[0].split()[-1])
        for row in rows:
            if row['components'] not in (1, 3, 4):
                continue
            space = {1: 'DeviceGray', 3: 'DeviceRGB', 4: 'DeviceCMYK'}[row['components']]
            bits = row['precision']
            raw = b''.join(v.to_bytes(bits // 8, 'big')
                           for pixel in zip(*source_values(row)) for v in pixel)
            attrs = f"/Width {row['width']} /Height {row['height']} /ColorSpace /{space}"
            for interpolate in ('', ' /Interpolate true'):
                for decode in ('', ' /Decode [' + '1 0 ' * row['components'] + ']'):
                    expected = render(fixture(raw, attrs + f' /BitsPerComponent {bits}' +
                                               interpolate + decode))
                    for payload in (base64.b64decode(row['codestream']), jp2(row)):
                        actual = fixture(payload, attrs + ' /Filter /JPXDecode' + interpolate + decode)
                        assert render(actual) == expected, (row['name'], interpolate, decode)
            if row['name'] in ('RGB-unsigned', 'I;16-signed'):
                fault(actual)
        row = next(r for r in rows if r['name'] == 'RGB-unsigned')
        attrs = f"/Width {row['width']} /Height {row['height']} /ColorSpace /DeviceRGB"
        payload = jp2(row)
        expected = render(fixture(payload, attrs + ' /Filter /JPXDecode'))
        for extra in (' /BitsPerComponent 1', ' /BitsPerComponent null',
                      ' /BitsPerComponent 99', ' /BitsPerComponent /Ignored'):
            assert render(fixture(payload, attrs + ' /Filter /JPXDecode' + extra)) == expected
        for filters, data, objects in (
                ('/JPXDecode', payload, {}),
                ('/JPX#44ecode', payload, {}),
                ('20 0 R', payload, {20: b'21 0 R', 21: b'/JPXDecode'}),
                ('[20 0 R 21 0 R]', zlib.compress(payload),
                 {20: b'/FlateDecode', 21: b'/JPXDecode'}),
                ('[/ASCIIHexDecode /JPXDecode]', payload.hex().encode() + b'>', {})):
            actual = fixture(data, attrs + ' /Filter ' + filters, extra=objects)
            assert render(actual) == expected
        fault(actual)
        # Missing ColorSpace selects the encoded specification; Decode is then ignored.
        geometry = f"/Width {row['width']} /Height {row['height']}"
        for ignored in ('', ' /Decode [1 0 1 0 1 0]', ' /Decode /Ignored'):
            assert render(fixture(payload, geometry + ' /Filter /JPXDecode' + ignored)) == expected
        fault(fixture(payload, geometry + ' /Filter /JPXDecode'))
        # Authored matrix/TRC profiles use the same native ICC pipeline as PDF streams.
        for name in ('RGB-unsigned', 'RGB-signed', 'L-unsigned'):
            icc_row = next(r for r in rows if r['name'] == name)
            count = icc_row['components']
            prof = profile(curves=[gamma(1.8)] * count, gray=count == 1)
            color = box(b'colr', bytes([2, 4, 1]) + prof)
            encoded = signature() + box(b'jp2h', ihdr(icc_row) + color) + box(b'jp2c', base64.b64decode(icc_row['codestream']))
            geometry = f"/Width {icc_row['width']} /Height {icc_row['height']}"
            raw = bytes(v for pixel in zip(*source_values(icc_row)) for v in pixel)
            decode = ''
            if icc_row['signed']:
                decode = ' /Decode [' + ' '.join(f'{-128/255:.17g} {127/255:.17g}' for _ in range(count)) + ']'
            control = fixture(raw, geometry + ' /BitsPerComponent 8 /ColorSpace [/ICCBased 7 0 R]' + decode,
                              extra={7: stream(prof, f'/N {count}')})
            native = fixture(encoded, geometry + ' /Filter /JPXDecode')
            assert render(native) == render(control), name
            fault(native)
            # An unsupported high-priority profile falls through to the next usable box.
            fallback = signature() + box(b'jp2h', ihdr(icc_row) + box(b'colr', bytes([2, 9, 1]) + b'invalid') + color) + box(b'jp2c', base64.b64decode(icc_row['codestream']))
            assert render(fixture(fallback, geometry + ' /Filter /JPXDecode')) == render(native)
        geometry = f"/Width {row['width']} /Height {row['height']}"
        # CIELab code offsets differ from a PDF Lab image's default Decode array.
        for parameters in (None, (100, 0, 220, 100, 180, 110, 0x00443635)):
            fields = parameters or (100, 0, 170, 128, 200, 96, 0x00443530)
            ranges = [(-fields[c * 2 + 1] / 255 * fields[c * 2],
                       (255 - fields[c * 2 + 1]) / 255 * fields[c * 2]) for c in range(3)]
            ep = b'' if parameters is None else struct.pack('>7I', *parameters)
            encoded = signature(True) + box(b'jp2h', ihdr(row) +
                box(b'colr', bytes([1, 0, 1]) + struct.pack('>I', 14) + ep)) + box(b'jp2c', base64.b64decode(row['codestream']))
            white = '.9642 1 .8249' if fields[-1] == 0x00443530 else '.95047 1 1.08883'
            decode = ' '.join(f'{v:.17g}' for pair in ranges for v in pair)
            lab_range = ' '.join(f'{v:.17g}' for pair in ranges[1:] for v in pair)
            raw = bytes(v for pixel in zip(*source_values(row)) for v in pixel)
            control = fixture(raw, geometry + f' /BitsPerComponent 8 /ColorSpace [/Lab << /WhitePoint [{white}] /Range [{lab_range}] >>] /Decode [{decode}]')
            native = fixture(encoded, geometry + ' /Filter /JPXDecode')
            assert render(native) == render(control), fields
            fault(native)
        # JPX is forbidden in inline images even when an outer byte filter gives an extent.
        inline = f'BI /W 3 /H 2 /BPC 8 /CS /RGB /F [/AHx /JPXDecode] ID {payload.hex()}> EI'
        render(fixture(content=inline), True)
        # A separately encoded grayscale soft mask has its own precision and dimensions.
        mask_row = next(r for r in rows if r['name'] == 'L-unsigned')
        mask_attrs = f"/Width {mask_row['width']} /Height {mask_row['height']} /ColorSpace /DeviceGray"
        packed_mask = bytes(source_values(mask_row)[0])
        for interpolate in ('', ' /Interpolate true'):
            control_mask = image_stream(packed_mask, mask_attrs + ' /BitsPerComponent 8' + interpolate)
            native_mask = image_stream(jp2(mask_row), mask_attrs + ' /Filter /JPXDecode' + interpolate)
            control = fixture(payload, attrs + ' /Filter /JPXDecode /SMask 7 0 R', extra={7: control_mask})
            native = fixture(payload, attrs + ' /Filter /JPXDecode /SMask 7 0 R', extra={7: native_mask})
            assert render(native) == render(control)
        fault(native)
        # Embedded opacity replaces Mask; premultiplied samples match zero-matte recovery.
        for name in ('LA-unsigned', 'RGBA-unsigned'):
            alpha_row = next(r for r in rows if r['name'] == name)
            planes = source_values(alpha_row)
            count = alpha_row['components'] - 1
            color = 'DeviceGray' if count == 1 else 'DeviceRGB'
            alpha_attrs = f"/Width {alpha_row['width']} /Height {alpha_row['height']}"
            raw = bytes(v for pixel in zip(*planes[:-1]) for v in pixel)
            opacity = bytes(planes[-1])
            for mode in (0, 1, 2):
                metadata = cdef([(c, 0, c + 1) for c in range(count)] + [(count, 2 if mode == 2 else 1, 0)])
                encoded = jp2(alpha_row, metadata)
                for interpolate in ('', ' /Interpolate true'):
                    parent = alpha_attrs + f' /ColorSpace /{color}' + interpolate
                    mask_attrs = alpha_attrs + ' /ColorSpace /DeviceGray /BitsPerComponent 8' + interpolate
                    if mode == 2:
                        mask_attrs += ' /Matte [' + '0 ' * count + ']'
                    expected = fixture(raw, parent + ' /BitsPerComponent 8' + (' /SMask 7 0 R' if mode else ''),
                                       extra={7: image_stream(opacity, mask_attrs)})
                    native = fixture(encoded, parent + f' /Filter /JPXDecode /SMaskInData {mode}')
                    assert render(native) == render(expected), (name, mode, interpolate)
                    if mode:
                        assert render(fixture(encoded, parent + f' /Filter /JPXDecode /SMaskInData {mode} /Mask [0 255]')) == render(native)
                fault(native)
                if mode:
                    render(fixture(encoded, parent + f' /Filter /JPXDecode /SMaskInData {mode} /SMask 7 0 R',
                                   extra={7: image_stream(opacity, mask_attrs)}), True)
        # Palette values stay at their declared precision, including above 32 bits.
        for precision in (1, 12, 16, 31, 38):
            maximum = (1 << precision) - 1
            entries = [[0, maximum, 0], [maximum, 0, maximum]]
            metadata = palette(entries, [precision - 1] * 3) + cmap([(0, 1, c) for c in range(3)])
            native = jp2(mask_row, metadata)
            values = source_values(mask_row)[0]
            raw = bytes(v for index in values for v in ([0, 255, 0] if index == 0 else [255, 0, 255]))
            assert render(fixture(native, attrs + ' /Filter /JPXDecode')) == render(
                fixture(raw, attrs + ' /BitsPerComponent 8'))
        # A palette may supply the one-bit samples required for a stencil.
        metadata = palette([[0], [1]], [0]) + cmap([(0, 1, 0)])
        stencil = jp2(mask_row, metadata)
        stencil_attrs = f"/Width {mask_row['width']} /Height {mask_row['height']} /ImageMask true"
        paint = '0 0 1 rg 18 0 0 12 0 0 cm /Im0 Do'
        for decode in ('', ' /Decode [1 0]'):
            native = fixture(stencil, stencil_attrs + ' /Filter /JPXDecode' + decode, content=paint)
            control = fixture(bytes([0x60, 0xe0]), stencil_attrs + decode, content=paint)
            assert render(native) == render(control)
        fault(native)
        # BitsPerComponent is not resolved at all for a JPX image.
        assert render(fixture(payload, attrs + ' /Filter /JPXDecode /BitsPerComponent 20 0 R',
                              extra={20: b'20 0 R'})) == render(fixture(payload, attrs + ' /Filter /JPXDecode'))
        for bad_attrs, bad_payload in (
                (attrs.replace('/Width 3', '/Width 4') + ' /Filter /JPXDecode', payload),
                (attrs + ' /Filter [/JPXDecode /FlateDecode]', payload),
                (attrs + ' /Filter /JPXDecode', payload[:-4]),
                (attrs + ' /Filter /JPXDecode /SMaskInData 1', payload)):
            bad = fixture(bad_payload, bad_attrs)
            render(bad, True)
            fault(bad, True)
    print(f'PDF JPEG 2000 images: {checks} page checks and {allocations} allocation checks passed')


if __name__ == '__main__':
    main()
