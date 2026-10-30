# Interactive visual review

This tool renders each selected image, PDF page, or exposed animation frame in
your terminal, then asks you to rate what you see. It saves each answer immediately.
The review log is separate from automated pass/fail tests.

```sh
make cli bin/test_asset_probe
```

## Download PDFs before reviewing

The PDF corpus is cached outside Git. Pulling a branch does not download those
files. To download and verify all 1,007 pinned PDFs without rendering or rating:

```sh
make download-pdf-corpus
# Or directly, without a build:
python3 scripts/audit_pdf_corpus.py --download-only
```

Existing cached files are verified and reused. The command can be rerun after an
interruption; no `--resume` is needed. It does not change rating or audit reports.
Use `--match`, `--limit`, or `--cache` with the Python command to select inputs or
choose their cache location. A failed download or hash check gives a nonzero exit
status and preserves any existing file; it is not a rendering failure.

For a Sixel comparison after downloading and building:

```sh
python3 scripts/review_assets.py --pdfjs --reference --backend sixel \
  --report .cache/visual-review/pdf-ratings-sixel.jsonl
```

Alternatively, add `--download` to that review command to fetch each missing PDF
as it is reached. `Input unavailable: Not cached; use --download` means that no
decoder was tested for the file. The separate external `.link` corpus is not
followed by either download mode.

## Choose files

```sh
# Every normal checked-in asset:
python3 scripts/review_assets.py --assets

# Every PNG, or just the first five PNGs in sorted order:
python3 scripts/review_assets.py --format png
python3 scripts/review_assets.py --format png --range 1-5

# One file:
python3 scripts/review_assets.py assets/png/basic.png

# A mixture of formats:
python3 scripts/review_assets.py assets/png/basic.png assets/jpeg/cat.jpeg assets/pdf/pdfjs/basicapi.pdf

# All cached PDF.js documents, with an independent reference:
python3 scripts/review_assets.py --pdfjs --reference

# Twenty PDF.js documents, first page only; download missing files explicitly:
python3 scripts/review_assets.py --pdfjs --download --range 1-20 --pages first --reference

# Filter by filename, or preview the selection without rendering:
python3 scripts/review_assets.py --assets --format pdf,png --match '*quadrants*'
python3 scripts/review_assets.py --format png --range 1-5 --list
```

Files are sorted before applying the 1-based `--range`. Ranges can be combined,
for example `1-5,8,12-15`. `--pages` accepts `all`, `first`, or the same range
syntax and applies to the pages/frames exposed by each format's API. It defaults
to all. A single-frame PNG remains one item even when mixed with multipage PDFs.
Directories and quoted globs are also accepted as positional inputs.

Malformed assets under `negative/` are excluded unless `--include-negative` is
set. TGA type-0 metadata-only assets have no image to rate and are recorded as such.
SVG review uses its initial sampled state, not a browser animation timeline.

## PDF rendering options

PDF review defaults to the same unhinted, center-point text path as the library.
To test an explicit grayscale/native-hinting configuration on a small selection:

```sh
python3 scripts/review_assets.py --pdfjs --range 1-20 --pages first \
  --reference --backend sixel --pdf-text-aa --pdf-tt-hinting --pdf-cff-hinting --pdf-type1-hinting \
  --report .cache/visual-review/pdf-native-hinting-sixel.jsonl
```

The four switches can be used independently. They affect PDF decoding, not the
PNG preview sent to the terminal, and do not change other formats in a mixed
selection. They also do not change the independent renderer's settings. See the
[TrueType](pdf-page-hinting.md), [CFF](pdf-cff-page-hinting.md),
[Type 1](pdf-type1-page-hinting.md) and [coverage](pdf-coverage.md) guides for profile limits.

Type 1 hinting is recorded in a rendering profile only when enabled. A missing
`type1_hinting` field means false, preserving resume keys from older reports.

The runner checks that its copied probe supports requested PDF switches before
opening the report. A stale probe gives a rebuild error instead of marking the
selected documents wrong. Rebuild with `make cli bin/test_asset_probe` after
pulling renderer changes. `--list` still needs no renderer and writes no ratings.

## Rate the result

At each prompt:

- `c` or `1`: correct.
- `p` or `2`: partly wrong; `half bad` is also accepted.
- `w` or `3`: wrong.
- `s`, `0`, or Enter: skip without claiming correctness.
- `r`: redisplay the current item.
- `n`: add a note before choosing a rating.
- `q` or Ctrl+C: stop, keeping completed ratings.

Decode failures, crashes, timeouts, invalid output, and failed terminal rendering
automatically save a `wrong` rating and advance without asking you to judge a
reference-only or missing image. These records have `rating_source: automatic`;
keyboard decisions have `rating_source: manual`. Missing input files are reported
separately because no decoder was tested. Previously saved ratings are not rewritten.

Wrong/partial manual ratings offer an optional note. Describe missing text, incorrect
colors, misplaced shapes, clipping, or other visible problems. Decode errors and
terminal-render errors are recorded separately from your rating. A successful
process is not automatically marked correct.

With `--reference`, PDFs display cupidimage on the left and the independent
reference on the right. If actual rendering fails, a reference-only preview is
clearly labeled. The reference option requires installed `pdfinfo` and `pdftoppm`.
It does not silently claim an independent reference for PNG or another format.

Use `--backend ansi` to force colored terminal cells, or `--backend sixel` to
force Sixel. The default `auto` follows the CLI's terminal detection, including
Kitty where applicable. The previews fit the terminal; full-resolution image
quality cannot always be judged from a small terminal window.

## Save and resume

The default log is `.cache/visual-review/ratings.jsonl`. New passes append instead
of overwriting previous decisions. Choose another path with `--report`.

```sh
python3 scripts/review_assets.py --pdfjs --reference --resume
```

Resume skips ratings only when the file contents, selected page/frame, decoder
binary, CLI binary, backend, reference mode, reviewer label and PDF rendering
options match. A rebuilt
binary causes affected items to be reviewed again. Inputs and binaries are copied
for the active review so a concurrent edit cannot silently change the rated data.
Changing a PDF switch requires a new rating for affected PDF pages even when the
input and binaries are unchanged. Keep the same switches when resuming a pass.
Default-option fingerprints remain compatible with older logs.
External resources referenced by an SVG are not included in its file hash; do a
new pass rather than resuming if those resources change. Installed reference-tool
versions and system font configuration are not fingerprinted; use a new report
when those change.

Ratings include the source path/hash, dimensions, page/frame, available animation
metadata, result status, PDF options, note, and timestamp. `--reviewer` is an optional label,
not an authenticated identity. Treat logs from private documents as private too.
You can send the JSONL file with a bug report to identify exactly what looked wrong.

For generated PNG galleries and automated pixel differences rather than human
ratings, see [the corpus testing guide](testing.md). Both workflows are useful,
but their results answer different questions.
