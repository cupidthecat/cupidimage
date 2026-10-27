#!/usr/bin/env python3
"""Generate scalar glyph-name and PDF base-encoding lookups from pinned data."""
import json
from pathlib import Path
source=Path('third_party/adobe-glyph-list/glyphlist.txt').read_text()
license=Path('third_party/adobe-glyph-list/LICENSE.txt').read_text()
rows=[]
for line in source.splitlines():
    if not line or line.startswith('#'):continue
    name,value=line.split(';');values=value.split()
    rows.append((name,int(values[0],16) if len(values)==1 else 0,int(len(values)==1)))
header='/* Derived from the pinned Adobe Glyph List.\n'+''.join((' * '+line).rstrip()+'\n' for line in license.splitlines())+' */\n'
header+='static const struct {const char *name; uint32_t scalar; unsigned char single;} pdf_glyph_names[] = {\n'
header+=''.join('    {"%s", 0x%x, %d},\n'%row for row in rows)+'};\n'
Path('src/formats/pdf/fonts/glyph_names_data.h').write_text(header)
data=json.loads(Path('tests/fixtures/pdf-encodings.json').read_text())
header='/* ISO 32000-1:2008, Annex D.2/D.4, including duplicate-space/hyphen notes. */\n'
for key in ('StandardEncoding','MacRomanEncoding','WinAnsiEncoding','MacExpertEncoding'):
    header+='static const char *const pdf_'+key+'[256] = {\n'
    header+=''.join('    '+', '.join(json.dumps(n) for n in data[key][i:i+8])+',\n' for i in range(0,256,8))+'};\n'
Path('src/formats/pdf/fonts/encoding_data.h').write_text(header)
