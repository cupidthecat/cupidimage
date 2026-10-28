#!/usr/bin/env python3
"""Check compatibility and marked-content scope without executing unknown operators."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_type3 import stream, fixture as type3_fixture


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = allocations = 0
    paint = '1 0 0 rg 0 0 18 12 re f'
    def fixture(content, extra=None, resources=''):
        return document('/MediaBox [0 0 18 12] /Resources << '+resources+' >>',
                        stream=content, extra=extra)
    with tempfile.TemporaryDirectory(prefix='cupidimage-content-scopes-') as directory:
        path = Path(directory)/'case.pdf'
        def render(data, reject=False):
            nonlocal checks
            path.write_bytes(data)
            run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=15)
            assert run.returncode == (3 if reject else 0), run.stderr
            checks += 1
            return run.stdout
        expected = render(fixture(paint))
        good = ['BX FutureOp EX', 'BX BX FutureOp EX FutureOp EX',
                'q BX Q FutureOp EX', 'BX q EX Q',
                '/Tag MP', '/Tag << /MCID 2 >> DP', '/Tag /Props DP',
                '/Tag BMC EMC', '/Tag << /MCID 2 >> BDC EMC', '/Tag /Props BDC EMC',
                'q /Tag BMC Q EMC', '/Outer BMC /Inner BMC EMC EMC',
                '/Tag BMC BX FutureOp EX EMC',
                '/RelativeColorimetric ri', '/Perceptual ri', '/UnknownIntent ri',
                'BX '+('x'*4096)+' EX', 'BX '+('true '*64)+'FutureOp EX',
                'BX '*64+'FutureOp '+'EX '*64,
                '/Tag BMC '*64+'EMC '*64]
        for operands in ('123', '/Name', '(text)', '<4142>', '[1 (nested)]',
                         '<< /Private [1 2] >>', 'true', 'false', 'null',
                         'true null false [1] << /A 2 >> /Name (text)'):
            good.append('BX '+operands+' FutureOp EX')
        for content in good:
            assert render(fixture(content+' '+paint)) == expected, content
        bad = ['FutureOp', 'BX FutureOp EX FutureOp', 'EX', 'BX', 'BX BX EX',
               '1 BX EX', 'BX 1 EX', 'BX false 1 w EX', 'BX null 1 w EX',
               'BX -1 i EX', 'BX (bad) Tj EX', 'BX [bad EX',
               'BX '+('false '*65)+'FutureOp EX', 'BX '*65+'EX '*65,
               'EMC', '/Tag BMC', '/Tag BMC 1 EMC', '/Tag BMC EMC EMC',
               'BMC EMC', '123 BMC EMC', '/Tag 123 BDC EMC', '/Tag BDC EMC',
               'MP', '1 MP', '/Tag true DP', '/Tag (text) DP',
               '/Tag BMC '*65+'EMC '*65, 'ri', '1 ri', '/A /B ri',
               '123', 'null', '(trailing)', '[1]', '<< /A 1 >>',
               ']', ')', '>', '}', '{', 'BX ] EX']
        for content in bad:
            render(fixture(content+' '+paint if content not in ('123', 'null', '(trailing)', '[1]', '<< /A 1 >>') else paint+' '+content), True)
        # Contents arrays form one stream; scopes span array boundaries.
        page = '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents [10 0 R 11 0 R 12 0 R] >>'
        assert render(fixture('', {3:page, 10:stream('BX'),
                                  11:stream('true [1] FutureOp'),
                                  12:stream('EX '+paint)})) == expected
        resources = '/XObject << /X 10 0 R >>'
        form_attrs = '/Type /XObject /Subtype /Form /BBox [0 0 18 12]'
        # The caller's compatibility/marked-content depth does not leak into a Form.
        render(fixture('BX /X Do EX', {10:stream('FutureOp', form_attrs)}, resources), True)
        render(fixture('/Tag BMC /X Do EMC', {10:stream('EMC', form_attrs)}, resources), True)
        assert render(fixture('BX /X Do EX '+paint,
                              {10:stream('BX FutureOp EX', form_attrs)}, resources)) == expected
        render(fixture('/X Do '+paint, {10:stream('BX', form_attrs)}, resources), True)
        for prefix in ('', 'BX '):
            content = prefix+'BT /F 1 Tf (A) Tj ET'+(' EX' if prefix else '')
            render(type3_fixture(content, programs=['600 0 d0 FutureOp', '500 0 d0']), True)
            render(type3_fixture(content, programs=['600 0 d0 BX FutureOp EX', '500 0 d0']))
        if len(sys.argv) > 2:
            allocator = str(Path(sys.argv[2]).resolve())
            for content, reject in [('BX (text) [1] << /A 2 >> FutureOp EX '+paint, False),
                                    ('/Tag << /MCID 1 >> BDC '+paint+' EMC', False),
                                    ('(text) FutureOp', True), ('BX (text)', True)]:
                path.write_bytes(fixture(content))
                run = subprocess.run([allocator, str(path), str(int(reject))],
                                     capture_output=True, text=True, timeout=30)
                assert run.returncode == 0, run.stdout+run.stderr
                allocations += int(run.stdout.split(': ')[1].split(' checks')[0])
    print(f'PDF content scopes: {checks} checks passed; {allocations} allocation checks')


if __name__ == '__main__':
    main()
