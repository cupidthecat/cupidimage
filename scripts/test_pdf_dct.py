#!/usr/bin/env python3
"""Check native DCT samples before image Decode arrays and color conversion."""
from pathlib import Path
import base64
import zlib
import subprocess
import sys
import tempfile
from test_jpeg_decoder import jpeg, scan, segment, EOI
from test_pdf_images import fixture, image_stream
from test_pdf_pages import document
from test_pdf_lzw import literals
from audit_pdf_corpus import read_ppm


def image(components, progressive=False, marker=None):
    data = jpeg(components, progressive, dc=1)
    if marker is not None:
        data += segment(0xEE, b'Adobe\x00\x64\x00\x00\x00\x00' + bytes([marker]))
    data += scan(tuple(range(1, components + 1)), se=0 if progressive else 63)
    bits = ''.join(('01' if i % 2 == 0 else '00') + ('' if progressive else '0')
                   for i in range(components))
    bits += '1' * (-len(bits) % 8)
    entropy = bytes(int(bits[i:i+8], 2) for i in range(0, len(bits), 8)).replace(b'\xff', b'\xff\0')
    return data + entropy + EOI


def pipeline_cases():
    for components, space in ((1, 'DeviceGray'), (3, 'DeviceRGB'), (4, 'DeviceCMYK')):
        for progressive in (False, True):
            for marker in (None, 0, 1 if components == 3 else 2):
                for transform in (-1, 0, 1):
                    encoded = image(components, progressive, marker)
                    convert = marker != 0 if marker is not None else (transform if transform >= 0 else components == 3)
                    values = [129 if i % 2 == 0 else 127 for i in range(components)]
                    if components == 3 and convert:
                        values = [130, 129, 127]
                    elif components == 4 and convert:
                        values = [125, 126, 128, 127]
                    attrs = f'/Width 8 /Height 8 /BitsPerComponent 8 /ColorSpace /{space}'
                    if transform == -1:
                        params = 'null'
                    else:
                        params = f'<< /ColorTransform {transform} >>'
                    for prefix in ('none', 'hex', 'ascii85', 'flate', 'runlength', 'lzw', 'ascii85-flate'):
                        payload = encoded
                        names = ['/DCTDecode']
                        parms = [params]
                        if prefix == 'hex':
                            payload = encoded.hex().encode() + b'>'
                            names.insert(0, '/ASCIIHexDecode'); parms.insert(0, 'null')
                        elif prefix == 'ascii85':
                            payload = base64.a85encode(encoded) + b'~>'
                            names.insert(0, '/ASCII85Decode'); parms.insert(0, 'null')
                        elif prefix == 'flate':
                            payload = zlib.compress(encoded)
                            names.insert(0, '/FlateDecode'); parms.insert(0, 'null')
                        elif prefix == 'runlength':
                            payload = b''.join(bytes([len(encoded[i:i+128])-1]) + encoded[i:i+128]
                                               for i in range(0, len(encoded), 128)) + b'\x80'
                            names.insert(0, '/RunLengthDecode'); parms.insert(0, 'null')
                        elif prefix == 'lzw':
                            payload = literals(encoded, 1)
                            names.insert(0, '/LZWDecode'); parms.insert(0, 'null')
                        elif prefix == 'ascii85-flate':
                            payload = base64.a85encode(zlib.compress(encoded)) + b'~>'
                            names = ['/ASCII85Decode', '/FlateDecode', *names]; parms = ['null', 'null', *parms]
                        filtered = attrs + ' /Filter [' + ' '.join(names) + '] /DecodeParms [' + ' '.join(parms) + ']'
                        expected = fixture(bytes(values) * 64, attrs)
                        natural = document('/MediaBox [0 0 6 6] /Resources << /XObject << /Im0 5 0 R >> >>',
                                           stream='6 0 0 6 0 0 cm /Im0 Do', extra={5: image_stream(payload, filtered)})
                        yield f'{components}-{progressive}-{marker}-{transform}-{prefix}', fixture(payload, filtered), expected, natural, components != 4


def metadata_cases():
    attrs = '/Width 8 /Height 8 /BitsPerComponent 8 /ColorSpace /DeviceRGB'
    payload = image(3)
    for label, filters, parms, extra, values in (
        ('indirect-filter', '10 0 R', 'null', {10: b'/DCTDecode'}, (130, 129, 127)),
        ('indirect-parameters', '/DCTDecode', '11 0 R', {11: b'<< /ColorTransform 12 0 R >>', 12: b'0'}, (129, 127, 129)),
        ('indirect-array-members', '[10 0 R]', '[11 0 R]', {10: b'/DCTDecode', 11: b'<< /ColorTransform 12 0 R >>', 12: b'0'}, (129, 127, 129)),
        ('null-transform', '/DCTDecode', '<< /ColorTransform null >>', {}, (130, 129, 127)),
        ('undefined-transform', '/DCTDecode', '<< /ColorTransform 90 0 R >>', {}, (130, 129, 127)),
        ('null-parameters', '/DCTDecode', 'null', {}, (130, 129, 127)),
        ('undefined-parameters', '/DCTDecode', '90 0 R', {}, (130, 129, 127)),
        ('escaped-filter', '/DCT#44ecode', '<< /ColorTransform 0 >>', {}, (129, 127, 129)),
        ('unrelated-predictor-key', '/DCTDecode', '<< /ColorTransform 0 /Predictor (ignored) >>', {}, (129, 127, 129)),
        ('unrelated-cyclic-control', '/DCTDecode', '<< /ColorTransform 0 /EarlyChange 90 0 R >>', {90: b'90 0 R'}, (129, 127, 129)),
    ):
        filtered = attrs + ' /Filter ' + filters + ' /DecodeParms ' + parms
        natural = document('/MediaBox [0 0 6 6] /Resources << /XObject << /Im0 5 0 R >> >>',
                           stream='6 0 0 6 0 0 cm /Im0 Do', extra={5: image_stream(payload, filtered), **extra})
        yield label, fixture(payload, filtered, extra=extra), fixture(bytes(values) * 64, attrs), natural, True
    flate = zlib.compress(bytes((129, 127, 129)) * 64)
    filtered = attrs + ' /Filter /FlateDecode /DecodeParms << /ColorTransform (ignored) >>'
    natural = document('/MediaBox [0 0 6 6] /Resources << /XObject << /Im0 5 0 R >> >>',
                       stream='6 0 0 6 0 0 cm /Im0 Do', extra={5: image_stream(flate, filtered)})
    yield 'flate-unrelated-color-transform', fixture(flate, filtered), fixture(bytes((129, 127, 129)) * 64, attrs), natural, True
    for components, space, values in ((1, 'DeviceGray', (129,)),
                                      (3, 'DeviceRGB', (130, 129, 127)),
                                      (4, 'DeviceCMYK', (129, 127, 129, 127))):
        attrs = f'/Width 8 /Height 8 /BitsPerComponent 8 /ColorSpace /{space}'
        for progressive in (False, True):
            for inversion in (False, True):
                decode = ' /Decode [' + ('1 0 ' if inversion else '-.25 1.25 ') * components + ']'
                for soft_mask in (False, True):
                    mask = ' /SMask 7 0 R' if soft_mask else ''
                    extra = {7: image_stream(bytes([128]) * 64,
                                            '/Width 8 /Height 8 /BitsPerComponent 8 /ColorSpace /DeviceGray')}
                    actual = fixture(image(components, progressive), attrs + decode + mask + ' /Filter /DCTDecode', extra=extra)
                    expected = fixture(bytes(values) * 64, attrs + decode + mask, extra=extra)
                    yield f'native-decode-mask-{components}-{progressive}-{inversion}-{soft_mask}', actual, expected, actual, False


def check_pipelines(probe, oom=None, reference=False):
    checks = allocations = references = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-pdf-dct-') as tmp:
        path = Path(tmp) / 'case.pdf'
        for name, data, expected, natural, compare in (*pipeline_cases(), *metadata_cases()):
            results = []
            for pdf in (expected, data):
                path.write_bytes(pdf)
                run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
                assert run.returncode == 0, (name, run.stderr)
                results.append(run.stdout)
            assert results[0] == results[1], (name, 'native sample image differs')
            if reference and compare:
                path.write_bytes(natural)
                native = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
                assert native.returncode == 0, (name, native.stderr)
                prefix = Path(tmp) / 'reference'
                ref = subprocess.run(['pdftoppm', '-r', '96', '-aa', 'no', '-aaVector', 'no',
                                      '-singlefile', str(path), str(prefix)], capture_output=True, timeout=30)
                assert ref.returncode == 0, (name, ref.stderr)
                w, h, rgb = read_ppm(prefix.with_suffix('.ppm'))
                actual = bytes(v for i, v in enumerate(native.stdout[12:]) if i % 4 != 3)
                assert (w, h) == (8, 8) and rgb == actual, name
                references += 1
            if oom:
                path.write_bytes(data)
                faults = subprocess.run([oom, str(path), '0'], capture_output=True, text=True, timeout=60)
                assert faults.returncode == 0, (name, faults.stdout, faults.stderr)
                allocations += int(faults.stdout.split(': ')[1].split()[0])
            checks += 1
    print(f'PDF DCT pipelines: {checks} checks; {references} reference matches; {allocations} allocation checks')


def main():
    probe = str(Path(sys.argv[1]).resolve()); checks = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-dct-') as tmp:
        path = Path(tmp) / 'image.jpg'
        for components in (1, 2, 3, 4):
            for progressive in (False, True):
                for marker in (None, 0, 1, 2, 3):
                    for transform in (-1, 0, 1):
                        path.write_bytes(image(components, progressive, marker))
                        run = subprocess.run([probe, str(path), '8', '8', str(components), str(transform)],
                                             capture_output=True, timeout=30)
                        invalid = marker == 3 or (components == 3 and marker == 2) or (components == 4 and marker == 1)
                        assert run.returncode == (3 if invalid else 0), (components, progressive, marker, transform, run.stderr)
                        if not invalid:
                            convert = marker != 0 if marker is not None else (transform if transform >= 0 else components == 3)
                            values = [129 if i % 2 == 0 else 127 for i in range(components)]
                            if components == 3 and convert:
                                values = [130, 129, 127]
                            elif components == 4 and convert:
                                values = [125, 126, 128, 127]
                            assert run.stdout == bytes(values) * 64, (components, progressive, marker, transform, run.stdout[:8])
                        checks += 1
        for components in (3, 4):
            for progressive in (False, True):
                for marker in (0, 1, 2, 3):
                    for transform in (-1, 0, 1):
                        data = image(components, progressive)
                        data = data[:-2] + segment(0xEE, b'Adobe\x00\x64\x00\x00\x00\x00' + bytes([marker])) + EOI
                        path.write_bytes(data)
                        run = subprocess.run([probe, str(path), '8', '8', str(components), str(transform)], capture_output=True, timeout=30)
                        invalid = marker == 3 or (components == 3 and marker == 2) or (components == 4 and marker == 1)
                        assert run.returncode == (3 if invalid else 0), (components, progressive, marker, transform)
                        if not invalid:
                            values = ([129, 127, 129] if components == 3 else [129, 127, 129, 127]) if marker == 0 else ([130, 129, 127] if components == 3 else [125, 126, 128, 127])
                            assert run.stdout == bytes(values) * 64
                        checks += 1
        for progressive in (False, True):
            path.write_bytes(image(3, progressive)[:-2])
            assert subprocess.run([probe, str(path), '8', '8', '3', '-1'], capture_output=True).returncode == 3
            checks += 1
        path.write_bytes(image(3))
        for dimensions in (('9', '8', '3', '-1'), ('8', '9', '3', '-1'), ('8', '8', '4', '-1'),
                           ('8', '8', '3', '2'), ('100000', '100000', '3', '-1')):
            assert subprocess.run([probe, str(path), *dimensions], capture_output=True).returncode == 3
            checks += 1
    print(f'PDF native DCT samples: {checks} checks passed')
    positional = [arg for arg in sys.argv[2:] if not arg.startswith('--')]
    if positional:
        check_pipelines(str(Path(positional[0]).resolve()),
                        str(Path(positional[1]).resolve()) if len(positional) > 1 else None,
                        '--reference' in sys.argv)


if __name__ == '__main__':
    main()
