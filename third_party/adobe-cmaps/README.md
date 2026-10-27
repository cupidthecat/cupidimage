# Adobe CMap resources

These 59 predefined CJK encoding CMaps are listed in ISO 32000-1 Table 118.
Identity-H and Identity-V are constructed directly by the renderer. The source
files retain their original copyright and BSD license notices.

Source: https://github.com/adobe-type-tools/cmap-resources
Revision: f5cf3bca7fdfeaceb77aa82847e974f2306c20b4

`manifest.json` records each original path and SHA-256 digest. The files include
all base maps needed by their `usecmap` references. Additional maps in the upstream
repository are not bundled by this selection.

Run `python3 scripts/generate_pdf_cmaps.py` from the repository root to regenerate
`src/formats/pdf/fonts/cmap_data.inc`. Generation checks source digests and uses
Python's standard-library zlib compressor. Runtime decoding uses CupidImage's
existing bounded zlib decoder; no system CMap installation is needed.

Include THIRD_PARTY_NOTICES.md with binary distributions.
