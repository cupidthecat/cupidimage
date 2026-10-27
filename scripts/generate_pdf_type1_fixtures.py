#!/usr/bin/env python3
"""Generate original binary Type 1 fixtures using the specified cipher."""
from pathlib import Path
from test_pdf_type1 import fixture
from test_pdf_type1_charstrings import WIDTH,BOX,command
from test_pdf_type1_render import CURVE
root=Path(__file__).resolve().parents[1]
fonts={'test_type1_font':fixture(glyphs=[('.notdef',WIDTH+b'\x0e'),('A',BOX),('B',CURVE)])}
accent=command([100,200],13)+command([0,0],21)+command([100],6)+command([100],7)+command([-100],6)+b'\x09\x0e'
seac=WIDTH+command([100,125,600,65,194],(12,6))
fonts['test_type1_composite']=fixture(glyphs=[('.notdef',WIDTH+b'\x0e'),('A',BOX),('acute',accent),('Aacute',seac)])
output='/* Original geometric Type 1 fonts; regenerate with generate_pdf_type1_fixtures.py. */\n'
for name,(raw,l1,l2) in fonts.items():
    output+=f'static const unsigned {name}_clear = {l1}, {name}_encrypted = {l2};\n'
    output+=f'static const unsigned char {name}[] = {{\n'
    output+=''.join('    '+','.join(str(v) for v in raw[i:i+24])+',\n' for i in range(0,len(raw),24))+'};\n'
(root/'tests/fixtures/pdf-type1.inc').write_text(output)
