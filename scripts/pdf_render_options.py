#!/usr/bin/env python3
"""Shared PDF rendering options for corpus tools and probe capability checks."""
import json

OPTIONS = (
    ('--pdf-text-aa', 'text_antialias', 'Enable grayscale PDF outline text fills'),
    ('--pdf-tt-hinting', 'truetype_hinting', 'Enable the native TrueType hinting profile'),
    ('--pdf-cff-hinting', 'cff_hinting', 'Enable the bounded CFF hinting profile'),
    ('--pdf-type1-hinting', 'type1_hinting', 'Enable the bounded Type 1 hinting profile'),
)


def add_pdf_options(parser):
    for flag, _, help_text in OPTIONS:
        parser.add_argument(flag, action='store_true', help=help_text)


def pdf_option_key_values(values):
    # Keep earlier report profiles compatible when the new option is off.
    return {key: bool(value) for key, value in values.items()
            if key != 'type1_hinting' or value}


def pdf_option_values(args):
    return pdf_option_key_values({field: bool(getattr(args, flag[2:].replace('-', '_'), False))
                                  for flag, field, _ in OPTIONS})


def pdf_option_arguments(values):
    return [flag for flag, field, _ in OPTIONS if values.get(field, False)]


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
