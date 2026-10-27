#!/usr/bin/env python3
"""Generate font-name escaping and literal-line-ending regression assets."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_content_syntax import newline_font
from test_pdf_text_knockout import fixture
from test_pdf_type3 import stream

root=Path(__file__).resolve().parents[1]
path=root/'tests/fixtures/pdf-image-reference.json'
rows=json.loads(path.read_text())
cases=[
    ('escaped-content-font.pdf',fixture('1 0 0 rg BT /#46 1 Tf (A) Tj ET')),
    ('literal-text-crlf.pdf',fixture('1 0 0 rg BT /F 1 Tf (\r\n) Tj ET',
        extra={5:newline_font(),7:stream('18 0 d0')})),
]
for name,pdf in cases:
    asset='assets/pdf/reference/'+name
    (root/asset).write_bytes(pdf)
    pixels=bytes((255,0,0,255))*(24*16)
    rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':24,'height':16,
                         'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
path.write_text(json.dumps(rows,indent=2)+'\n')
