#!/usr/bin/env python3
"""Generate owned JPEG references with an independent libjpeg pixel oracle."""
import base64
import hashlib
import json
from pathlib import Path
import random
import zlib
import PIL
from PIL import Image, features

ROOT = Path(__file__).resolve().parents[1]
output = ROOT / 'assets/jpeg/reference'
output.mkdir(parents=True, exist_ok=True)
rows = []
for mode in ('L', 'RGB'):
    for width, height in ((8, 8), (17, 19), (32, 32)):
        for pattern in ('gradient', 'noise'):
            rng = random.Random(17)
            values = []
            for y in range(height):
                for x in range(width):
                    if mode == 'L':
                        values.append((x*13+y*7) % 256 if pattern == 'gradient' else rng.randrange(256))
                    else:
                        values.append(((x*13+y*7) % 256, (x*3+y*11) % 256, (x*7+y*5) % 256)
                                      if pattern == 'gradient' else tuple(rng.randrange(256) for _ in range(3)))
            image = Image.new(mode, (width, height)); image.putdata(values)
            for progressive in (False, True):
                for quality in (10, 50, 90):
                    name = f'{mode.lower()}-{pattern}-{width}x{height}-{"progressive" if progressive else "baseline"}-q{quality}.jpg'
                    path = output / name
                    image.save(path, quality=quality, progressive=progressive, subsampling=0)
                    expected = Image.open(path).convert('RGBA').tobytes()
                    rows.append(dict(asset=str(path.relative_to(ROOT)), width=width, height=height,
                                     sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                                     rgba_zlib=base64.b64encode(zlib.compress(expected)).decode(),
                                     tolerance=2 if mode == 'L' else 3))
(ROOT / 'tests/fixtures/jpeg-reference.json').write_text(json.dumps({
    'provenance': f'Owned synthetic pixels, seed 17. Pillow {PIL.__version__} / libjpeg {features.version("jpg")} encoder and independent pixel oracle. RGB uses 4:4:4 to avoid interpolation-policy differences.',
    'cases': rows}, indent=2) + '\n')
print('Generated', len(rows), 'JPEG references')
