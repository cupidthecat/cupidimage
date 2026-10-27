# Testing

## Everyday checks

```sh
make -j2 all test
make test-pdf-corpus
```

The regular suite needs a C99 compiler, GNU Make, Python 3, and Bash. It does not
need an external image codec or network access. Fixture generation is separate:
Pillow, FFmpeg, or the format-specific generator dependencies are only needed
when regenerating those fixtures.

`make test` includes the complete checked-in asset audit. It validates the file
inventory, decodes every image and each exposed GIF frame or TIFF/PDF/ICO page,
and checks ANSI, Kitty, and Sixel output using a deterministic sample of up to
16 × 16 pixels from each decoded page. The sample bounds terminal-test cost;
it is not a replacement for the full-resolution decoder pixel tests.

The runner records separate results:

- `reference-pass`: full decoded pixels match an independent or analytic oracle,
  within the documented JPEG rounding tolerance where applicable.
- `decoded-unverified`: decoding succeeded, but this page has no independent
  expected pixel image. Terminal sample round trips are still checked.
- `expected-rejection`: a deliberately malformed input was rejected normally.
  A crash, timeout, or sanitizer failure does not count as an expected rejection.
- `tolerated-negative`: the manifest explicitly permits a malformed/nonstandard
  case that the decoder can safely interpret. It is not silently exempted.
- `metadata-only`: the TGA type-0 fixture correctly contains no image.
- Other results identify failures, including unexpected acceptance, invalid
  output, pixel differences, process signals, and timeouts.

```sh
make audit-assets
# The report is .cache/asset-tests/results.jsonl.
python3 scripts/test_asset_corpus.py --require-reference
```

`--require-reference` also fails for pages without an independent pixel oracle.
It is deliberately stricter than the normal gate. `--diagnostic` records failures
without a failing exit status and is intended for investigation, not CI acceptance.

See [test assets](test-assets.md) for provenance, regeneration, and integrity checks.
The inventory is never rewritten by a test run.

## Full PDF.js corpus

The pinned catalog in `tests/fixtures/pdfjs-corpus.json` covers all **1,007 PDFs
tracked in Git** at revision `67667f1e449378a2675f4095cded012b7ce5f37b`, about
119 MiB in total. The catalog includes file lengths and Git blob hashes; the
runner verifies both before using downloaded or cached files. Per-run reports
also record SHA-256 hashes.

```sh
make bin/test_pdf_pages
# Full tracked corpus, every page up to the explicit per-document safety cap:
python3 scripts/audit_pdf_corpus.py --download
# Equivalent Make target:
make audit-pdf-full

# Faster first-page sweep, explicitly not full-page coverage:
python3 scripts/audit_pdf_corpus.py --download --pages first \
  --report .cache/pdfjs-full/first-pages.jsonl

# A small feature-focused selection:
python3 scripts/audit_pdf_corpus.py --download --match '*shading*' --limit 10

# Continue an interrupted report with the same binary and run options:
python3 scripts/audit_pdf_corpus.py --download --resume
```

Downloads go into `.cache/pdfjs-full/<revision>/`, not Git. The 460 `.pdf.link`
entries in the upstream tree point at separate external downloads. They are
listed in the catalog but are **not fetched automatically**. This avoids silently
following third-party URLs, dead links, or arbitrary document actions. If those
files have been acquired in a local PDF.js checkout, audit that directory too:

```sh
python3 scripts/audit_pdf_corpus.py --corpus /path/to/pdf.js/test/pdfs
```

`--corpus` recursively finds local `.pdf` files. It does not execute JavaScript,
launch actions embedded in a PDF, or invoke upstream download scripts.

Each run writes JSON Lines records as work finishes, including document/page
counts, errors, timeouts, output dimensions, pixel hashes, and a final summary.
The probe is copied at startup so rebuilding the library during a long run does
not silently mix results from different binaries. Resume requires the same
binary and configuration; use a new report after changing the decoder.

The default timeout is 15 seconds per subprocess. Each document is capped at
1,000 pages; raise `--max-pages` deliberately for larger documents. Untested pages
are counted rather than silently dropped. On POSIX systems, subprocesses also
have CPU, output-size, core-dump, and address-space limits. Set `--memory-mib 0`
when using an AddressSanitizer build, whose shadow mapping needs a large virtual
address space. Systems without the Python `resource` module retain timeouts but
not those POSIX resource limits. These limits are not a security sandbox.

The corpus audit is diagnostic and returns success after writing its report.
Use `--fail-on-error` to make errors, skipped pages, or reference differences fail
the command. Successfully loading a document never establishes visual fidelity.

## Independent PDF image comparisons

Install the system `pdfinfo` and `pdftoppm` tools if you want independent page
counts and raster comparisons. The runner does not install them itself.

```sh
python3 scripts/audit_pdf_corpus.py --corpus assets/pdf/pdfjs \
  --reference --keep-images --report .cache/pdfjs-full/visual.jsonl
```

Reference pages use 96 DPI and the CropBox. Equal-sized pages are compared
channel by channel, reporting maximum error, mean absolute error, and the fraction
of differing channels. Different sizes are reported as dimension mismatches;
the runner does not resize, align, or crop them to manufacture a match. The library preserves page whitespace; remaining dimension differences should
be investigated as geometry or parsing discrepancies, not hidden by resizing.

`--keep-images` saves full-resolution actual/reference PNGs, a side-by-side PNG,
and a difference preview when page dimensions match. An HTML gallery is written
beside the JSONL report, for example `visual.html`. Open it in a browser to inspect
several pages. Preview images may be downsampled for display; the numerical
comparison still uses original pixels without alignment or resizing.

To display comparisons in the terminal as they are produced:

```sh
python3 scripts/audit_pdf_corpus.py --download --reference --terminal-preview \
  --pause --limit 20 --report .cache/pdfjs-full/visual-20.jsonl
```

This is different from the ordinary audit command, which prints progress/results
but does not display page images. Missing actual output is labeled as a failure;
a reference-only preview is never presented as cupidimage's output.

For interactive **correct / partly wrong / wrong / skip** ratings, mixed-format
selections, and resumable manual reviews, use [interactive visual review](visual-review.md).

Raw probe output and reference PPMs are also retained for diagnosis. Actual PDF
output starts with three native-endian 32-bit values (width, height, page count),
followed by RGBA bytes. All generated artifacts remain ignored by Git. Large
all-page runs with saved images can use substantial disk space; start with a
selection rather than storing the entire corpus at once.

## Focused suites and sanitizers

Use `make test-png`, `test-jpeg`, `test-jpeg-reference`, `test-webp`,
`test-webp-container`, `test-webp-pixels`, `test-gif-regressions`,
`test-tiff-bounds`, or `test-decoder-safety` when changing a decoder.

PDF checks include `test-pdf-pages`, `test-pdf-streams`, `test-pdf-safety`,
`test-pdf-graphics`, `test-pdf-images`, `test-pdf-packed-images`, `test-pdf-forms`, `test-pdf-clipping`,
`test-pdf-colors`, `test-pdf-masks`, `test-pdf-filters`, `test-pdf-lzw`,
`test-pdf-indexed`, `test-pdf-transparency`, `test-pdf-font-maps`, `test-pdf-object-syntax`, `test-pdf-shading`, `test-pdf-functions`, `test-pdf-sampled-functions`, `test-pdf-calculator`, and `test-pdf-clip-oom`. `test-pdf-audit` tests the corpus runner
itself without network access. `test-deflate-containers` covers shared compression
through complete PDF/TIFF containers, and `test-inflate` checks zlib directly.

SVG has `test-svg-regressions`, `test-svg-safety`, `test-svg-numeric`, and
`test-svg-css-oom`. Terminal checks are `test-sixel`, `test-terminal-regressions`,
and `test-format-pixels`. `test-cli` covers command-line behavior.

```sh
make -j2 all test CC=clang OBJDIR=obj/clang BINDIR=bin/clang \
  CFLAGS='-O1 -g -Wall -Wextra -std=c99 -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie'
```

Use separate object and binary directories when changing compiler flags. CI runs
GCC and Clang sanitizer jobs. Interactive viewers such as `make test-svg` and
`make test-pdf` are manual inspection tools, not unattended regression gates.

The packed-image suite covers 156 analytic pixel/validation cases. Its optional
`--reference` mode adds independent raster comparisons and needs `pdftoppm` plus
the optional `fitz` Python module for full 16-bit predictor/mask precision. The
secondary comparisons allow one channel level for gray-background tie rounding;
the normal analytic pixel checks remain exact. None of those external tools are
needed for `make test-pdf-packed-images`.

## PDF fax regression fixtures

`make test-pdf-ccitt` runs direct filter checks and 180 exact image comparisons.
The fixed rasters and their compressed bytes are in
`tests/fixtures/pdf-ccitt.json`; routine tests use only Python's standard library.
To regenerate this authored fixture set with Pillow's TIFF encoder, run
`python3 scripts/test_pdf_ccitt.py --generate`. Review the fixture diff before
committing it. Patterns include solid rows, stripes, alternating pixels, random
bits, non-byte-aligned widths, and long makeup runs.

## Inline PDF images

`make test-pdf-inline` compares inline images with equivalent image XObjects
and checks malformed input handling. The normal run has no extra dependencies.
For the ten optional independent raster checks, run
`python3 scripts/test_pdf_inline.py bin/test_pdf_pages --reference` with PyMuPDF
installed. The whole-stream DEFLATE tests remain strict about trailing data;
separate prefix tests check the byte count returned for inline Flate streams.

## PDF text state

`make test-pdf-text-state` compares equivalent text/graphics transforms and
explicit glyph positions. It checks the text operators, all rendering modes,
clipping, simple-font widths and word-spacing rules without depending on the
shape of an external font. These checks deliberately separate positioning from
the still-incomplete font-outline renderer.
