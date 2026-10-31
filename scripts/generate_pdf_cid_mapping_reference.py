#!/usr/bin/env python3
"""Generate an independent fontTools cmap/vertical oracle for pinned CID faces."""
import base64
import hashlib
import json
from pathlib import Path
import struct
import zlib
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'third_party/cid-standard-fonts'


def glyph(font, scalar):
    for platform, encoding in ((3, 10), (3, 1), (0, None)):
        for sub in font['cmap'].tables:
            if sub.platformID == platform and (encoding is None or sub.platEncID == encoding):
                result = font.getGlyphID(sub.cmap.get(scalar, '.notdef'))
                if result:
                    return result
    return 0


def generate(names, suffix):
    fonts = [TTFont(SOURCE / name) for name in names]
    for font in fonts:
        charset = font['CFF '].cff.topDictIndex[0].charset
        assert charset[0] == '.notdef'
        assert all(name == f'cid{i:05d}' for i, name in enumerate(charset) if i)
    scalars = sorted({scalar for font in fonts for sub in font['cmap'].tables
                      if sub.isUnicode() for scalar in sub.cmap})
    vertical = []
    for font in fonts:
        gsub = font['GSUB'].table
        records = [r for r in gsub.FeatureList.FeatureRecord if r.FeatureTag == 'vrt2']
        assert records and all(r.Feature.LookupListIndex == records[0].Feature.LookupListIndex for r in records)
        assert len(records[0].Feature.LookupListIndex) == 1
        lookup = gsub.LookupList.Lookup[records[0].Feature.LookupListIndex[0]]
        assert lookup.LookupType == 1 and lookup.LookupFlag == 0 and len(lookup.SubTable) == 1
        vertical.append({font.getGlyphID(a): font.getGlyphID(b)
                         for a, b in lookup.SubTable[0].mapping.items()})
    packed = bytearray()
    for scalar in scalars:
        values = []
        for font, variants in zip(fonts, vertical):
            normal = glyph(font, scalar)
            values.extend((normal, variants.get(normal, normal)))
        packed += struct.pack('>I4H', scalar, *values)
    result = {'source': 'Independent fontTools cmap and vrt2 decoding; original font bytes are unchanged.',
              'layout': 'big-endian u32 scalar, u16 Gothic horizontal/vertical, u16 Mincho horizontal/vertical',
              'cid_equals_gid': True,
              'fonts': [{'file': name, 'sha256': hashlib.sha256((SOURCE / name).read_bytes()).hexdigest(),
                         'glyphs': len(font.getGlyphOrder())} for name, font in zip(names, fonts)], 'count': len(scalars),
              'records_zlib': base64.b64encode(zlib.compress(packed, 9)).decode()}
    path = ROOT / ('tests/fixtures/pdf-cid-substitute-mapping' + suffix + '.json')
    path.write_text(json.dumps(result, indent=2) + '\n')
    print(f'Independent CID mapping oracle{suffix}: {len(scalars)} Unicode scalars across two faces')
    for font in fonts:
        font.close()


if __name__ == '__main__':
    manifest = json.loads((SOURCE / 'manifest.json').read_text())
    for start in range(0, len(manifest['files']), 2):
        rows = manifest['files'][start:start + 2]
        suffix = '' if start == 0 else '-' + rows[0]['face'].split('_', 1)[1]
        generate(tuple(row['file'] for row in rows), suffix)
