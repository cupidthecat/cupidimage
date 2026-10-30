#!/usr/bin/env python3
"""Generate an original non-isolated group with a known backdrop/opacity result."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_groups import fixture
root=Path(__file__).resolve().parents[1]
asset='assets/pdf/reference/nonisolated-backdrop-group.pdf'
(root/asset).write_bytes(fixture('/A gs 0 0 1 rg 0 0 18 12 re f','0 1 0 rg 0 0 18 12 re f /A gs /G Do',group='<< /S /Transparency >>'))
pixels=bytes((0,191,64,255))*(24*16)
path=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(path.read_text())
rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
rows['cases'].append({'asset':asset,'width':24,'height':16,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
path.write_text(json.dumps(rows,indent=2)+'\n')
