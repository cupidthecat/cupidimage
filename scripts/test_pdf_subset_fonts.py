#!/usr/bin/env python3
"""Check strict admission and bounded PDF CID subset-font recovery."""
import base64
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_truetype import tables
from test_pdf_cff import fixture as cff_font

FONT = base64.b64decode(json.loads(Path('tests/fixtures/pdf-truetype.json').read_text())['font'])


def repack(font, changes):
    original = tables(font)
    payloads = {tag: font[offset:offset+length] for tag, (_, offset, length) in original.items()}
    payloads.update(changes)
    payloads = {tag: data for tag, data in payloads.items() if data is not None}
    head = bytearray(payloads[b'head']);head[8:12] = bytes(4);payloads[b'head'] = bytes(head)
    count = len(payloads); power = count.bit_length()-1; search = 16 << power
    result = bytearray(struct.pack('>IHHHH', 0x10000, count, search, power, count*16-search))
    body = bytearray(); offset = 12 + 16*count
    for tag, data in sorted(payloads.items()):
        padded = data + bytes((-len(data)) % 4)
        checksum = sum(struct.unpack('>'+str(len(padded)//4)+'I', padded)) & 0xffffffff if padded else 0
        result += struct.pack('>4sIII', tag, checksum, offset, len(data))
        body += padded;offset += len(padded)
    result += body
    total = sum(struct.unpack('>'+str(len(result)//4)+'I', result)) & 0xffffffff
    head_offset = tables(result)[b'head'][1]
    struct.pack_into('>I', result, head_offset+8, (0xb1b0afba-total) & 0xffffffff)
    return bytes(result)


def tt_pair(reorder=False, sparse=False, font=FONT):
    source = tables(font)
    def payload(tag):
        _, start, length = source[tag]
        return font[start:start+length]
    head = bytearray(payload(b'head')); maxp = bytearray(payload(b'maxp'))
    glyphs = struct.unpack_from('>H', maxp, 4)[0]
    short = struct.unpack_from('>h', head, 50)[0] == 0
    raw = payload(b'loca'); width = 2 if short else 4
    locations = [int.from_bytes(raw[i*width:(i+1)*width], 'big') * (2 if short else 1) for i in range(glyphs+1)]
    glyf = payload(b'glyf'); records = [glyf[a:b] for a, b in zip(locations, locations[1:])]
    control = repack(font, {b'cmap': None})
    order = [i for i, record in enumerate(records) if record]
    if reorder:
        order = order[1:2] + order[:1] + order[2:]
    body = bytearray(); starts = {}
    for index in order:
        starts[index] = len(body);body += records[index]
    loc = [len(body)] * (glyphs+1)
    for index in range(glyphs-1, -1, -1):
        loc[index] = starts[index] if records[index] else loc[index+1]
    hmtx = payload(b'hmtx')
    if sparse:
        # Four additional empty glyphs are represented by a broken zero tail.
        control_loca = locations + [locations[-1]] * 4
        control_head = bytearray(head);struct.pack_into('>h', control_head, 50, 1)
        struct.pack_into('>H', maxp, 4, glyphs+4)
        hmtx += bytes(8)
        control = repack(font, {b'cmap': None, b'head': bytes(control_head), b'maxp': bytes(maxp),
                               b'hmtx': hmtx, b'loca': b''.join(struct.pack('>I', n) for n in control_loca)})
        loc = loc[:-1] + [0] * 5
    struct.pack_into('>h', head, 50, 1)
    broken = repack(font, {b'cmap': None, b'head': bytes(head), b'maxp': bytes(maxp), b'hmtx': hmtx,
                          b'glyf': bytes(body), b'loca': b''.join(struct.pack('>I', n) for n in loc)})
    return control, broken, glyphs + (4 if sparse else 0)


def main():
    probe = str(Path(sys.argv[1]).resolve());checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupid-subset-fonts-') as directory:
        path = Path(directory) / 'font.bin'
        def run(data, mode, glyph=0, reject=False, faults=False):
            nonlocal checks, allocations
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), mode, str(glyph), *(['--faults'] if faults else [])],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == (3 if reject else 0), (mode, glyph, result.returncode, result.stderr)
            checks += 1
            if faults and not reject:
                allocations += int(result.stdout.split()[0])
            return result.stdout.splitlines()
        for reorder, sparse in ((True, False), (False, True), (True, True)):
            control, broken, count = tt_pair(reorder, sparse)
            run(broken, 'tt-strict', reject=True)
            for glyph in range(count):
                expected = run(control, 'tt-strict', glyph)
                actual = run(broken, 'tt-recover', glyph)
                assert actual[0].split()[-1] == '1'
                assert actual[1:] == expected[1:], (reorder, sparse, glyph, actual, expected)
                assert run(control, 'tt-recover', glyph) == expected
            run(broken, 'tt-recover', 5, faults=True)
            layout = tables(broken); start, length = layout[b'loca'][1:]
            offsets = bytearray(broken[start:start+length]);struct.pack_into('>I', offsets, 4, 0xffffffff)
            run(repack(broken, {b'loca': bytes(offsets)}), 'tt-recover', reject=True)
            run(repack(broken, {b'loca': broken[start:start+length-4]}), 'tt-recover', reject=True)
            # An interior offset cannot let a glyph borrow the next record's bytes.
            offsets = bytearray(broken[start:start+length]);a = struct.unpack_from('>I', offsets, 4)[0]
            struct.pack_into('>I', offsets, 12, a+1)
            run(repack(broken, {b'loca': bytes(offsets)}), 'tt-recover', 1, reject=True)
        valid = cff_font(cid=True, fdselect=b'\3\0\1\0\0\0\0\4')
        repaired = cff_font(cid=True, fdselect=b'\3\0\1\0\1\0\0\4')
        expected = run(valid, 'cff-strict')
        assert run(valid, 'cff-recover') == expected
        run(repaired, 'cff-strict', reject=True)
        assert run(repaired, 'cff-recover') == expected
        run(repaired, 'cff-recover', faults=True)
        for private_count, selector in ((2, b'\3\0\1\0\1\0\0\4'),
                                        (1, b'\3\0\1\0\2\0\0\4'),
                                        (1, b'\3\0\1\0\1\1\0\4'),
                                        (1, b'\3\0\1\0\1\0\0\3')):
            run(cff_font(cid=True, private_count=private_count, fdselect=selector), 'cff-recover', reject=True)
        for cut in (0, 1, 3, len(repaired)-1):
            run(repaired[:cut], 'cff-recover', reject=True)
    print(f'PDF subset-font structures: {checks} checks; {allocations} allocation checks')


if __name__ == '__main__':
    main()
