#!/usr/bin/env python3
"""Compare compiled metrics and built-in encodings with the pinned AFM resources."""
from pathlib import Path
import subprocess
import sys
from generate_pdf_standard_metrics import SOURCE, metrics

probe = str(Path(sys.argv[1]).resolve())
count = glyph_count = missing_count = 0
for path in sorted(SOURCE.glob('*.afm')):
    expected, encoding = metrics(path)
    result = subprocess.run([probe,path.stem],capture_output=True,text=True,timeout=10)
    assert result.returncode == 0,(path,result.stderr)
    actual, codes = {}, {}
    for line in result.stdout.splitlines():
        kind, key, value = line.split()
        if kind == 'glyph': actual[key] = int(value)
        else: codes[int(key)] = value
    assert actual == {'.notdef':0,**expected},path
    assert codes == {i:encoding.get(i,'.notdef') for i in range(256)},path
    count += len(expected) + 256
    outlines = subprocess.run([probe,path.stem,'--outlines'],capture_output=True,text=True,timeout=10)
    assert outlines.returncode == 0,(path,outlines.stderr)
    glyphs, missing = map(int, outlines.stdout.split())
    assert missing == (0 if path.stem in ('Symbol','ZapfDingbats') else 84 if path.stem.startswith('Courier') else 86)
    glyph_count += glyphs
    missing_count += missing
for unknown in ('Arial','ABCDEF+Helvetica','helvetica','Helvetica-Bold-Italic',''):
    assert subprocess.run([probe,unknown],capture_output=True).returncode == 2
print(f'Standard font resources: 14 fonts, {count} metric/encoding correspondences, 5 unknown-name checks passed')
print(f'Standard substitute outlines: {glyph_count} glyphs decoded; {missing_count} extended AFM names are absent from the compact arrays; extended coverage is tested separately')
metadata = subprocess.run([probe,'Helvetica','--metadata'],capture_output=True,text=True,timeout=10)
assert metadata.returncode == 0, metadata.stderr
print(metadata.stdout.strip())
