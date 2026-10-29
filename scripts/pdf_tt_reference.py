"""Shared state extraction for optional TrueType outline comparisons."""


def outline_state(face):
    outline = face.glyph.outline
    ends = set(outline.contours)
    return [(x, y, int(tag & 1), int(i in ends))
            for i, ((x, y), tag) in enumerate(zip(outline.points, outline.tags))]
