#!/usr/bin/env python3
"""Verify sparse implicit text surfaces against eager and explicit geometry."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_embedded_fonts import fixture


def main():
    probe,allocator=[str(Path(arg).resolve()) for arg in sys.argv[1:3]]
    checks=allocations=0
    page={3:'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F 5 0 R >> /ExtGState << /Half << /ca .5 /CA .5 >> >> >> >>'}
    with tempfile.TemporaryDirectory(prefix='cupidimage-text-surfaces-') as directory:
        path=Path(directory)/'case.pdf'
        def render(content,large=False):
            path.write_bytes(fixture(content,extra=page if large else None))
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
            assert run.returncode==0,run.stderr
            return run.stdout
        def same(actual,expected,large=False):
            nonlocal checks
            assert render(actual,large)==render(expected,large),'text-surface pixels differ'
            checks+=1
        same('BT /F 12 Tf ET '*5000,'q Q',True)
        same('BT /F 30 Tf 3 Tr (AAAA) Tj ET '*5000,'q Q',True)
        objects=[];boxes=[]
        for row in range(22):
            for column in range(12):
                x,y=18+column*48,30+row*33
                objects.append(f'BT /F 30 Tf {x} {y} Td (A) Tj ET')
                boxes.append(f'{x} {y} 12 18 re f')
        same(' '.join(objects),' '.join(boxes),True)
        same('0 0 1 rg 0 0 120 90 re f 1 0 0 rg BT /F 30 Tf 3 Tr 20 30 Td (A) Tj 0 Tr (A) Tj ET',
             '0 0 1 rg 0 0 120 90 re f 1 0 0 rg 50 30 12 18 re f')
        same('BT /F 30 Tf 7 Tr 20 30 Td (A) Tj ET 1 0 0 rg 0 0 120 90 re f',
             '20 30 12 18 re W n 1 0 0 rg 0 0 120 90 re f')
        same('/Pattern cs BT /F 30 Tf 20 30 Td (A) Tj ET','q Q')
        # Fault runs also compare lazy surfaces with an eager-backdrop baseline.
        for content in ('BT /F 30 Tf 20 30 Td (A) Tj ET',
                        '0 0 1 rg 0 0 120 90 re f /Half gs 1 0 0 rg BT /F 30 Tf 20 30 Td (A) Tj -20 0 Td (A) Tj ET',
                        'BT /F 30 Tf 7 Tr 20 30 Td (A) Tj ET 1 0 0 rg 0 0 120 90 re f',
                        'BT /F 30 Tf ET BT /F 30 Tf 3 Tr (AA) Tj ET'):
            path.write_bytes(fixture(content))
            run=subprocess.run([allocator,str(path),'0'],capture_output=True,text=True,timeout=30)
            assert run.returncode==0,run.stdout+run.stderr
            allocations+=int(run.stdout.split(': ')[1].split(' checks')[0])
    print(f'PDF text surfaces: {checks} geometry checks; {allocations} allocation/eager comparisons')


if __name__=='__main__':
    main()
