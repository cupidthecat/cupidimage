#!/usr/bin/env python3
"""Shared PDF rendering options for corpus tools and probe capability checks."""
import json

OPTIONS = (
    ('--pdf-text-aa', 'text_antialias', 'Enable grayscale PDF outline text and clipping edges'),
    ('--pdf-tt-hinting', 'truetype_hinting', 'Enable the native legacy TrueType hinting profile'),
    ('--pdf-cff-hinting', 'cff_hinting', 'Enable the bounded CFF hinting profile'),
    ('--pdf-type1-hinting', 'type1_hinting', 'Enable the bounded Type 1 hinting profile'),
    ('--pdf-graphics-aa', 'graphics_antialias', 'Enable grayscale PDF vector painting and clipping edges'),
)
MODERN_TT_FLAG = '--pdf-tt-modern-hinting'


def add_pdf_options(parser):
    for flag, field, help_text in OPTIONS:
        if field != 'truetype_hinting':
            parser.add_argument(flag, action='store_true', help=help_text)
    profiles = parser.add_mutually_exclusive_group()
    profiles.add_argument('--pdf-tt-hinting', action='store_true', help=OPTIONS[1][2])
    profiles.add_argument(MODERN_TT_FLAG, action='store_true',
                          help='Select modern TrueType hinting; combine with --pdf-text-aa for grayscale')


def pdf_option_key_values(values):
    # Preserve old Boolean profiles; the second enabled TrueType mode is distinct.
    result = {}
    for key, value in values.items():
        if key == 'truetype_hinting':
            if value not in (None, False, True, 0, 1, 2):
                raise ValueError('Invalid TrueType hinting profile')
            result[key] = 2 if value == 2 else bool(value)
        elif key not in ('type1_hinting', 'graphics_antialias') or value:
            result[key] = bool(value)
    return result


def pdf_option_values(args):
    values = {field: bool(getattr(args, flag[2:].replace('-', '_'), False))
              for flag, field, _ in OPTIONS}
    if getattr(args, 'pdf_tt_modern_hinting', False):
        values['truetype_hinting'] = 2
    return pdf_option_key_values(values)


def pdf_option_arguments(values):
    return [MODERN_TT_FLAG if field == 'truetype_hinting' and values.get(field) == 2 else flag
            for flag, field, _ in OPTIONS if values.get(field, False)]


def supports_pdf_options(raw, values):
    requested = pdf_option_arguments(values)
    if not requested:
        return True
    try:
        data = json.loads(raw)
    except (ValueError, UnicodeError, TypeError):
        return False
    if not isinstance(data, dict) or type(data.get('schema')) is not int or data['schema'] != 1:
        return False
    supported = data.get('pdf_options')
    return (isinstance(supported, list) and all(isinstance(value, str) for value in supported)
            and all(value in supported for value in requested))
