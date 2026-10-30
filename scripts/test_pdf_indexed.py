#!/usr/bin/env python3
"""Check indexed image palettes, resources, masks, and malformed lookups."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_images import fixture, image_stream
from test_pdf_pages import document as make_document
from test_pdf_packed_images import pack
from audit_pdf_corpus import read_ppm


def main():
    probe = str(Path(sys.argv[1]).resolve())
    count = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-indexed-') as directory:
        def check(name, space, data, expected=None, bits=8, extra=None, attrs='', reject=False, reference=True, resource=None, secondary=False):
            nonlocal count
            count += 1
            path = Path(directory) / (name + '.pdf')
            pdf = fixture(data, f'/Width 3 /Height 2 /BitsPerComponent {bits} /ColorSpace {space} '+attrs, extra=extra)
            if resource:
                objects = {5: image_stream(data, f'/Width 3 /Height 2 /BitsPerComponent {bits} /ColorSpace {space} '+attrs)}
                objects.update(extra or {})
                pdf = make_document('/MediaBox [0 0 18 12] /Resources << /XObject << /Im0 5 0 R >> /ColorSpace '+resource+' >>',
                                    stream='0.5 g -100 -100 300 300 re f 18 0 0 12 0 0 cm /Im0 Do', extra=objects)
            path.write_bytes(pdf.replace(b"%PDF-1.4", b"%PDF-1.7"))
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
                actual = bytes(c for i, c in enumerate(run.stdout[12:]) if i%4 != 3)
                assert (w, h) == (24, 16) and actual == rgb, (name, 'reference differs')

        palette = bytes((255, 0, 0, 0, 255, 0, 0, 0, 255))
        colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255)]
        indices = [0, 1, 2, 2, 1, 0]
        space = '[/Indexed /DeviceRGB 2 <'+palette.hex()+'>]'
        expected = lambda x, y: colors[indices[(y//8)*3+x//8]]
        for bits in (2, 4, 8, 16):
            data = pack(indices[:3], bits) + pack(indices[3:], bits)
            # Full 16-bit palette indices use analytic expectations; the installed
            # comparison rasterizers reduce these samples before palette lookup.
            check('packed-'+str(bits), space, data, expected, bits=bits, reference=bits != 16)
        data = bytes(indices)
        check('indirect-space', '8 0 R', data, expected, extra={8: space})
        # Reference chains are a bounded compatibility extension.
        check('indirect-space-chain', '8 0 R', data, expected, extra={8: '9 0 R', 9: space}, reference=False)
        check('named-space', '/Palette', data, expected, resource='<< /Palette '+space+' >>', reject=True)
        check('indirect-resources', '/Palette', data, expected, resource='8 0 R', extra={8: '<< /Palette 9 0 R >>', 9: space}, reject=True)
        literal = '('+''.join('\\%03o'%c for c in palette)+')'
        check('literal', '[/Indexed /DeviceRGB 2 '+literal+']', data, expected)
        check('indirect-string', '[/Indexed /DeviceRGB 2 8 0 R]', data, expected, extra={8: '<'+palette.hex()+'>'})
        check('indirect-base', '[/Indexed 8 0 R 2 <'+palette.hex()+'>]', data, expected, extra={8: '/DeviceRGB'})
        stream = lambda b, attrs='': f'<< /Length {len(b)} {attrs} >>\nstream\n'.encode()+b+b'\nendstream'
        for name, payload, filters in (('raw', palette, ''), ('flate', zlib.compress(palette), '/Filter /FlateDecode'),
                                       ('hex', palette.hex().encode()+b'>', '/Filter /ASCIIHexDecode')):
            check('palette-stream-'+name, '[/Indexed /DeviceRGB 2 8 0 R]', data, expected, extra={8: stream(payload, filters)})
        check('decode-inverted', space, data, lambda x, y: colors[2-indices[(y//8)*3+x//8]], attrs='/Decode [2 -253]')
        check('decode-clamped', space, bytes((0, 1, 2, 3, 254, 255)), lambda x, y: colors[min(2, (0,1,2,3,254,255)[(y//8)*3+x//8])])
        check('decode-rounded', space, bytes((0, 1, 2, 3, 4, 5)), lambda x, y: colors[min(2, int((y//8*3+x//8)*.5+.5))], attrs='/Decode [0 127.5]')
        check('color-key', space, data, lambda x, y: (128,)*3 if indices[(y//8)*3+x//8] == 1 else expected(x,y), attrs='/Mask [1 1]')
        mask = image_stream(b'\0\xff', '/Width 1 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceGray')
        check('soft-mask', space, data, lambda x, y: (128,)*3 if y < 8 else expected(x,y), attrs='/SMask 8 0 R', extra={8:mask})
        check('gray-base', '[/Indexed /DeviceGray 2 <0080ff>]', data, lambda x, y: ((0,128,255)[indices[(y//8)*3+x//8]],)*3)
        check('cmyk-base', '[/Indexed /DeviceCMYK 2 <00ffff00ff00ff00ffff0000>]', data, expected, reference=False)
        check('one-bit', '[/Indexed /DeviceRGB 1 <ff000000ff00>]', b'\x40\xa0', lambda x, y: colors[(y//8*3+x//8)%2], bits=1)
        check('escaped-family', space.replace('/Indexed', '/In#64exed'), data, expected)
        check('odd-hex-nibble', '[/Indexed /DeviceGray 0 <f>]', data, lambda x,y: (240,)*3)
        check('literal-nesting', '[/Indexed /DeviceRGB 0 ((a))]', data, lambda x,y: (40,97,41))
        # ISO 32000-1 7.3.4.2 requires unescaped CRLF to become LF.
        # The secondary oracle follows that rule for palette strings.
        check('literal-line-ending', '[/Indexed /DeviceRGB 0 (A\r\nB)]', data, lambda x,y: (65,10,66), secondary=True)
        check('literal-line-continuation', '[/Indexed /DeviceRGB 0 (A\\\nBC)]', data, lambda x,y: (65,66,67))
        check('maximum-palette', '[/Indexed /DeviceGray 255 <'+bytes(range(256)).hex()+'>]', bytes((0,51,102,153,204,255)),
              lambda x,y: (((y//8)*3+x//8)*51,)*3)
        check('indexed-matte-wrong-component-count', space, data, attrs='/SMask 8 0 R',
              extra={8:image_stream(bytes([255]*6), '/Width 3 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceGray /Matte [1]')}, reject=True)
        check('indexed-matte-base-components', space, data, expected, attrs='/SMask 8 0 R',
              extra={8:image_stream(bytes([255]*6), '/Width 3 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceGray /Matte [1 1 1]')})
        for name, broken, extra in (
            ('negative-high', '[/Indexed /DeviceRGB -1 <>]', None),
            ('high-too-large', '[/Indexed /DeviceRGB 256 <>]', None),
            ('fractional-high', '[/Indexed /DeviceRGB 1.5 <>]', None),
            ('short-palette', '[/Indexed /DeviceRGB 2 <ff>]', None),
            ('long-palette', '[/Indexed /DeviceRGB 0 <ff000000>]', None),
            ('unterminated-hex', '[/Indexed /DeviceRGB 0 <ff0000]', None),
            ('unterminated-string', '[/Indexed /DeviceRGB 0 (abc]', None),
            ('bad-hex', '[/Indexed /DeviceRGB 0 <xx0000>]', None),
            ('missing-close', '[/Indexed /DeviceRGB 0 <ff0000>', None),
            ('nested-indexed', '[/Indexed [/Indexed /DeviceGray 0 <00>] 0 <00>]', None),
            ('cyclic-space', '8 0 R', {8:'9 0 R',9:'8 0 R'}),
            ('cyclic-lookup', '[/Indexed /DeviceRGB 2 8 0 R]', {8:'8 0 R'}),
            ('bad-stream', '[/Indexed /DeviceRGB 2 8 0 R]', {8:stream(b'bad', '/Filter /FlateDecode')}),
        ):
            check(name, broken, data, extra=extra, reject=True)
        check('cyclic-resource', '/Palette', data, resource='<< /Palette /Palette >>', reject=True)
    print(f'PDF indexed colors: {count} passed')


if __name__ == '__main__':
    main()
