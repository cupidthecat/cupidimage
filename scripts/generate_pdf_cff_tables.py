#!/usr/bin/env python3
"""Generate CFF standard lookup tables from the specification transcription."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
data=json.loads((root/'tests/fixtures/pdf-cff-tables.json').read_text())
assert len(data['strings'])==391
out=['/* CFF standard tables, Adobe Technical Note 5176, Appendices A-C.\n * Regenerate with scripts/generate_pdf_cff_tables.py. */\n','static const char *const cff_strings[391] = {\n']
out+=['    '+json.dumps(n)+',\n' for n in data['strings']];out+=['};\n']
for name,size in (('expert_charset',166),('expert_subset_charset',87),('standard_encoding',256),('expert_encoding',256)):
 vals=data[name];assert len(vals)==size and all(0<=v<391 for v in vals)
 out+=[f'static const unsigned short cff_{name}[{len(vals)}] = {{\n'];out+=['    '+','.join(str(v) for v in vals[i:i+16])+',\n' for i in range(0,len(vals),16)];out+=['};\n']
(root/'src/formats/pdf/fonts/cff_tables.inc').write_text(''.join(out))
