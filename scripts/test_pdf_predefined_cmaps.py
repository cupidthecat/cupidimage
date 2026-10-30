#!/usr/bin/env python3
"""Check compiled standard CMaps against the pinned resource definitions."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from functools import lru_cache
from test_pdf_cmaps import cmap_pdf
from test_pdf_embedded_fonts import fixture
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm

SOURCE = Path('third_party/adobe-cmaps')


@lru_cache(None)
def resource(name):
    data = (SOURCE / 'CMap' / name).read_text()
    parent = re.search(r'^/(\S+) usecmap$', data, re.M)
    if parent:
        base = resource(parent[1])
        spaces, mapping, notdef = list(base[0]), dict(base[1]), dict(base[2])
    else:
        spaces, mapping, notdef = [], {}, {}
    for match in re.finditer(r'\d+ begin(codespacerange|cidrange|cidchar|notdefrange|notdefchar)\s+(.*?)\s+end\1', data, re.S):
        kind, body = match.groups()
        for row in body.strip().splitlines():
            parts = row.split()
            low = bytes.fromhex(parts[0].strip('<>'))
            if kind.endswith('char'):
                high, cid = low, int(parts[1])
            else:
                high = bytes.fromhex(parts[1].strip('<>'))
                cid = 0 if kind == 'codespacerange' else int(parts[2])
            if kind == 'codespacerange':
                spaces.append((low, high))
            else:
                table = notdef if kind.startswith('notdef') else mapping
                start, end = int.from_bytes(low, 'big'), int.from_bytes(high, 'big')
                for code in range(start, end + 1):
                    table[code.to_bytes(len(low), 'big')] = cid + (0 if table is notdef else code-start)
    return spaces, mapping, notdef


def valid(code, spaces):
    return any(len(code) == len(lo) and all(a <= b <= c for a,b,c in zip(lo,code,hi))
               for lo,hi in spaces)


def main():
    decoder = str(Path(sys.argv[1]).resolve())
    probe = str(Path(sys.argv[2]).resolve())
    entries = json.loads((SOURCE / 'manifest.json').read_text())['maps']
    checked = renders = refs = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-predefined-') as tmp:
        path = Path(tmp) / 'case.pdf'
        for entry in entries:
            name = entry['name']
            assert hashlib.sha256((SOURCE / 'CMap' / name).read_bytes()).hexdigest() == entry['sha256']
            spaces, mapping, notdef = resource(name)
            # Check every defined character, including inherited overrides.
            codes = sorted(code for code in mapping.keys() | notdef.keys() if valid(code, spaces))
            path.write_bytes(cmap_pdf('q Q', '', extra={10:('/'+name).encode()}))
            for start in range(0, len(codes), 1800):
                batch = codes[start:start+1800]
                run = subprocess.run([decoder,str(path),b''.join(batch).hex()], capture_output=True,text=True,timeout=20)
                assert run.returncode == 0, (name,run.returncode,run.stderr)
                lines = run.stdout.splitlines()
                assert lines[0] == 'mode '+str(int(name == 'V' or name.endswith('-V'))), name
                expected = [(len(code),int.from_bytes(code,'big'),mapping.get(code,notdef.get(code,0)),notdef.get(code,0),1) for code in batch]
                actual = [tuple(map(int,line.split())) for line in lines[1:]]
                assert actual == expected, (name,start, next(((a,b) for a,b in zip(actual,expected) if a!=b),None))
                checked += len(batch)
        for name, code in [('90ms-RKSJ-H','8141'),('90ms-RKSJ-V','8141'),
                           ('GB-EUC-H','A1A1'),('B5pc-H','A140'),('KSC-EUC-H','A1A1'),
                           ('UniJIS-UTF16-H','D840DC0B')]:
            cid = resource(name)[1][bytes.fromhex(code)]
            gidmap = bytearray((cid+1)*2); gidmap[-1] = 1
            vertical = name.endswith('-V')
            position = '60 69' if vertical else '20 30'
            ordering = 'GB1' if name.startswith('GB') else 'CNS1' if name.startswith('B5') else 'Korea1' if name.startswith('KSC') else 'Japan1'
            extra = {5:('<< /Type /Font /Subtype /Type0 /Encoding /'+name+' /DescendantFonts [6 0 R] >>').encode(),
                     6:('<< /Type /Font /Subtype /CIDFontType2 /BaseFont /CupidImageTestShapes /FontDescriptor 8 0 R /CIDSystemInfo << /Registry (Adobe) /Ordering ('+ordering+') /Supplement 7 >> /CIDToGIDMap 11 0 R /DW2 [900 -1000] >>').encode(),
                     11:stream(bytes(gidmap))}
            pdf = fixture(f'BT /F 30 Tf {position} Td <{code}> Tj ET',cid=True,extra=extra)
            path.write_bytes(pdf)
            actual = subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            path.write_bytes(fixture(f'BT /F 30 Tf {position} Td <0001> Tj ET',cid=True,vertical=vertical,metrics="/DW2 [900 -1000]"))
            expected = subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert actual.returncode == expected.returncode == 0 and actual.stdout == expected.stdout, name
            renders += 1
            if '--reference' in sys.argv:
                path.write_bytes(pdf); prefix=Path(tmp)/'reference'
                run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=15)
                assert run.returncode==0,run.stderr
                w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                pixels=bytes(v for i,v in enumerate(actual.stdout[12:]) if i%4!=3)
                assert (w,h)==(160,120) and pixels==rgb,(name,'reference pixels differ')
                refs += 1
    print(f'Predefined CMaps: {len(entries)} resources, {checked} character mappings, {renders} renders; {refs} independent comparisons')


if __name__ == '__main__':
    main()
