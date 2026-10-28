#!/usr/bin/env python3
"""Verify page-unit scaling before font, image, form and pattern transforms."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_type3 import stream, fixture as type3
from test_pdf_embedded_fonts import fixture as truetype
from test_pdf_cff_render import fixture as cff
from test_pdf_type1_render import fixture as type1
from test_pdf_images import image_stream, RGB
from test_pdf_forms import form


def scene(unit, explicit, kind='standard', mode=0, rotation=0, origin=(0, 0)):
    scale = unit if explicit else 1
    x, y = origin
    box = ' '.join(f'{v * scale:.12g}' for v in (x, y, x + 36, y + 24))
    attrs = f'/MediaBox [{box}] /UserUnit {1 if explicit else unit} /Rotate {rotation}'
    resources = '/Resources << /Font << /F 5 0 R >> /XObject << /Im 11 0 R /Fm 12 0 R >> '
    resources += '/Pattern << /P 13 0 R >> /ExtGState << /Half << /ca .5 /CA .5 >> >> >>'
    content = f'0.75 w BT /F 12 Tf {mode} Tr 3 4 Td (AB) Tj ET'
    if mode >= 4:
        content += ' .25 .5 .75 rg 0 0 36 24 re f'
    if kind == 'image':
        content = 'q 12 3 -2 12 4 3 cm /Im Do Q'
    elif kind == 'form':
        content = 'q 1 .25 .125 1 4 2 cm /Fm Do Q'
    elif kind == 'pattern':
        content = '/Pattern cs /P scn 0 0 36 24 re f'
    elif kind == 'path':
        content = '1 0 0 rg 0 0 18 12 re f .75 w 0 0 m 24 18 l S'
    elif kind == 'transparency':
        content = '/Half gs ' + content + ' BT /F 12 Tf 6 6 Td (AB) Tj ET'
    if explicit:
        content = f'{unit} 0 0 {unit} 0 0 cm ' + content
    objects = {
        3: f'<< /Type /Page /Parent 2 0 R {attrs} {resources} >>'.encode(),
        4: stream(content),
        11: image_stream(RGB, '/Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB'),
        12: form('BT /F 12 Tf 3 4 Td (AB) Tj ET',
                 '/BBox [0 0 30 24] /Resources << /Font << /F 5 0 R >> >>'),
        13: stream('1 0 0 rg 0 0 3 6 re f',
                   '/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 2 '
                   f'/BBox [0 0 6 6] /XStep 6 /YStep 6 /Matrix [{scale} 0 0 {scale} 0 0] /Resources << >>'),
    }
    if kind in ('truetype', 'cff', 'type1', 'type3'):
        return {'truetype': truetype, 'cff': cff, 'type1': type1, 'type3': type3}[kind]('', extra=objects)
    objects[5] = b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>'
    return document(extra=objects)


def cases():
    for unit in (.5, 1.5, 2, 4):
        for kind in ('standard', 'truetype', 'cff', 'type1', 'type3', 'image', 'form', 'pattern', 'path', 'transparency'):
            for rotation, origin in ((0, (0, 0)), (90, (-9, -6)), (270, (6, 3))):
                yield f'{unit}-{kind}-{rotation}', scene(unit, True, kind, rotation=rotation, origin=origin), scene(
                    unit, False, kind, rotation=rotation, origin=origin)
    for kind in ('standard', 'truetype', 'cff', 'type1'):
        for mode in range(8):
            yield f'text-mode-{kind}-{mode}', scene(2, True, kind, mode), scene(2, False, kind, mode)
    for unit in (.5, 2, 4):
        for crop in ((3, 3, 15, 9), (-3, -3, 24, 18), (15, 9, 3, 3)):
            media = ' '.join(str(v * unit) for v in (0, 0, 18, 12))
            scaled_crop = ' '.join(str(v * unit) for v in crop)
            actual_crop = ' '.join(map(str, crop))
            paint = '1 0 0 rg 0 0 9 6 re f'
            yield f'crop-{unit}-{crop}', document(
                f'/MediaBox [{media}] /CropBox [{scaled_crop}]',
                stream=f'{unit} 0 0 {unit} 0 0 cm ' + paint), document(
                f'/MediaBox [0 0 18 12] /CropBox [{actual_crop}] /UserUnit {unit}', stream=paint)
    base = '/MediaBox [0 0 18 12]'
    expected = document(base)
    for value in ('null', '20 0 R', '21 0 R', '999 0 R'):
        yield 'default-unit-' + value, expected, document(base + ' /UserUnit ' + value,
                                                        extra={20: b'null', 21: b'20 0 R'})
    for value in ('2', 'null', '(ignored)'):
        yield 'not-inherited-' + value, expected, document(base, '/UserUnit ' + value)
    yield 'indirect-unit-chain', document('/MediaBox [0 0 36 24]', stream='2 0 0 2 0 0 cm 1 0 0 rg 0 0 18 12 re f'), document(
        base + ' /UserUnit 20 0 R', stream='1 0 0 rg 0 0 18 12 re f', extra={20: b'21 0 R', 21: b'2'})
    for key, value in (('MediaBox', '[0 0 18 12]'), ('CropBox', '[3 0 15 12]'), ('Rotate', '90')):
        parent = base if key != 'MediaBox' else ''
        parent += f' /{key} {value}'
        expected = document(parent_attrs=parent)
        for null in ('null', '20 0 R', '999 0 R'):
            yield f'inherit-null-{key}-{null}', expected, document('/' + key + ' ' + null, parent,
                                                                  extra={20: b'null'})
    yield 'indirect-box-numbers', document(base), document('/MediaBox 20 0 R', extra={
        20: b'[21 0 R 21 0 R 22 0 R 23 0 R]', 21: b'0', 22: b'18', 23: b'12'})
    yield 'unit-upper-bound', document(base, stream=''), document(
        '/MediaBox [0 0 .00024 .00016] /UserUnit 75000', stream='')


def main():
    probe, oom = (str(Path(arg).resolve()) for arg in sys.argv[1:3])
    checks = allocations = rejections = references = 0
    if "--reference" in sys.argv:
        import fitz
        fitz.TOOLS.set_aa_level(0)
    with tempfile.TemporaryDirectory(prefix='cupidimage-userunit-') as directory:
        path = Path(directory) / 'case.pdf'
        for name, expected, changed in cases():
            outputs = []
            for data in (expected, changed):
                path.write_bytes(data)
                result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
                assert result.returncode == 0, (name, result.stderr)
                outputs.append(result.stdout)
            assert outputs[0] == outputs[1], (name, len(outputs[0]), len(outputs[1]),
                                               sum(a != b for a, b in zip(*outputs)))
            checks += 1
            if '--reference' in sys.argv and name in {
                '2-path-0', '2-image-0', '2-truetype-0', '2-cff-0',
                '2-type1-0', '2-type3-0', '2-pattern-0'
            }:
                with fitz.open(path) as reference:
                    raster = reference[0].get_pixmap(matrix=fitz.Matrix(96 / 72, 96 / 72), alpha=False)
                width, height, _ = struct.unpack('=III', outputs[1][:12])
                rgb = bytes(v for i, v in enumerate(outputs[1][12:]) if i % 4 != 3)
                assert (width, height) == (raster.width, raster.height) and rgb == raster.samples, name
                references += 1
            result = subprocess.run([oom, str(path), '0'], capture_output=True, text=True, timeout=60)
            assert result.returncode == 0, (name, result.stdout, result.stderr)
            allocations += int(result.stdout.split(': ')[1].split()[0])
        mixed = document('/MediaBox [0 0 18 12] /UserUnit 1', '/UserUnit 4', second=True,
                         extra={6: b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /UserUnit 2 /Contents 4 0 R >>'})
        path.write_bytes(mixed)
        for index, dimensions in ((1, (48, 32)), (0, (24, 16)), (1, (48, 32))):
            result = subprocess.run([probe, str(path), str(index)], capture_output=True, timeout=30)
            assert result.returncode == 0 and struct.unpack('=III', result.stdout[:12]) == (*dimensions, 2), result.stderr
            checks += 1
        for value in ('0', '-1', '75001', 'true', '[]', '(2)', '20 0 R'):
            path.write_bytes(document('/MediaBox [0 0 18 12] /UserUnit ' + value, extra={20: b'20 0 R'}))
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert result.returncode == 3, (value, result.stderr)
            rejections += 1
        for attrs in ('', '/MediaBox null', '/MediaBox [0 0 null 12]'):
            path.write_bytes(document(attrs))
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert result.returncode == 3, (attrs, result.stderr)
            rejections += 1
    print(f'PDF page units: {checks} rendering/geometry checks, {rejections} rejections and {allocations} allocation checks; {references} independent matches')


if __name__ == '__main__':
    main()
