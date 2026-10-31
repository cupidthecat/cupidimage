#!/usr/bin/env python3
"""Compatibility entry point for the original CP932-only generator."""
from generate_pdf_codepages import main, mappings

if __name__ == '__main__':
    main((932,))
