#!/usr/bin/env python3
"""Regenerate owned PDF DCT fixtures using ImageMagick's JPEG encoder."""
from pathlib import Path
import base64
import json
import subprocess
import tempfile


def main():
    cases = []
    with tempfile.TemporaryDirectory(prefix='cupidimage-dct-encode-') as directory:
        root = Path(directory)
        for components, mode in ((1, 'gray'), (3, 'rgb'), (4, 'cmyk')):
            for width, height in ((8, 8), (33, 17), (129, 9)):
                pixels = bytes((x * 37 + y * 59 + c * 83 + x * y * 11) % 256
                               for y in range(height) for x in range(width)
                               for c in range(components))
                raw = root / 'pixels.raw'
                raw.write_bytes(pixels)
                for progressive in (False, True):
                    for sampling in ('1x1', '2x2', '4x1') if components == 3 else ('1x1',):
                        for restart in (0, 1):
                            name = f'{mode}-{width}x{height}-{sampling}-p{int(progressive)}-r{restart}'
                            case = dict(name=name, width=width, height=height, components=components,
                                        progressive=progressive, sampling=sampling, restart=restart)
                            for arithmetic in (False, True):
                                path = root / 'encoded.jpg'
                                command = ['convert', '-size', f'{width}x{height}', '-depth', '8',
                                           f'{mode}:{raw}', '-quality', '90', '-sampling-factor', sampling,
                                           '-define', f'jpeg:arithmetic-coding={str(arithmetic).lower()}',
                                           '-define', f'jpeg:restart-interval={restart}']
                                if progressive:
                                    command += ['-interlace', 'Plane']
                                subprocess.run([*command, str(path)], check=True)
                                encoded = path.read_bytes()
                                sof = 0xca if progressive else 0xc9
                                assert not arithmetic or bytes((255, sof)) in encoded
                                assert not restart or b'\xff\xdd' in encoded
                                case['arithmetic' if arithmetic else 'huffman'] = base64.b64encode(encoded).decode()
                            cases.append(case)
    version = subprocess.check_output(['convert', '-version'], text=True).splitlines()[0]
    output = dict(provenance=f'Owned synthetic component samples. {version}. '
                  'Paired arithmetic/Huffman encodings use identical pixels, quantization and sampling. '
                  'Regenerate with scripts/generate_pdf_dct_arithmetic.py; no encoder is needed for tests.',
                  cases=cases)
    Path('tests/fixtures/pdf-dct-arithmetic.json').write_text(json.dumps(output, indent=2) + '\n')
    print(f'Generated {len(cases)} paired DCT fixtures')


if __name__ == '__main__':
    main()
