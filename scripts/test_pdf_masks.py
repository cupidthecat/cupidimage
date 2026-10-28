#!/usr/bin/env python3
"""Exact-pixel coverage for explicit masks, soft masks, and matte recovery."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from audit_pdf_corpus import read_ppm
from test_pdf_images import fixture, image_stream
from test_pdf_packed_images import pack


def main():
    probe = str(Path(sys.argv[1]).resolve())
    count = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-masks-') as directory:
        def check(name, payload, attrs, mask, expected=None, reject=False, content=None):
            nonlocal count
            count += 1
            path = Path(directory) / (name + '.pdf')
            args = {} if content is None else {'content': content}
            path.write_bytes(fixture(payload, attrs, extra={8: mask}, **args))
            run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=10)
            assert run.returncode == (3 if reject else 0), (name, run.stderr)
            if reject:
                return
            assert struct.unpack('=III', run.stdout[:12]) == (24, 16, 1)
            wanted = bytes(c for y in range(16) for x in range(24) for c in (*expected(x, y), 255))
            # Match the renderer's non-antialiased path clipping at pixel edges.
            if '--reference' in sys.argv:
                prefix = Path(directory) / 'reference'
                subprocess.run(['pdftoppm', '-singlefile', '-r', '96', '-aaVector', 'no', str(path), str(prefix)], check=True, capture_output=True, timeout=15)
                w, h, pixels = read_ppm(prefix.with_suffix('.ppm'))
                assert (w, h) == (24, 16), name
                actual = bytes(c for i, c in enumerate(run.stdout[12:]) if i % 4 != 3)
                assert max(abs(a-b) for a, b in zip(actual, pixels)) <= 1, (name, 'reference differs')
            assert run.stdout[12:] == wanted, (name, next((i, a, b) for i, (a, b) in enumerate(zip(run.stdout[12:], wanted)) if a != b))

        attrs = '/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB /SMask 8 0 R'
        red = bytes((255, 0, 0))
        for bits in (1, 2, 4, 8, 16):
            maximum = (1 << bits) - 1
            values = [0, maximum, maximum // 3, 2 * maximum // 3]
            for invert in (False, True):
                for compressed in (False, True):
                    data = pack(values[:2], bits) + pack(values[2:], bits)
                    extra = ' /Decode [1 0]' if invert else ''
                    if compressed:
                        data = zlib.compress(data)
                        extra += ' /Filter /FlateDecode'
                    mask = image_stream(data, f'/Width 2 /Height 2 /BitsPerComponent {bits} /ColorSpace /DeviceGray' + extra)
                    def expected(x, y):
                        a = values[(y // 8) * 2 + x // 12] / maximum
                        if invert:
                            a = 1-a
                        return tuple(int(a*c + (1-a)*128 + .5) for c in red)
                    check(f'soft-{bits}-{invert}-{compressed}', red, attrs, mask, expected)
        for invert in (False, True):
            mask = image_stream(bytes((0x40, 0x80)), '/Width 2 /Height 2 /ImageMask true' + (' /Decode [1 0]' if invert else ''))
            def expected(x, y):
                painted = (x // 12 == y // 8) != invert
                return (255, 0, 0) if painted else (128, 128, 128)
            check(f'explicit-{invert}', red, attrs.replace('/SMask', '/Mask'), mask, expected)
        gray_mask = '/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray'
        opaque = image_stream(b'\xff', gray_mask)
        check('soft-overrides-key', red, attrs + ' /Mask [255 255 0 0 0 0]', opaque, lambda x, y: (255, 0, 0))
        check('soft-overrides-invalid-mask', red, attrs + ' /Mask 99 0 R', opaque, lambda x, y: (255, 0, 0))
        # Unblend in component space, after Decode, before converting to RGB.
        check('white-matte', bytes((255, 127, 127)), attrs,
              image_stream(b'\x80', gray_mask + ' /Matte [1 1 1]'), lambda x, y: (192, 64, 64))
        check('indexed-rgb-white-matte', b'\0',
              attrs.replace('/DeviceRGB', '[/Indexed /DeviceRGB 0 <ff7f7f>]'),
              image_stream(b'\x80', gray_mask + ' /Matte [1 1 1]'), lambda x, y: (192, 64, 64))
        check('black-matte', bytes((128, 0, 0)), attrs,
              image_stream(b'\x80', gray_mask + ' /Matte [0 0 0]'), lambda x, y: (192, 64, 64))
        check('zero-alpha-matte', red, attrs, image_stream(b'\0', gray_mask + ' /Matte [1 1 1]'), lambda x, y: (128, 128, 128))
        check('clipped-mask', red, attrs, opaque,
              lambda x, y: (255, 0, 0) if x < 12 else (128, 128, 128),
              content='0 0 9 12 re W n 18 0 0 12 0 0 cm /Im0 Do')
        for name, properties, data in (
            ('recursive', gray_mask + ' /SMask 8 0 R', b'\xff'),
            ('nested-explicit', gray_mask + ' /Mask 8 0 R', b'\xff'),
            ('stencil', gray_mask + ' /ImageMask true', b'\xff'),
            ('invalid-boolean', gray_mask + ' /ImageMask falsehood', b'\xff'),
            ('wrong-space', gray_mask.replace('DeviceGray', 'DeviceRGB'), b'\xff'),
            ('zero-width', gray_mask.replace('/Width 1', '/Width 0'), b'\xff'),
            ('huge', gray_mask.replace('/Width 1', '/Width 2147483647'), b'\xff'),
            ('bits', gray_mask.replace('Component 8', 'Component 3'), b'\xff'),
            ('short', gray_mask, b''),
            ('long', gray_mask, b'\xff\xff'),
            ('decode-short', gray_mask + ' /Decode [0]', b'\xff'),
            ('decode-long', gray_mask + ' /Decode [0 1 2]', b'\xff'),
            ('matte-short', gray_mask + ' /Matte [1]', b'\xff'),
            ('matte-range', gray_mask + ' /Matte [1 2 0]', b'\xff'),
            ('matte-size', gray_mask.replace('/Width 1', '/Width 2') + ' /Matte [1 1 1]', b'\xff\xff'),
        ):
            check(name, red, attrs, image_stream(data, properties), reject=True)
        check('self-reference', red, attrs.replace('8 0 R', '5 0 R'), opaque, reject=True)
        check('explicit-must-be-stencil', red, attrs.replace('/SMask', '/Mask'), opaque, reject=True)
    print(f'PDF image masks: {count} passed')


if __name__ == '__main__':
    main()
