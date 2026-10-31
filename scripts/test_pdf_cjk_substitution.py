#!/usr/bin/env python3
"""Compare Chinese and Korean CID substitutes with pinned embedded font programs."""
import base64
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_cid_substitution import fixture
from generate_pdf_codepages import mappings
from test_pdf_pages import document
from test_pdf_type3 import stream

SOURCE = Path('third_party/cid-standard-fonts')
PREFIX = {'GB1': 'UniGB', 'CNS1': 'UniCNS', 'Korea1': 'UniKS'}
LEGACY = {'GB1': (936, ('GB-EUC', 'GBK-EUC', 'GBKp-EUC')),
          'CNS1': (950, ('ETen-B5', 'ETenms-B5')),
          'Korea1': (949, ('KSCms-UHC', 'KSCms-UHC-HW'))}
WORDS = {'GB1': 'A中文（）', 'CNS1': 'A繁體中文（）', 'Korea1': 'A한국어（）'}


def main():
    probe, allocator = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    manifest = json.loads((SOURCE / 'manifest.json').read_text())['files']
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupid-cjk-substitutes-') as directory:
        path = Path(directory) / 'case.pdf'

        def render(data, hinted=False, reject=False):
            nonlocal checks
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), '0', *(['1'] * 5 if hinted else [])],
                                    capture_output=True, timeout=90)
            assert (result.returncode != 0) == reject, result.stderr
            checks += 1
            return result.stdout

        for index, row in enumerate(manifest[2:], 2):
            serif = bool(index % 2)
            suffix = row['face'].split('_', 1)[1]
            reference = json.loads(Path(f'tests/fixtures/pdf-cid-substitute-mapping-{suffix}.json').read_text())
            assert reference['cid_equals_gid']
            oracle = {r[0]: r[1:] for r in struct.iter_unpack(
                '>I4H', zlib.decompress(base64.b64decode(reference['records_zlib'])))}
            compressed = zlib.compress((SOURCE / row['file']).read_bytes())
            base = dict(serif=serif, ordering='(' + row['ordering'] + ')',
                        supplement=str(row['supplement']), name='MissingCollectionFont')

            def packet(embedded=False, **kwargs):
                options = {**base, **kwargs}
                if embedded:
                    options['descriptor'] = options.get('descriptor', '') + ' /FontFile3 9 0 R'
                    options['extra'] = {9: stream(compressed, '/Subtype /OpenType /Filter /FlateDecode'),
                                        **options.get('extra', {})}
                return fixture(**options)

            scalars = list(map(ord, WORDS[row['ordering']]))
            for vertical in (False, True):
                direction = 'V' if vertical else 'H'
                gids = ''.join(f'{oracle[c][int(serif) * 2]:04x}' for c in scalars)
                content = f'BT /F 15 Tf 50 85 Td <{gids}> Tj ET'
                for hinted in (False, True):
                    options = dict(encoding='/Identity-' + direction, content=content)
                    assert render(packet(**options), hinted) == render(packet(True, **options), hinted)
                # Type 0 Unicode CMaps remain CID-driven, including their vertical mapping.
                options = dict(encoding='/' + PREFIX[row['ordering']] + '-UTF16-' + direction,
                               content='BT /F 15 Tf 50 85 Td <' + ''.join(map(chr, scalars)).encode('utf-16-be').hex() + '> Tj ET')
                assert render(packet(**options)) == render(packet(True, **options))
                for encoding in ('UCS2', 'UTF16'):
                    selected = scalars[:]
                    supplementary = next((c for c in oracle if c > 65535), None)
                    if encoding == 'UTF16' and supplementary is not None:
                        selected.append(supplementary)
                    encoded = ''.join(map(chr, selected)).encode('utf-16-be').hex()
                    gids = ''.join(f'{oracle[c][int(serif) * 2 + int(vertical)]:04x}' for c in selected)
                    for mode in (0, 4):
                        start = f'.6 w BT /F 12 Tf {mode} Tr .5 Tc 90 Tz 1 Ts 1 .1 -.05 1 50 96 Tm '
                        end = ' Tj ET' + (' .2 .4 .8 rg 0 0 240 110 re f' if mode else '')
                        actual = packet(subtype='CIDFontType2',
                                        encoding='/' + PREFIX[row['ordering']] + '-' + encoding + '-' + direction,
                                        content=start + '<' + encoded + '>' + end,
                                        attributes='/ToUnicode 12 0 R',
                                        extra={12: stream('1 beginbfchar <0041> <005a> endbfchar')})
                        expected = packet(True, encoding='/Identity-' + direction,
                                          content=start + '<' + gids + '>' + end)
                        assert render(actual, mode == 4) == render(expected, mode == 4), (row['face'], encoding, vertical, mode)
            codepage, families = LEGACY[row['ordering']]
            reverse = {}
            for code, scalar in mappings(codepage):
                reverse.setdefault(scalar, code)
            selected = [c for c in scalars + [0x24, 0x5c, 0x20ac] if c in reverse]
            encoded = b''.join(reverse[c].to_bytes(1 if reverse[c] < 256 else 2, 'big')
                               for c in selected).hex()
            for family in families:
                for vertical in (False, True):
                    direction = 'V' if vertical else 'H'
                    gids = ''.join(f'{oracle.get(c, (0, 0, 0, 0))[int(serif) * 2 + int(vertical)]:04x}'
                                   for c in selected)
                    start = 'BT /F 9 Tf .25 Tc 95 Tz 50 104 Td '
                    actual = packet(subtype='CIDFontType2', encoding='/' + family + '-' + direction,
                                    content=start + '<' + encoded + '> Tj ET')
                    expected = packet(True, encoding='/Identity-' + direction,
                                      content=start + '<' + gids + '> Tj ET')
                    for hinted in (False, True):
                        assert render(actual, hinted) == render(expected, hinted), (row['face'], family, vertical)
            path.write_bytes(actual)
            result = subprocess.run([allocator, str(path), '0'], capture_output=True, text=True,
                                    check=True, timeout=180)
            allocations += int(result.stdout.split('checks')[0].split()[-1])
            unsupported = render(packet(ordering='(Unknown)'))
            assert render(packet(supplement=str(row['supplement'] + 1))) == unsupported
            for mismatch in ('UniJIS-UTF16-H', '90ms-RKSJ-H'):
                options = dict(subtype='CIDFontType2', encoding='/' + mismatch)
                assert render(packet(**options)) == render(packet(ordering='(Unknown)', **options))
            # A supplied but unusable font must not be replaced by the bundled program.
            render(packet(descriptor='/FontFile3 9 0 R',
                          extra={9: stream('broken', '/Subtype /Unsupported')}), reject=True)
            path.write_bytes(packet())
            result = subprocess.run([allocator, str(path), '0'], capture_output=True, text=True,
                                    check=True, timeout=180)
            allocations += int(result.stdout.split('checks')[0].split()[-1])
        # All eight original programs fit the unchanged shared 64 MiB font budget.
        objects, names, content = {}, [], []
        for index, row in enumerate(manifest):
            n = 10 + index * 3
            names.append(f'/F{index} {n} 0 R')
            objects[n] = f'<< /Type /Font /Subtype /Type0 /Encoding /Identity-H /DescendantFonts [{n+1} 0 R] >>'
            objects[n+1] = f'<< /Type /Font /Subtype /CIDFontType0 /BaseFont /Missing /FontDescriptor {n+2} 0 R /CIDSystemInfo << /Registry (Adobe) /Ordering ({row["ordering"]}) /Supplement {row["supplement"]} >> >>'
            objects[n+2] = f'<< /Type /FontDescriptor /Flags {6 if index % 2 else 4} >>'
            content.append(f'BT /F{index} 12 Tf {index * 24 + 10} 30 Td <0041> Tj ET')
        render(document('/MediaBox [0 0 240 60] /Resources << /Font << ' + ' '.join(names) + ' >> >>',
                        stream=' '.join(content), extra=objects))
    print(f'PDF CJK substitutes: {checks} render checks; {allocations} allocation checks')


if __name__ == '__main__':
    main()
