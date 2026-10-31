"""Read the small, pinned OpenType metadata used by substitute-font generation."""
import struct


def u16(data, at):
    return struct.unpack_from('>H', data, at)[0]


def table(data, tag):
    assert data[:4] == b'OTTO'
    for i in range(u16(data, 4)):
        at = 12 + i * 16
        if data[at:at + 4] == tag:
            offset, length = struct.unpack_from('>II', data, at + 8)
            assert offset <= len(data) and length <= len(data) - offset
            return data[offset:offset + length]
    raise AssertionError(f'Missing {tag!r}')


def vertical_substitutions(raw):
    data = table(raw, b'GSUB')
    assert data[:4] in (b'\0\1\0\0', b'\0\1\0\1')
    features, lookups = u16(data, 6), u16(data, 8)
    candidates = []
    for i in range(u16(data, features)):
        record = features + 2 + i * 6
        if data[record:record + 4] != b'vrt2':
            continue
        at = features + u16(data, record + 4)
        candidates.append(tuple(u16(data, at + 4 + j * 2) for j in range(u16(data, at + 2))))
    # All script/language records in these pinned fonts select the same single
    # vrt2 lookup. Do not silently generalize this to arbitrary layout programs.
    assert candidates and all(value == candidates[0] for value in candidates)
    assert len(candidates[0]) == 1
    index = candidates[0][0]
    assert index < u16(data, lookups)
    lookup = lookups + u16(data, lookups + 2 + index * 2)
    assert (u16(data, lookup), u16(data, lookup + 2), u16(data, lookup + 4)) == (1, 0, 1)
    sub = lookup + u16(data, lookup + 6)
    coverage = sub + u16(data, sub + 2)
    glyphs = []
    if u16(data, coverage) == 1:
        glyphs = [u16(data, coverage + 4 + i * 2) for i in range(u16(data, coverage + 2))]
    else:
        assert u16(data, coverage) == 2
        for i in range(u16(data, coverage + 2)):
            first, last, start = struct.unpack_from('>HHH', data, coverage + 4 + i * 6)
            assert first <= last and start == len(glyphs)
            glyphs.extend(range(first, last + 1))
    assert glyphs == sorted(set(glyphs))
    if u16(data, sub) == 1:
        delta = u16(data, sub + 4)
        mapped = [(glyph + delta) & 65535 for glyph in glyphs]
    else:
        assert u16(data, sub) == 2 and u16(data, sub + 4) == len(glyphs)
        mapped = [u16(data, sub + 6 + i * 2) for i in range(len(glyphs))]
    count = u16(table(raw, b'maxp'), 4)
    assert all(a < count and b < count for a, b in zip(glyphs, mapped))
    return list(zip(glyphs, mapped))
