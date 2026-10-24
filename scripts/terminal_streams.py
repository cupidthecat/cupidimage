"""Independent terminal stream parsers shared by asset tests."""
import re

def decode_ansi(data):
    position = 0
    pixels = []
    color = None
    widths = []
    width = 0
    while position < len(data):
        match = re.match(rb'\x1b\[48;2;(\d+);(\d+);(\d+)m', data[position:])
        if match:
            color = tuple(map(int, match.groups())); position += match.end()
        elif data[position:position+4] == b'\x1b[0m':
            color = None; position += 4
        elif data[position:position+1] == b' ':
            assert color is not None
            pixels.append(color); width += 1; position += 1
        elif data[position:position+1] == b'\n':
            widths.append(width); width = 0; position += 1
        else:
            raise AssertionError(('Unexpected ANSI bytes', data[position:position+20]))
    assert width == 0
    return widths, pixels
