#!/usr/bin/env python3
"""Verify indirect and null image metadata against equivalent direct dictionaries."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_images import fixture, image_stream, RGB

ATTRS = '/Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB'
DEFAULTS = {20: b'null', 21: b'20 0 R', 22: b'999 0 R'}


def cases():
    baseline = fixture()
    for key in ('ImageMask', 'Decode', 'SMask', 'Mask', 'Type'):
        for value in ('null', '20 0 R', '21 0 R', '22 0 R', '999 0 R'):
            if key == 'Type':
                body = image_stream(RGB, ATTRS).replace(b'/Type /XObject', f'/Type {value}'.encode())
                changed = fixture(extra={**DEFAULTS, 5: body})
            else:
                changed = fixture(attrs=ATTRS + f' /{key} {value}', extra=DEFAULTS)
            yield f'null-{key}-{value}', baseline, changed
    for key, value in (('Width', '2'), ('Height', '2'), ('BitsPerComponent', '8'),
                       ('ColorSpace', '/DeviceRGB'), ('ImageMask', 'false'),
                       ('Decode', '[0 1 0 1 0 1]'), ('Type', '/XObject'), ('Subtype', '/Image')):
        for indirect in ('20 0 R', '21 0 R'):
            attrs = ATTRS
            if key not in ('Type', 'Subtype'):
                if f'/{key} {value}' in attrs:
                    attrs = attrs.replace(f'/{key} {value}', f'/{key} {indirect}')
                else:
                    attrs += f' /{key} {indirect}'
            changed = fixture(attrs=attrs, extra={20: value.encode(), 21: b'20 0 R'})
            if key in ('Type', 'Subtype'):
                body = image_stream(RGB, attrs).replace(f'/{key} {value}'.encode(), f'/{key} {indirect}'.encode())
                changed = fixture(extra={5: body, 20: value.encode(), 21: b'20 0 R'})
            yield f'indirect-{key}-{indirect}', baseline, changed
    for value in ('[20 0 R 21 0 R 20 0 R 21 0 R 20 0 R 21 0 R]', '22 0 R'):
        objects = {20: b'1', 21: b'0', 22: b'[20 0 R 21 0 R 20 0 R 21 0 R 20 0 R 21 0 R]'}
        yield 'decode-members-' + value, fixture(attrs=ATTRS + ' /Decode [1 0 1 0 1 0]'), fixture(
            attrs=ATTRS + ' /Decode ' + value, extra=objects)
    key_mask = '[255 255 0 0 0 0]'
    for value in ('20 0 R', '[21 0 R 21 0 R 22 0 R 22 0 R 22 0 R 22 0 R]'):
        yield 'indirect-color-key-' + value, fixture(attrs=ATTRS + ' /Mask ' + key_mask), fixture(
            attrs=ATTRS + ' /Mask ' + value, extra={20: key_mask.encode(), 21: b'255', 22: b'0'})
    stencil_attrs = '/Width 2 /Height 2 /ImageMask true'
    stencil = fixture(b'\x40\x80', stencil_attrs)
    yield 'indirect-stencil-flag', stencil, fixture(
        b'\x40\x80', stencil_attrs.replace('true', '20 0 R'), extra={20: b'true'})
    for key in ('BitsPerComponent', 'ColorSpace', 'Decode', 'Mask', 'SMask'):
        for value in ('null', '20 0 R', '21 0 R', '999 0 R'):
            yield f'stencil-default-{key}-{value}', stencil, fixture(
                b'\x40\x80', stencil_attrs + f' /{key} {value}', extra=DEFAULTS)
    for soft in (False, True):
        key = 'SMask' if soft else 'Mask'
        mask_attrs = '/Width 2 /Height 2 ' + (
            '/BitsPerComponent 8 /ColorSpace /DeviceGray' if soft else '/ImageMask true')
        samples = bytes([64, 128, 192, 255]) if soft else b'\x40\x80'
        mask = image_stream(samples, mask_attrs)
        parent = ATTRS + f' /{key} 7 0 R'
        expected = fixture(attrs=parent, extra={7: mask})
        yield 'mask-alias-' + key, expected, fixture(attrs=ATTRS + f' /{key} 8 0 R',
                                                   extra={7: mask, 8: b'7 0 R'})
        fields = ('Mask', 'SMask', 'Decode', 'Matte', 'Type', 'ImageMask')
        if not soft:
            fields += ('BitsPerComponent', 'ColorSpace')
        for field in fields:
            if not soft and field == 'ImageMask':
                continue
            for value in ('null', '20 0 R', '21 0 R', '999 0 R'):
                if field == 'Type':
                    altered = mask.replace(b'/Type /XObject', f'/Type {value}'.encode())
                else:
                    altered = image_stream(samples, mask_attrs + f' /{field} {value}')
                yield f'mask-default-{soft}-{field}-{value}', expected, fixture(
                    attrs=parent, extra={7: altered, **DEFAULTS})
        for field, value in (('Width', '2'), ('Height', '2'), ('Subtype', '/Image'),
                              ('Type', '/XObject'),
                              ('ColorSpace', '/DeviceGray') if soft else ('ImageMask', 'true')):
            altered = mask.replace(f'/{field} {value}'.encode(), f'/{field} 20 0 R'.encode())
            yield f'mask-value-{soft}-{field}', expected, fixture(
                attrs=parent, extra={7: altered, 20: value.encode()})
        altered = image_stream(samples, mask_attrs + ' /Decode [1 0]')
        expected_decode = fixture(attrs=parent, extra={7: altered})
        for value in ('20 0 R', '[21 0 R 22 0 R]'):
            altered = image_stream(samples, mask_attrs + ' /Decode ' + value)
            yield f'mask-decode-{soft}-{value}', expected_decode, fixture(
                attrs=parent, extra={7: altered, 20: b'[1 0]', 21: b'1', 22: b'0'})
        if soft:
            matte = image_stream(samples, mask_attrs + ' /Matte [.25 .5 .75]')
            altered = image_stream(samples, mask_attrs + ' /Matte 20 0 R')
            yield 'indirect-matte-members', fixture(attrs=parent, extra={7: matte}), fixture(
                attrs=parent, extra={7: altered, 20: b'[21 0 R 22 0 R 23 0 R]',
                                     21: b'.25', 22: b'.5', 23: b'.75'})
    escaped = image_stream(RGB, ATTRS).replace(b'/Subtype /Image', b'/Subtype /Im#61ge')
    yield 'escaped-image-subtype', baseline, fixture(extra={5: escaped})
    # Null soft masks do not suppress a separately supplied explicit mask.
    explicit = ATTRS + ' /Mask [255 255 0 0 0 0]'
    yield 'null-soft-mask-fallback', fixture(attrs=explicit), fixture(attrs=explicit + ' /SMask null')


def main():
    probe, oom = (str(Path(arg).resolve()) for arg in sys.argv[1:3])
    checks = allocations = rejections = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-image-values-') as directory:
        path = Path(directory) / 'case.pdf'
        for name, expected, changed in cases():
            output = []
            for data in (expected, changed):
                path.write_bytes(data)
                result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
                assert result.returncode == 0, (name, result.stderr)
                output.append(result.stdout)
            assert output[0] == output[1], name
            checks += 1
            result = subprocess.run([oom, str(path), '0'], capture_output=True, text=True, timeout=60)
            assert result.returncode == 0, (name, result.stdout, result.stderr)
            allocations += int(result.stdout.split(': ')[1].split()[0])
        for key, value, extra in (
            ('ImageMask', '20 0 R', {20: b'1'}),
            ('Decode', '[0 null 0 1 0 1]', {}),
            ('Decode', '20 0 R', {20: b'20 0 R'}),
            ('Decode', '[0 20 0 R 0 1 0 1]', {20: b'false'}),
            ('Mask', '[255 255 0 0 null 0]', {}),
            ('SMask', '20 0 R', {20: b'false'}),
            ('ImageMask', '20 0 R', {20: b'21 0 R', 21: b'20 0 R'}),
        ):
            path.write_bytes(fixture(attrs=ATTRS + f' /{key} {value}', extra=extra))
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert result.returncode == 3, (key, value, result.stderr)
            rejections += 1
        for key, original, value in (('Type', '/XObject', '/Page'), ('Subtype', '/Image', 'null')):
            body = image_stream(RGB, ATTRS).replace(f'/{key} {original}'.encode(), f'/{key} {value}'.encode())
            path.write_bytes(fixture(extra={5: body}))
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert result.returncode == 3, (key, result.stderr)
            rejections += 1
    print(f'PDF image values: {checks} equivalence checks, {rejections} rejections and {allocations} allocation checks passed')


if __name__ == '__main__':
    main()
