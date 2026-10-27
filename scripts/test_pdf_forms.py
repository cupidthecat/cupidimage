#!/usr/bin/env python3
"""Render generated Form XObjects and compare their complete pixel output."""
import base64
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib

from test_pdf_pages import document
from test_pdf_images import image_stream
from test_jpeg_decoder import jpeg, scan, EOI
from test_pdf_lzw import literals

RED, BLUE, GRAY = (255, 0, 0), (0, 0, 255), (128, 128, 128)
PAINT = '1 0 0 rg -100 -100 300 300 re f'


def form(content=PAINT, attrs='/BBox [0 0 9 6]', compression=None, length=None):
    payload = content.encode() if isinstance(content, str) else content
    if compression is not None:
        payload = zlib.compress(payload, compression)
        attrs += ' /Filter /FlateDecode'
    size = len(payload) if length is None else length
    return (f'<< /Type /XObject /Subtype /Form {attrs} /Length {size} >>\nstream\n'.encode()
            + payload + b'\nendstream')


def fixture(body=None, content='/F0 Do', extra=None, resources='/F0 5 0 R', attrs=''):
    objects = {5: form() if body is None else body}
    objects.update(extra or {})
    return document('/MediaBox [0 0 18 12] /Resources << /XObject << ' + resources + ' >> >> ' + attrs,
                    stream='0.5 g -100 -100 300 300 re f ' + content, extra=objects)


def main():
    probe = str(pathlib.Path(sys.argv[1]).resolve())
    failures = count = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-pdf-forms-') as directory:
        def check(name, pdf, expected=None, reject=False, same=None):
            nonlocal failures, count
            count += 1
            path = pathlib.Path(directory) / (name + '.pdf')
            path.write_bytes(pdf)
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=15)
            try:
                if reject:
                    assert result.returncode == 3, (result.returncode, result.stderr[:1000])
                    assert result.stderr
                    return
                assert result.returncode == 0, result.stderr[:1000]
                assert struct.unpack('=III', result.stdout[:12]) == (24, 16, 1)
                assert len(result.stdout) == 12 + 24 * 16 * 4
                if same is not None:
                    assert result.stdout == same
                if expected is not None:
                    for y in range(16):
                        for x in range(24):
                            got = tuple(result.stdout[12 + (y * 24 + x) * 4:16 + (y * 24 + x) * 4])
                            want = (*expected(x, y), 255)
                            assert got == want, (x, y, got, want)
                return result.stdout
            except (AssertionError, struct.error) as error:
                failures += 1
                print('FAIL', name, str(error), file=sys.stderr)

        lower_left = lambda x, y: RED if x < 12 and y >= 8 else GRAY
        for label, payload, filters in (
            ('hex', PAINT.encode().hex().encode() + b'>', '/ASCIIHexDecode'),
            ('ascii85', base64.a85encode(PAINT.encode()) + b'~>', '/ASCII85Decode'),
            ('runlength', bytes([len(PAINT)-1]) + PAINT.encode() + b'\x80', '/RunLengthDecode'),
            ('ascii85-flate', base64.a85encode(zlib.compress(PAINT.encode())) + b'~>', '[/ASCII85Decode /FlateDecode]'),
        ):
            check('filtered-' + label, fixture(form(payload, attrs='/BBox [0 0 9 6] /Filter ' + filters)), lower_left)
        for compression in (None, 0, 6, 9):
            check('paint-' + str(compression), fixture(form(compression=compression)), lower_left)
        check('identity-matrix', fixture(form(attrs='/BBox [0 0 9 6] /Matrix [1 0 0 1 0 0]')), lower_left)
        check('double-scale', fixture(form(attrs='/BBox [0 0 9 6] /Matrix [2 0 0 2 0 0]')), lambda x, y: RED)
        check('translation', fixture(content='q 1 0 0 1 9 6 cm /F0 Do Q'),
              lambda x, y: RED if x >= 12 and y < 8 else GRAY)
        check('concatenated-matrix', fixture(form(attrs='/BBox [0 0 9 6] /Matrix [0.5 0 0 0.5 0 0]'),
              content='2 0 0 2 0 0 cm /F0 Do'), lower_left)
        check('rotation', fixture(form(attrs='/BBox [0 0 9 6] /Matrix [0 1 -1 0 18 0]')),
              lambda x, y: RED if x >= 16 and y >= 4 else GRAY)
        check('reflection', fixture(form(attrs='/BBox [0 0 9 6] /Matrix [-1 0 0 1 18 0]')),
              lambda x, y: RED if x >= 12 and y >= 8 else GRAY)
        check('sheared-bbox', fixture(form(attrs='/BBox [0 0 9 6] /Matrix [1 0 1 1 0 0]')),
              lambda x, y: RED if y >= 8 and 0 <= (x + y - 15) * 0.75 < 9 else GRAY)
        check('indirect-bbox-matrix', fixture(form(attrs='/BBox 6 0 R /Matrix 7 0 R'),
              extra={6: '[0 0 9 6]', 7: '[1 0 0 1 0 0]'}), lower_left)
        check('bbox-offset', fixture(form(attrs='/BBox [9 6 18 12]')),
              lambda x, y: RED if x >= 12 and y < 8 else GRAY)
        check('inherited-color', fixture(form('-100 -100 300 300 re f'), content='1 0 0 rg /F0 Do'), lower_left)
        check('caller-color-restored', fixture(content='0 0 1 rg /F0 Do 9 6 9 6 re f'),
              lambda x, y: RED if x < 12 and y >= 8 else BLUE if x >= 12 and y < 8 else GRAY)
        check('caller-matrix-restored', fixture(form(attrs='/BBox [0 0 9 6] /Matrix [2 0 0 2 0 0]'),
              content='/F0 Do 0 0 1 rg 9 6 9 6 re f'), lambda x, y: BLUE if x >= 12 and y < 8 else RED)
        check('caller-path-preserved', fixture(content='9 6 9 6 re /F0 Do 0 0 1 rg f'),
              lambda x, y: RED if x < 12 and y >= 8 else BLUE if x >= 12 and y < 8 else GRAY)
        check('unbalanced-form-q', fixture(form('Q ' + PAINT + ' q 2 0 0 2 0 0 cm'),
              content='/F0 Do 0 0 1 rg 9 6 9 6 re f'),
              lambda x, y: RED if x < 12 and y >= 8 else BLUE if x >= 12 and y < 8 else GRAY)
        check('repeated-form', fixture(content='/F0 Do q 1 0 0 1 9 6 cm /F0 Do Q'),
              lambda x, y: RED if (x < 12) == (y >= 8) else GRAY)
        check('empty-form', fixture(form('')), lambda x, y: GRAY)
        check('singular-transform', fixture(form(attrs='/BBox [0 0 9 6] /Matrix [0 0 0 0 0 0]')), lambda x, y: GRAY)

        red_image = image_stream(bytes(RED), '/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB')
        blue_image = image_stream(bytes(BLUE), '/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB')
        image_content = '18 0 0 12 0 0 cm /Im0 Do'
        for resources, extra in (
            ('/Resources << /XObject << /Im0 6 0 R >> >>', {6: red_image}),
            ('/Resources 7 0 R', {6: red_image, 7: '<< /XObject 8 0 R >>', 8: '<< /Im0 6 0 R >>'}),
            ('', {6: red_image}),
        ):
            check('image-resources-' + str(count), fixture(form(image_content, '/BBox [0 0 9 6] ' + resources),
                  resources='/F0 5 0 R /Im0 6 0 R', extra=extra), lower_left)
        check('resource-shadow', fixture(form(image_content,
              '/BBox [0 0 9 6] /Resources << /XObject << /Im0 7 0 R >> >>'),
              resources='/F0 5 0 R /Im0 6 0 R', extra={6: red_image, 7: blue_image}),
              lambda x, y: BLUE if x < 12 and y >= 8 else GRAY)
        check('empty-resources-shadow', fixture(form(image_content, '/BBox [0 0 9 6] /Resources << >>'),
              resources='/F0 5 0 R /Im0 6 0 R', extra={6: red_image}), reject=True)
        check('nested-clips', fixture(form('/F1 Do', '/BBox [0 0 9 6] /Resources << /XObject << /F1 6 0 R >> >>'),
              extra={6: form(attrs='/BBox [0 0 18 12]')}), lower_left)
        check('nested-transformed-clips', fixture(form('q 1 0 0 1 3 0 cm /F1 Do Q',
              '/BBox [0 0 9 6] /Resources << /XObject << /F1 6 0 R >> >>'),
              extra={6: form(attrs='/BBox [0 0 9 6]')}),
              lambda x, y: RED if 4 <= x < 12 and y >= 8 else GRAY)
        jpeg_payload = jpeg(3, False, 2, 2, dc=1) + scan((1, 2, 3)) + b'\x41\x7f' + EOI
        check('jpeg-in-form', fixture(form(image_content), resources='/F0 5 0 R /Im0 6 0 R',
              extra={6: image_stream(jpeg_payload,
              '/Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB /Filter /DCTDecode')}),
              lambda x, y: (130, 129, 127) if x < 12 and y >= 8 else GRAY)

        text = 'BT /F1 6 Tf 1 0 0 1 0 6 Tm (A) Tj ET'
        glyph_a = (0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0)
        text_reference = check('inline-text-reference', fixture(content='1 0 0 rg ' + text),
              lambda x, y: RED if x < 8 and y < 8 and glyph_a[y] & (1 << x) else GRAY)
        check('text-in-form', fixture(form('1 0 0 rg ' + text, '/BBox [0 0 18 12]')), same=text_reference)
        check('scaled-text', fixture(form('1 0 0 rg ' + text.replace('0 6 Tm', '0 0 Tm'),
              '/BBox [0 0 18 12] /Matrix [2 0 0 2 0 0]')),
              lambda x, y: RED if x < 16 and glyph_a[y // 2] & (1 << (x // 2)) else GRAY)
        check('rotated-text', fixture(form('1 0 0 rg ' + text,
              '/BBox [0 0 18 12] /Matrix [0 1 -1 0 12 0]')),
              lambda x, y: RED if x < 8 and y >= 8 and glyph_a[x] & (1 << (15 - y)) else GRAY)
        # A 9-point translation is exactly 12 pixels at 96 DPI.
        check('translated-text', fixture(form('1 0 0 rg ' + text,
              '/BBox [0 0 18 12] /Matrix [1 0 0 1 9 0]')),
              lambda x, y: RED if 12 <= x < 20 and y < 8 and glyph_a[y] & (1 << (x - 12)) else GRAY)
        check('text-clipped', fixture(form('1 0 0 rg ' + text, '/BBox [9 0 18 12]')), lambda x, y: GRAY)
        check('huge-text-position', fixture(form('BT 99999999999999999999999999999999999999999 0 Td (A) Tj ET')), lambda x, y: GRAY)
        cmap = b'1 beginbfchar\n<5A> <0041>\nendbfchar'
        font_objects = {6: '<< /Type /Font /Subtype /Type1 /ToUnicode 7 0 R >>',
                        7: b'<< /Length ' + str(len(cmap)).encode() + b' >>\nstream\n' + cmap + b'\nendstream'}
        check('form-font-map', fixture(form('1 0 0 rg ' + text.replace('(A)', '(Z)'),
              '/BBox [0 0 18 12] /Resources << /Font << /F1 6 0 R >> >>'), extra=font_objects), same=text_reference)
        for label, payload, filters in (
            ('hex', cmap.hex().encode() + b'>', '/ASCIIHexDecode'),
            ('ascii85', base64.a85encode(cmap) + b'~>', '/ASCII85Decode'),
            ('runlength', bytes([len(cmap)-1]) + cmap + b'\x80', '/RunLengthDecode'),
            ('lzw', literals(cmap, 1), '/LZWDecode'),
            ('chain', base64.a85encode(zlib.compress(cmap)) + b'~>', '[/ASCII85Decode /FlateDecode]'),
        ):
            cmap_stream=f'<< /Length {len(payload)} /Filter {filters} >>\nstream\n'.encode()+payload+b'\nendstream'
            check('filtered-font-map-'+label, fixture(form('1 0 0 rg '+text.replace('(A)', '(Z)'),
                  '/BBox [0 0 18 12] /Resources << /Font << /F1 6 0 R >> >>'),
                  extra={**font_objects, 7:cmap_stream}), same=text_reference)
        check('indirect-form-font-map', fixture(form('1 0 0 rg ' + text.replace('(A)', '(Z)'),
              '/BBox [0 0 18 12] /Resources 8 0 R'), extra={**font_objects,
              8: '<< /Font 9 0 R >>', 9: '<< /F1 6 0 R >>'}), same=text_reference)

        for depth in (16, 17):
            extra = {number: form('/F0 Do', f'/BBox [0 0 9 6] /Resources << /XObject << /F0 {number + 1} 0 R >> >>')
                     for number in range(5, 5 + depth - 1)}
            extra[4 + depth] = form()
            check('depth-' + str(depth), fixture(extra=extra), lower_left if depth == 16 else None, reject=depth == 17)
        check('self-recursion', fixture(form('/F0 Do')), reject=True)
        check('mutual-recursion', fixture(form('/F1 Do', '/BBox [0 0 9 6] /Resources << /XObject << /F1 6 0 R >> >>'),
              extra={6: form('/F0 Do', '/BBox [0 0 9 6] /Resources << /XObject << /F0 5 0 R >> >>')}), reject=True)
        check('indirect-resources-not-dictionary', fixture(form(attrs='/BBox [0 0 9 6] /Resources 6 0 R'),
              extra={6: '12'}), reject=True)
        check('non-isolated-group', fixture(form(attrs='/BBox [0 0 9 6] /Group << /S /Transparency >>')),
              expected=lambda x,y: RED if x<12 and y>=8 else GRAY)
        for name, body in (
            ('missing-bbox', form(attrs='')),
            ('invalid-bbox', form(attrs='/BBox [0 0 bad 6]')),
            ('matrix-short', form(attrs='/BBox [0 0 9 6] /Matrix [1 0 0]')),
            ('matrix-extra', form(attrs='/BBox [0 0 9 6] /Matrix [1 0 0 1 0 0 9]')),
            ('form-type', form(attrs='/BBox [0 0 9 6] /FormType 2')),
            ('reference', form(attrs='/BBox [0 0 9 6] /Ref << >>')),
            ('resources', form(attrs='/BBox [0 0 9 6] /Resources /bad')),
            ('filter', form(attrs='/BBox [0 0 9 6] /Filter /LZWDecode')),
            ('truncated-flate', form(b'bad zlib', attrs='/BBox [0 0 9 6] /Filter /FlateDecode')),
            ('length', form(length=10000)),
            ('content-string', form('(unterminated')),
        ):
            check(name, fixture(body), reject=True)
        # Repeated compressed streams share one decoded-byte budget per page.
        check('repeated-stream-budget', fixture(form(b' ' * (5 * 1024 * 1024), compression=6),
              content='/F0 Do ' * 13), reject=True)
        check('empty-form-call-limit', fixture(form(''), content='/F0 Do ' * 4097), reject=True)
    print(f'{count} PDF form checks, {failures} failures')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
