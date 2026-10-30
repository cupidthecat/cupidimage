#!/usr/bin/env python3
"""Generate Macintosh post-table names from the specification transcription."""
import json
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
names=json.loads((root/'tests/fixtures/pdf-post-names.json').read_text())['names']
assert len(names)==258 and len(set(names))==258
value='/* Standard Macintosh glyph identifiers from the Apple TrueType post table.\n * Regenerate with scripts/generate_pdf_post_names.py. */\nstatic const char *const post_standard_names[258] = {\n'
value+=''.join('    '+json.dumps(name)+',\n' for name in names)+'};\n'
path=root/'src/formats/pdf/fonts/post_names.inc'
if '--check' in sys.argv:assert path.read_text()==value
else:path.write_text(value)
