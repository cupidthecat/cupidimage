#!/usr/bin/env python3
"""Generate outline-less clipping and encoded-space ink regressions."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_empty_glyphs import fixture, FILL


def main():
    root=Path(__file__).resolve().parents[1]
    reference=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(reference.read_text())
    reset='1 0 0 1 -1.125 0 cm '
    cases=(('empty-glyph-preserves-clip.pdf',fixture(reset+'0 0 60 90 re W n BT /F 30 Tf 7 Tr (   ) Tj ET '+FILL),lambda x,y:x<80),
           ('space-code-outline-clip.pdf',fixture(reset+'BT /F 30 Tf 7 Tr 20 30 Td ( ) Tj ET '+FILL,encoding='<< /Differences [32 /A] >>'),lambda x,y:27<=x<43 and 56<=y<80))
    for name,data,red in cases:
        path=root/'assets/pdf/reference'/name;path.write_bytes(data)
        pixels=bytes(v for y in range(120) for x in range(160) for v in ((255,0,0,255) if red(x,y) else (255,255,255,255)))
        asset=str(path.relative_to(root));rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
        rows['cases'].append({'asset':asset,'width':160,'height':120,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
    reference.write_text(json.dumps(rows,indent=2)+'\n')
    print('Generated two glyph-clipping assets and analytic references')


if __name__=='__main__':main()
