"""Local PNG/HTML review artifacts. No image-library or web-service dependency."""
import html
import json
from pathlib import Path
import struct
import urllib.parse
import zlib


def write_png(path, width, height, pixels, channels=3):
    if channels not in (3, 4) or width <= 0 or height <= 0 or len(pixels) != width*height*channels:
        raise ValueError('Invalid PNG image dimensions or channels')
    with Path(path).open('wb') as output:
        output.write(b'\x89PNG\r\n\x1a\n')
        def chunk(kind, data):
            output.write(struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data)))
        chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6 if channels == 4 else 2, 0, 0, 0))
        compressor = zlib.compressobj()
        stride = width*channels
        for y in range(height):
            encoded = compressor.compress(b'\0'+pixels[y*stride:(y+1)*stride])
            if encoded: chunk(b'IDAT', encoded)
        encoded = compressor.flush()
        if encoded: chunk(b'IDAT', encoded)
        chunk(b'IEND', b'')


def thumbnail(width, height, data, channels, scale):
    w, h = max(1, int(width*scale)), max(1, int(height*scale))
    result = bytearray()
    for y in range(h):
        for x in range(w):
            i = ((y*height//h)*width+x*width//w)*channels
            if channels == 4:
                alpha = data[i+3]
                result.extend((data[i+c]*alpha+255*(255-alpha))//255 for c in range(3))
            else: result.extend(data[i:i+3])
    return w, h, bytes(result)


def save_visuals(target, actual, reference, ppm_reader, max_dimension=1600):
    target = Path(target)
    aw = ah = 0
    rgba = None
    if actual is not None:
        with Path(actual).open('rb') as source:
            header = source.read(12)
            if len(header) != 12: raise ValueError('Missing actual page header')
            aw, ah, _ = struct.unpack('=III', header)
            if not aw or not ah or aw*ah > 64*1024*1024: raise ValueError('Oversized actual page')
            rgba = source.read(aw*ah*4+1)
            if len(rgba) != aw*ah*4: raise ValueError('Actual pixel length mismatch')
        write_png(target/'actual.png', aw, ah, rgba, 4)
    rw = rh = 0
    rgb = None
    if reference is not None:
        rw, rh, rgb = ppm_reader(reference)
        write_png(target/'reference.png', rw, rh, rgb)
    dimensions_match = (aw, ah) == (rw, rh)
    scale = min(1.0, max_dimension/max(aw, ah, rw, rh, 1))
    left = thumbnail(aw, ah, rgba, 4, scale) if rgba is not None else None
    right = thumbnail(rw, rh, rgb, 3, scale) if rgb is not None else None
    if left:
        write_png(target/'actual-preview.png', *left)
    if right:
        write_png(target/'reference-preview.png', *right)
    if left and right:
        lw, lh, lp = left; rw, rh, rp = right
        width, height, gap = lw+rw+12, max(lh, rh), 12
        side = bytearray(b'\xff'*(width*height*3))
        for y in range(height):
            if y < lh: side[y*width*3:y*width*3+lw*3] = lp[y*lw*3:(y+1)*lw*3]
            side[(y*width+lw)*3:(y*width+lw+gap)*3] = b'\x80'*(gap*3)
            if y < rh: side[(y*width+lw+gap)*3:(y*width+lw+gap+rw)*3] = rp[y*rw*3:(y+1)*rw*3]
        write_png(target/'comparison.png', width, height, side)
        if dimensions_match:
            # Display-only difference uses the same thumbnail scale; metrics use original pixels.
            if (lw, lh) == (rw, rh):
                difference = bytes(abs(a-b) for a,b in zip(lp, rp))
                write_png(target/'difference.png', lw, lh, difference)


def write_gallery(report):
    report = Path(report).resolve()
    output = report.with_suffix('.html')
    rows = [json.loads(line) for line in report.read_text().splitlines()]
    blocks = []
    for row in rows:
        if row.get('kind') != 'page' and not (row.get('kind') == 'document' and row.get('status') != 'ok'):
            continue
        title = str(row.get('file', 'document')) + (f": page {row['page']}" if 'page' in row else '')
        status = row.get('comparison', {}).get('status', row.get('status', 'unknown'))
        block = '<section><h2>'+html.escape(title)+'</h2><p>'+html.escape(str(status))+'</p>'
        if row.get('error'): block += '<pre>'+html.escape(row['error'])+'</pre>'
        if row.get('artifacts'):
            folder = Path(row['artifacts']).resolve()
            try: relative = folder.relative_to(report.parent)
            except ValueError: relative = None
            if relative is not None:
                previews = [] if (folder/'comparison.png').is_file() else [('actual-preview.png', 'cupidimage output'), ('reference-preview.png', 'Independent reference')]
                for filename, label in previews + [('comparison.png', 'Left: cupidimage. Right: reference. Preview may be downsampled.'),
                                        ('actual.png', 'Actual full-resolution PNG'), ('reference.png', 'Reference full-resolution PNG'),
                                        ('difference.png', 'Absolute difference preview (black means equal)')]:
                    if not (folder/filename).is_file(): continue
                    url = urllib.parse.quote((relative/filename).as_posix(), safe='/')
                    if filename in ('comparison.png', 'difference.png', 'actual-preview.png', 'reference-preview.png'):
                        block += '<figure><img loading="lazy" src="'+url+'" alt="'+html.escape(label, quote=True)+'"><figcaption>'+html.escape(label)+'</figcaption></figure>'
                    else: block += '<p><a href="'+url+'">'+label+'</a></p>'
        blocks.append(block+'</section>')
    output.write_text('<!doctype html><html lang="en"><meta charset="utf-8"><title>PDF visual review</title>'
                     '<style>body{font:16px system-ui;max-width:1500px;margin:2rem auto;padding:0 1rem;background:#eee;color:#111}'
                     'section{background:white;padding:1rem;margin:1rem 0;border:1px solid #bbb}img{max-width:100%;height:auto}'
                     'pre{white-space:pre-wrap}figure{margin:1rem 0}figcaption{font-size:.9rem}</style>'
                     '<h1>PDF visual review</h1><p>Left: cupidimage. Right: independent reference. '
                     'Dimension mismatches are not resized away in the metrics. Missing output is a failure, not a blank-page match.</p>'
                     + ''.join(blocks) + '</html>')
    return output
