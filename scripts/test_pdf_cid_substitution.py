#!/usr/bin/env python3
"""Check CID-keyed Japanese substitutes against the same embedded font bytes."""
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_type3 import stream

SOURCE = Path('third_party/cid-standard-fonts')
FONTS = {False: (SOURCE / 'HaranoAjiGothic-Regular.otf').read_bytes(),
         True: (SOURCE / 'HaranoAjiMincho-Regular.otf').read_bytes()}
COMPRESSED = {key: zlib.compress(value) for key, value in FONTS.items()}


def fixture(serif=False, embedded=False, content=None, encoding='/Identity-H',
            registry='(Adobe)', ordering='(Japan1)', supplement='7', flags=None,
            name='MissingJapanese', subtype='CIDFontType0', metrics='/DW 1000',
            descriptor='', attributes='', extra=None):
    if flags is None:
        flags = 6 if serif else 4
    objects = {
        5: f'<< /Type /Font /Subtype /Type0 /BaseFont /{name} /Encoding {encoding} '
           f'/DescendantFonts [6 0 R] {attributes} >>',
        6: f'<< /Type /Font /Subtype /{subtype} /BaseFont /{name} /FontDescriptor 8 0 R '
           f'/CIDSystemInfo << /Registry {registry} /Ordering {ordering} '
           f'/Supplement {supplement} >> {metrics} >>',
        8: f'<< /Type /FontDescriptor /FontName /{name} /Flags {flags} '
           f'/FontBBox [-1000 -1000 2000 2000] /ItalicAngle 0 /Ascent 880 /Descent -120 '
           f'/CapHeight 700 /StemV 80 {descriptor}' +
           (' /FontFile3 9 0 R' if embedded else '') + ' >>',
    }
    if embedded:
        objects[9] = stream(COMPRESSED[serif], '/Subtype /OpenType /Filter /FlateDecode')
    objects.update(extra or {})
    return document('/MediaBox [0 0 240 110] /Resources << /Font << /F 5 0 R >> >>',
                    stream=content or 'BT /F 24 Tf 20 45 Td <0cd40e8a07a0> Tj ET', extra=objects)


def shared_fixture(explicit=False):
    objects = {}
    resources = []
    commands = ['BT']
    x = 10
    for i in range(24):
        number = 10 + i * 3
        width = 500 if i % 2 else 1000
        if not explicit or i == 0:
            resources.append(f'/F{i} {number} 0 R')
            objects[number] = f'<< /Type /Font /Subtype /Type0 /BaseFont /SharedJapanese '
            objects[number] += f'/Encoding /Identity-H /DescendantFonts [{number + 1} 0 R] >>'
            objects[number + 1] = f'<< /Type /Font /Subtype /CIDFontType0 /BaseFont /SharedJapanese '
            objects[number + 1] += f'/FontDescriptor {number + 2} 0 R /DW {width} '
            objects[number + 1] += '/CIDSystemInfo << /Registry (Adobe) /Ordering (Japan1) /Supplement 7 >> >>'
            objects[number + 2] = '<< /Type /FontDescriptor /Flags 4 >>'
        if explicit:
            commands.append(f'/F0 10 Tf 1 0 0 1 {x} 40 Tm <0cd4> Tj')
        else:
            commands.append((f'/F{i} 10 Tf ' + ('10 40 Td ' if not i else '') + '<0cd4> Tj'))
        x += width / 100
    commands.append('ET')
    return document('/MediaBox [0 0 240 110] /Resources << /Font << ' +
                    ' '.join(resources) + ' >> >>', stream=' '.join(commands), extra=objects)


def main():
    probe, allocator = [str(Path(arg).resolve()) for arg in sys.argv[1:3]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-cid-substitute-') as tmp:
        path = Path(tmp) / 'case.pdf'

        def render(data, reject=False, hinted=False):
            nonlocal checks
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), '0', *(['1'] * 5 if hinted else [])],
                                    capture_output=True, timeout=45)
            assert (result.returncode != 0) == reject, (result.returncode, result.stderr)
            checks += 1
            return result.stdout

        def fault(data):
            nonlocal allocations
            path.write_bytes(data)
            result = subprocess.run([allocator, str(path), '0'], capture_output=True,
                                    text=True, timeout=180)
            assert result.returncode == 0, (result.stdout, result.stderr)
            allocations += int(result.stdout.split(': ')[1].split()[0])

        for serif in (False, True):
            baseline = render(fixture(serif=serif, embedded=True))
            assert render(fixture(serif=serif)) == baseline
            for mode in range(8):
                for vertical in (False, True):
                    content = f'.8 w BT /F 22 Tf {mode} Tr .5 Tc 90 Tz 2 Ts 1 .15 -.1 1 55 80 Tm '
                    content += '[<0cd4> 90 <0e8a07a0>] TJ ET'
                    if mode >= 4:
                        content += ' .2 .4 .8 rg 0 0 240 110 re f'
                    kwargs = dict(serif=serif, content=content,
                                  encoding='/Identity-V' if vertical else '/Identity-H',
                                  metrics='/DW 700 /W [3284 [620]] /DW2 [880 -1000] '
                                          '/W2 [3284 [-950 310 850]]')
                    for hinted in (False, True):
                        assert render(fixture(**kwargs), hinted=hinted) == render(
                            fixture(embedded=True, **kwargs), hinted=hinted), (serif, mode, vertical)
            for encoding, text in (('/90ms-RKSJ-H', '<93fa967b8cea>'),
                                   ('/90ms-RKSJ-V', '<93fa967b8cea>'),
                                   ('/UniJIS-UTF16-H', '<65e5672c8a9e>')):
                kwargs = dict(serif=serif, encoding=encoding,
                              content=f'BT /F 20 Tf 70 75 Td {text} Tj ET')
                assert render(fixture(**kwargs)) == render(fixture(embedded=True, **kwargs))
            for supplement in ('0', '3', '7', '11 0 R'):
                assert render(fixture(serif=serif, supplement=supplement, extra={11: '7'})) == baseline
            for registry, ordering, extra in (
                ('<41646f6265>', '<4a6170616e31>', {}),
                ('(Ado\\142e)', '(Japan\\061)', {}),
                ('11 0 R', '12 0 R', {11: '(Adobe)', 12: '(Japan1)'}),
            ):
                assert render(fixture(serif=serif, registry=registry, ordering=ordering,
                                      extra=extra)) == baseline
            # Extraction text must not change CID-selected glyphs.
            assert render(fixture(serif=serif, attributes='/ToUnicode 12 0 R', extra={
                12: stream('1 beginbfchar <0cd4> <0041> endbfchar')})) == baseline
            fault(fixture(serif=serif))
        serif = render(fixture(serif=True, embedded=True))
        for name in ('HeiseiMin-W3', 'Ryumin-Light', 'MS-Mincho', 'ABCDEF+HeiseiMin-W3'):
            assert render(fixture(name=name, flags=4)) == serif
        unsupported = render(fixture(ordering='(Unknown)'))
        for kwargs in ({'registry': '(Other)'}, {'ordering': '(GB1)'}, {'ordering': '(Identity)'},
                       {'supplement': '8'}, {'supplement': 'null'}, {'subtype': 'CIDFontType2'}):
            assert render(fixture(**kwargs)) == unsupported, kwargs
        render(fixture(registry='11 0 R', extra={11: '(Adobe) (Other)'}), reject=True)
        for supplement in ('-1', '1.5', 'true', '11 0 R'):
            render(fixture(supplement=supplement, extra={11: '7 0'}), reject=True)
        for flags in ('-1', '4.5', '4294967296', '11 0 R'):
            render(fixture(flags=flags, extra={11: '4 0'}), reject=True)
        for key in ('FontFile', 'FontFile2', 'FontFile3'):
            render(fixture(descriptor=f'/{key} 11 0 R',
                           extra={11: stream('broken', '/Subtype /Unsupported')}), reject=True)
        # A shared outline program must not share per-resource PDF widths.
        assert render(shared_fixture()) == render(shared_fixture(explicit=True))
        fault(shared_fixture())
    print(f'PDF CID substitutes: {checks} render checks; {allocations} allocation checks')


if __name__ == '__main__':
    main()
