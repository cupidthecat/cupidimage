# Testing

Standard-font positioning can also be checked without comparing substitute
outlines: `make test-pdf-standard-fonts` validates the pinned AFMs and renders
invisible text followed by an embedded geometric marker. It also compares
bundled CFF substitutes with the same programs embedded in a PDF. See
[PDF fonts and text](pdf-fonts.md) for optional independent comparisons.

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
# Fetch inputs only; no renderer, manual rating, or report overwrite:
make download-pdf-corpus

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
binary and configuration, including enabled PDF rendering options; use a new
report after changing the decoder or those options. A probe that cannot confirm
requested options fails before the report is opened.

The audit accepts the same optional PDF text controls as the CLI and visual
reviewer. For example, a small automated run with explicit native profiles is:

```sh
make bin/test_pdf_pages
python3 scripts/audit_pdf_corpus.py --corpus assets/pdf/pdfjs --pages first \
  --pdf-text-aa --pdf-tt-hinting --pdf-cff-hinting --pdf-type1-hinting \
  --report .cache/pdfjs-full/native-text.jsonl
```

The options are recorded in the run configuration and checked on resume. They
apply only to actual output; independent reference settings are unchanged.
`--download-only` does not accept rendering switches. A default-options run
retains the previous configuration shape for compatibility with existing reports.

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

## Type 3 glyph programs

`make test-pdf-type3` compares glyph programs with explicit vector equivalents
and checks malformed/recursive programs. Its 60 cases include named/base
encodings, later missing-name overrides and indirect font subtypes. For optional independent comparisons,
run `python3 scripts/test_pdf_type3.py bin/test_pdf_pages --reference` with the
reference command-line tools and PyMuPDF installed. One shared-stream case uses
an explicit one-pixel boundary tolerance; the corresponding analytic comparison
still requires exact pixels. Type 3 tests do not establish complete font support;
embedded-program coverage has its own suite below.

## PDF patterns

`make test-pdf-patterns` runs 76 checks covering tiling, colour/opacity rules,
pattern resource scopes, clipping, transformed strokes, function-based/axial/
radial shading patterns, gradient cache reuse and bounded failure paths. Add
`--reference` when running `scripts/test_pdf_patterns.py bin/test_pdf_pages`
to check selected rasters independently. That optional pass needs the reference
command-line tools and PyMuPDF; the regular suite needs neither. The 39 optional
comparisons include explicit one- or two-channel-unit tolerances for gradient
approximation. Small analytic raster expectations remain exact.

## PDF stroke geometry

`make test-pdf-strokes` runs 60 checks for caps, joins, miter limits, dashes,
transforms, dictionary state and bounded failures. Run
`python3 scripts/test_pdf_strokes.py bin/test_pdf_pages --reference` for 26 exact
independent comparisons; that optional pass needs the reference command-line
tools and PyMuPDF. Analytic circle and polygon checks cover boundary cases where
independent rasterizers use different edge rules. The ordinary suite requires
neither reference dependency.

## CID positioning

`make test-pdf-cid` runs 62 Identity-H/V metric and placement checks without
external font dependencies. It compares identical fallback glyphs at explicit
origins, so font-outline differences cannot hide positioning failures. The PDF
allocation-failure target also exercises CID tables, malformed ranges and
recursive indirect values. These checks do not establish embedded-font fidelity.

## Embedded font programs

`make test-pdf-truetype` checks binary tables, glyph names and authored PDF font
fixtures. The optional independent pass is:

    python3 scripts/test_pdf_embedded_fonts.py bin/test_pdf_pages --reference

See [PDF fonts and text](pdf-fonts.md) for counts, fixture regeneration, data
licenses and the distinction between unhinted outlines and full font fidelity.
The regular suite does not require FontTools or a reference renderer.

## PDF encoding CMaps

`make test-pdf-cmaps` checks variable-length codespaces, CID ranges, missing-glyph
recovery, inheritance, writing modes and source-code word spacing. The 68 checks
include malformed maps, cycles and mapping-work exhaustion. Seven optional exact
raster comparisons run with:

    python3 scripts/test_pdf_cmaps.py bin/test_pdf_pages bin/test_pdf_cmap --reference

These are focused fixtures, not the full manual PDF rating pass.

`make test-pdf-predefined-cmaps` verifies the bundled standard CMaps against their
pinned definitions, then renders six authored glyph-selection fixtures. Add
`--reference` to scripts/test_pdf_predefined_cmaps.py for six independent exact
pixel comparisons. Mapping counts measure resource correspondence, not visual
coverage of that many distinct characters.

## CFF fonts

`make test-pdf-cff` checks compact font structures, Type 2 execution and PDF
rendering. Fixtures are authored byte programs and geometric glyphs, not copies
of commercial font programs. They include malformed offsets, truncated tables,
subroutine recursion, stack limits, hint masks, encodings, CID font dictionaries,
accent composition and exact cubic fill expectations. The PDF allocation-failure
suite covers partially loaded programs and partially expanded outlines.

For the optional independent raster comparisons:

    python3 scripts/test_pdf_cff_render.py bin/test_pdf_pages --reference

Unhinted comparisons do not establish hinted or antialiased font fidelity.

## Type 1 fonts

`make test-pdf-type1` covers binary font decryption, explicit byte lengths, font
encodings, sidebearings, paths, standard subroutines and malformed input. Its
lexer tests cover all radices from 2 through 36, decimal fractions and exponents,
string and dictionary boundaries, form-feed comments and 10,000 deterministic
byte mutations. Comma-decimal locale checks run when a suitable locale is
installed and report a skip otherwise. Private-hint checks distinguish literal
definitions, invalid values and unresolved computed expressions; they do not
claim Type 1 grid fitting. Trace cases cover stem groups, replacements, flex,
composite boundaries and shared execution budgets. Independent installed-font
checks are documented in [PostScript hint data](pdf-postscript-hints.md). The
owned geometric fixtures include encrypted and filtered font streams. Run
`python3 scripts/test_pdf_type1_render.py bin/test_pdf_pages --reference` for the
selected independent comparisons. Clear private dictionaries and a FontMatrix
rejected by the independent loader remain analytic-only tests.

The shared text tests follow ISO 32000-1/2 section 9.3.6: glyphs with no outlines
leave the previous clip unchanged. Earlier expectations that a space alone creates
an empty clip were corrected; the installed comparison renderer differs on that
case. The decision follows the selected glyph, not its character code.

## TrueType character maps and names

`make test-pdf-truetype` includes mixed-byte cmap format 2, mixed-word format 8,
trimmed full-width format 10, and all 258 standard Macintosh post names. It also
covers legacy signed post offsets and malformed table boundaries. The two added
asset PDFs have analytic rectangle oracles, not just successful decode checks.

See [PDF fonts and text](pdf-fonts.md) for independent table and raster comparisons
and the distinction between font cmaps and PDF encoding CMaps.

## Graphics-state font selection

`make test-pdf-graphics-fonts` tests font selection through ExtGState, including
indirect arrays/sizes, compressed objects, q/Q restoration, escaped resource
names and nested form/Type 3 scopes. The checked-in compressed-object asset has
an analytic rectangle oracle. Optional exact comparisons are listed in
[PDF fonts and text](pdf-fonts.md).

## Transparency groups and graphics-state masks

`make test-pdf-groups` verifies that group opacity is applied once, members use
reset alpha/blend state, and nested groups preserve their caller. Non-isolated
groups additionally check backdrop removal, partial-alpha backdrops, all blend
modes and cache/alpha separation. It also covers
text, images, patterns, shading, clipping and unsupported configurations. See
[PDF transparency](pdf-transparency.md) for independent-comparison tolerances,
the analytic bounding-box case and memory limits.

`make test-pdf-soft-masks` covers Alpha/Luminosity masks, captured transforms,
transfer functions, BC, state restoration, nested masks, image-mask precedence,
patterns and combined fill/stroke coverage. Allocation-failure tests also walk
mask construction and nested cleanup. The transparency guide records analytic-only
cases where independent renderers disagree. Three generated mask assets have
exact-pixel corpus expectations.

`make test-pdf-page-groups` checks initial blend modes against a transparent page
backdrop, explicit RGB/gray selection, indirect attributes, noninheritance,
empty-page validation and final-state separation. All assets retain their existing
expectations; two new analytic assets exercise page isolation and gray blending.

`make test-pdf-knockout` exercises separate shape/opacity, AIS, nested initial
backdrops, cached pattern shape and zero-opacity members. It includes all 16 blend
modes and two isolation settings. Optional comparisons use the secondary renderer;
the transparency guide identifies cases that remain analytic-only.

`make test-pdf-text-knockout` checks overlapping glyphs, TK state, independent text
objects, Type 3 internal painting and shape-only opacity. The independent
rasterizers differ from the specification on several overlapping-text cases;
those tests keep exact analytic expectations. Allocation-failure cases cover
both complete and unterminated text streams.

`make test-pdf-content-syntax` combines byte-level decoder tests with generated
rendering fixtures. It checks escaped font/resource names, literal CR/CRLF,
embedded NUL, odd hex nibbles, malformed TJ arrays and text-object context.
The content guide records exact size/depth bounds and independent comparisons.

`make test-pdf-mesh` checks free-form and lattice triangle streams, packed bit
widths, Decode, flag/row connectivity, optional functions and Indexed base-space
interpolation. The shading guide distinguishes analytic gradient checks from
independent comparisons and records the original corpus fixture's stream-length
failure. Synthetic asset expectations remain exact.

`make test-pdf-patch` checks Coons and tensor-product meshes, inverse mappings,
fold parameter priority, shared-edge records and field widths. It includes a
separate bisection oracle for 40 separable mappings. Allocation-failure tests cover
priority buffers, functions, pattern caches, masks and late work-budget failure.
Real corpus and diagnostic-wrapper results remain separate in the shading guide.

`make test-pdf-device-color` runs 74 analytic checks for direct CMYK-to-gray
conversion in fills, strokes, Type 3 text, images, all seven shading types and
uncolored patterns. It also checks saved color state, input clipping, 16-bit
image samples and repeated translucent paint without premature byte rounding.
The fixtures are generated in temporary directories; no downloaded corpus is
needed. Device-conversion formulas come from ISO 32000-1 section 10.3.

`make test-pdf-pattern-backdrops` checks non-isolated tiling evaluation against
analytic pixels, including changing backdrops, overlapping cells, transformed
grids, text, soft masks and deferred validation. Known independent-renderer
differences are documented in [PDF patterns](pdf-patterns.md).

`make test-pdf-pattern-colors` checks floating-point pattern caches across RGB/gray
groups, initial alpha-as-shape settings, compressed definitions and sub-byte
opacity. Allocation-failure tests cover definition retention and cache rebuilds.

`make test-pdf-shading-patterns` exercises pattern-local opacity, blending, masks
and shape across all shading geometries. Background members, compressed graphics
state values and ignored path/text parameters have exact analytic expectations.

## PDF object resolution

`make test-pdf-xref` checks complete generated PDFs, not just headerless object
snippets. Cases cover classic, stream and hybrid indices; incremental replacements;
free entries and generations; compressed-object indices; omitted/wide fields;
filter chains and predictors; indirect parameters and content arrays; and legacy
indirect stream lengths across revisions. Negative cases exercise cycles,
truncation, invalid offsets and malformed structural fields. The same PDFs run
through allocation-failure injection, checking leaks and successful-output pixels.
See [PDF objects and revisions](pdf-objects.md) for the supported rules and bounds.

`make test-pdf-font-defaults` checks null/undefined optional font values, indirect
widths, CID horizontal/vertical defaults, encoding CMap defaults,
embedded-program selection and inherited page/form resources against
exact equivalent documents. Allocation failures run through complete page loads;
a successful call must retain the baseline pixels, not silently replace fonts.

`make test-pdf-type3-objects` checks nonzero character-procedure generations,
undefined procedures, indirect font geometry, null resource inheritance and
horizontal width advances under rotated/sheared font matrices. Its complete
documents also run through allocation-failure injection.

`make test-pdf-truetype-selection` tests Unicode/Roman subtable precedence,
`post`-name fallback, extension maps and the Mac OS Roman additional glyph names.
Conflicting-table fixtures have exact vector equivalents and run through
complete-page allocation-failure injection. Direct/indirect Differences members
also have equivalent-document cases for Type 1, compact and Type 3 fonts;
malformed or cyclic required members are rejected. Use `--reference` with the script for
the selected exact independent comparisons described in the font guide.

`make test-pdf-dct` verifies native JPEG components and complete PDF filter chains,
including marker precedence, indirect per-stage parameters, Decode arrays and
soft masks. Allocation failures include the JPEG backend. Native-resolution
reference probes are separate from scaled raw-sample-equivalence checks; see
[PDF images](pdf-images.md).

`make test-jpeg-math` checks coefficient and refinement bounds, dense inverse
transform inputs, and sequential zero-run boundaries. These tests protect the
shared JPEG decoder used by PDF DCT streams. It also decodes all 256 decisions
in the published ISO/IEC 10918-1 K.4.1 arithmetic test sequence.

`make test-pdf-jpeg-sampling` generates varied component block grids for sampling
factors through four, edge blocks, restart wrapping, mixed/separate component
scans and per-component quantizer lifetimes. PDF cases compare with equivalent raw-sample images and exercise
allocation failures. The optional `--reference` pass checks selected grayscale
and RGB grids without claiming agreement for differing upsampling algorithms.


`make test-pdf-dct-arithmetic` compares paired arithmetic/Huffman streams before
color conversion and through complete PDF image rendering. It includes restart
markers, progressive refinement, separate scans, conditioning selectors, filter
chains, Decode inversion and allocation failures. The checked-in fixtures avoid
an encoder dependency in CI. Use `--reference` with the script for independent
paired-image comparisons; see [PDF images](pdf-images.md) for their scope.


`make test-pdf-image-values` compares indirect image/mask metadata with direct
values and optional null entries with omitted entries. It covers Decode and
Matte members, color-key arrays, stencil defaults, escaped names, mask reference
chains and soft-mask precedence. Required null/mistyped values and cycles are
rejected. Successful cases run through complete-page allocation-failure checks.


`make test-pdf-user-unit` compares page-unit scaling with equivalent explicit
transforms across standard, TrueType, CFF, Type 1 and Type 3 text, all text modes,
images, forms, patterns and transparency. It also checks page-local defaults,
null inheritance, indirect box members, bounds and rotation allocation failures.
The optional `--reference` pass uses seven exact checks with a secondary rasterizer;
the primary comparison tool in the development environment ignores UserUnit.
No raster tolerance is widened to hide that difference.


`make test-pdf-font-cache` verifies decoded-outline reuse, independent font
programs, eviction, budget exhaustion and cleanup. The document allocation probe
uses an uncached render as its pixel baseline before testing cached rendering and
fault injection. This is an internal equivalence check, not an independent
font-rasterization reference claim.

`make test-pdf-flatness` samples 1,001 points on a cubic at seven tolerances
and four transforms (identity, nonuniform scale, rotation and shear),
checks graphics-state restoration and malformed operands, and exercises complete
PDFs with fills, strokes, clipping, Forms, Type 3 glyphs and native curved glyph
strokes. Very small tolerances must produce a bounded rejection when subdivision
cannot satisfy them; successful decoding alone is not the expected outcome.
Paint-time changes are tested separately from construction-time CTM capture.
The document allocation harness covers deferred subdivision failures and recovery.

`make test-pdf-content-scopes` checks unknown operators inside and outside nested
compatibility sections, marked-content operands, scope depth bounds, `q`/`Q`
independence, split `Contents` arrays, nested Form/Type 3 isolation and malformed
operands/delimiters. Allocation-failure cases verify cleanup on ignored and
rejected instructions.

`make test-pdf-flate-extent` tests the PDF stream-ending exception with embedded
TrueType/CFF/Type 1 programs, page content, image samples and filter chains. A
single CR, LF or CRLF after the zlib checksum is accepted; garbage, repeated
endings, another zlib stream, bad checksums and truncation are rejected. The
same padded inputs still fail the strict shared inflater. Allocation-failure
runs compare successful recovery with the unpadded rendering.

`make test-pdf-calibrated` covers calibrated source colors across text, paths,
images, patterns and shading meshes, including compressed object references
and allocation-failure cleanup. See [PDF colors](pdf-colors.md) for the output
profile assumptions and independent-comparison scope.

`make test-pdf-lab` checks signed Lab components, default and restricted ranges,
image and palette decoding, source-space matte removal, native text, patterns
and meshes. Indexed matte arrays use the base color space component count.

`make test-pdf-icc` generates matrix/tone-curve profiles and tests profile tags,
sampled/parametric curves, indirect and compressed metadata, cache lifetime,
source-color pipelines and allocation-failure cleanup. Unsupported LUT profiles
are rejected rather than interpreted as matrix profiles.

`make test-pdf-default-colors` checks default-space substitution across color
operators, images, Forms, patterns, text and shadings. It also tests the empty
initial Pattern color, combined fill/stroke and clipping without visible paint.

`make test-pdf-truetype-bearings` verifies horizontal glyph origins from hmtx
side bearings and glyf bounds. It covers short metric layouts, composite metric
selection, PDF advance overrides, cached rendering and clipping. Regenerate the
two owned bearing assets with `scripts/generate_pdf_bearing_fixtures.py`.

`make test-pdf-text-surfaces` checks sparse text groups, dense collections of short
text objects, empty/invisible text and clipping. The allocation harness compares
lazy text surfaces against eager backdrop initialization and verifies that group
work bounds include zero-alpha shape and explicit backgrounds.

`make test-pdf-font-advances` tests fallback CFF/Type 1 metrics and inherited
TrueType metrics, zero and negative advances, nested/final-component widths,
explicit PDF overrides, all text modes and the page glyph-operation limit.
Optional independent checks cover unhinted font advances, not damaged-PDF parity.
Fault injection requires a failed width decode to fail the page, never silently
move later characters by zero.

`make test-pdf-separation` checks tint functions, All/None semantics, source-space
interpolation, resource-scoped alternates, compressed function metadata and cache
bounds. Regenerate its three owned color assets with
`scripts/generate_pdf_separation_fixtures.py`. All uses the specification's
additive-channel oracle; see [PDF colors](pdf-colors.md) for comparison differences.

`make test-pdf-devicen` checks legacy tint sources through 32 components, wide
sampled functions, image predictors, palettes, masks, patterns and meshes. It also
checks Indexed Lab patch conversion and invalid NChannel declarations.
Regenerate its three owned assets with `scripts/generate_pdf_devicen_fixtures.py`.
Use `--reference` with the test script for independent comparisons; sampled
functions above the installed reference's 16-input limit use analytic/native
oracles instead. See [PDF colors](pdf-colors.md) for cache and work limits.

The standard-font target also checks the pinned extended-font data and compares
all 1,016 added face/glyph combinations with explicit embedded programs. It covers
all eight text modes, transforms and allocation recovery. These equivalence tests
preserve the original compact outlines and PDF advances; they do not establish
cross-renderer parity or complete font coverage.

`make test-pdf-substitution` checks the recognized nonembedded Latin family names,
encoding rules, PDF width precedence, clipping, transforms and allocation failures.
Regenerate its two owned assets with `scripts/generate_pdf_substitution_fixtures.py`.
The width-marker reference is analytic; the visible font-family page is not a
cross-renderer reference pass.

`make test-pdf-font-resources` checks zero- through 127-byte font resource names,
escapes, common-prefix collisions, scope/state restoration and allocation recovery.
It also checks empty Type 3 glyph and color-resource names without accepting empty
encoding identifiers. Regenerate its two analytic assets with
`scripts/generate_pdf_font_resource_fixtures.py`.

`make test-pdf-nchannel` checks process-only NChannel spaces across text, images,
patterns, shading and scoped resources. Its independent pass covers consistent
alternates; deliberately conflicting aggregate functions use the Process dictionary
as their specification-based oracle. Regenerate its three analytic assets with
`scripts/generate_pdf_nchannel_fixtures.py`. Spot mixtures remain unsupported.

`make test-pdf-icc-lut` checks 8/16-bit ICC table pipelines and their use in PDF
text, CMYK JPEG images, masks, palettes, patterns and shading. The optional
`--reference` pass adds raw pipeline and raster comparisons. Regenerate its three
analytic assets with `scripts/generate_pdf_icc_lut_fixtures.py`.
`scripts/compare_pdf_icc_profiles.py` can compare supplied profile files with an
installed independent color engine; it disables black-point compensation and
never adds those profiles to the repository.

`make test-pdf-intents` exercises PDF intent inheritance and ICC transform selection.
Run `python3 scripts/test_pdf_intents.py bin/test_pdf_pages bin/test_pdf_clip_oom --reference`
for the optional independent comparisons. The version 4 perceptual reference-medium
mapping is distinct from the unfinished general black-point compensation path.

`make test-pdf-icc-mab` checks version 4 forward curve/matrix/CLUT pipelines and
PDF integration. Its optional `--reference` pass compares raw stage outputs and
uncompensated rendered profile samples. Matrix overflow uses the specification's
clipping rule, with the raw-engine difference documented in the color guide.

`make test-pdf-font-descriptors` checks descriptor-selected Latin substitutes,
subset-prefixed aliases, metadata types and references, PDF layout and allocation
recovery. It compares the chosen programs with explicitly embedded copies; it does
not establish fidelity to unavailable original font designs.

`make test-pdf-combined-paint` checks implicit fill/stroke knockout objects against
explicit groups and analytic interior colors. Its optional `--reference` pass
qualifies selected color samples, not whole-image edge coverage. Three owned
assets cover alpha-as-shape, stroke-pattern holes and one-time mask application.

`make test-pdf-empty-glyphs` checks outline-less text, encoded spaces with ink,
empty .notdef glyphs, degenerate nonempty outlines, saved clips and deferred group
allocation. Large-page and 5,000-object cases check that nonpainting glyphs do not
consume implicit surfaces or clipping operations. Optional independent comparisons
cover mixed text with nonempty glyphs; outline-less-only clipping uses the ISO rule
as its oracle.

`make test-pdf-tt-vm` checks the native instruction-engine foundation and initializes
the 12 pinned font programs. Numeric tests cover signed F26Dot6 arithmetic, all
predefined rounding modes and 192 selectors each for SROUND and S45ROUND; state tests cover defaults,
axis vectors and scalar setters. Point cases cover CVT cut-ins, all three delta
ranges, relative-positioning flags, original design distances, contour interpolation,
paired-point alignment, reference-point shifts, whole-zone shifts, curve flags, line intersections and direct freedom-vector shifts. The specified-defaults
state case, repeated single-anchor IUP, mixed-zone flags/shifts and determinant-quantization cases report their independent-engine
differences separately. Point comparisons cover four uniform sizes. Preparation checks run both monochrome and
grayscale configurations at 1–96 ppem. Optional independent probes compare numeric
results, prepared CVT values, authored point programs and paired-glyph state
sequences using temporary glyphs. Another probe executes simple-glyph programs
from all 12 pinned faces at six sizes and compares every outline coordinate, curve flag and contour boundary.
Geometry and phantom points are supplied independently, so this does not qualify
the native glyph loader, composite assembly or PDF rasterization. The engine's
unsupported instructions and unwired rendering path remain documented separately
in [the TrueType VM guide](pdf-truetype-vm.md).


`make test-pdf-tt-loader` checks native simple/composite loading and phantom construction,
including malformed descriptions, allocation failures, shared initialization work
limits, inhibited hinting and instructions on contour-less glyphs. Optional
independent probes compare 9,330 complete native-loaded outlines and 576 authored
metric/phantom cases, including separately reported shortened-table differences. See [native loader coverage](pdf-truetype-loader.md) for the
font selection, sizes and remaining PDF integration limits. Composite probes also
exercise authored transforms and state sharing, selected pinned outlines in
monochrome/grayscale, and an optional all-composite sweep. Scaled-offset differences
from the comparison engines are reported separately from matching results.

## Standard spacing marks

The standard-font target checks the eight final commaaccent fallbacks against
embedded copies of their renamed CFF subsets. It covers all text modes, transforms,
width overrides, AFM placement and compact/extended/mark program switching, with
allocation failures and cached/uncached comparisons. An optional FontTools probe
compares native curves and widths. Supplying `--sources` to the spacing tests also
compares the pinned original OTFs; the generator checks hashes before reuse.

The visible spacing sample is decoded-unverified. Its companion advance-marker
asset has an analytic pixel oracle, which tests layout rather than glyph fidelity.

## TrueType sizing and device transforms

The loader target includes unequal-size simple/composite sweeps, authored
projection-dependent CVT/DELTA/positioning cases, fractional sizes with head flags,
and post-hint affine outlines. Device-shape tests cover ownership, allocation and
work limits, empty glyphs and overflow. MPS and first-write WCVTF lifetime
differences are reported separately. The four rejected pinned musical-note
programs reference IP points 32 and 33 despite having only 32 points, including
phantoms; they are not zero-span failures or successful matches.

## PDF text coverage

`make test-pdf-coverage` tests the bounded scan converter and native glyph bitmap
pipeline. `make test-pdf-text-antialias` tests the explicit page option with
analytic rectangle areas, opacity, Form inheritance, soft masks, patterns and
Sixel output. Optional curve comparisons use an independent font engine. Default
rendering and existing asset oracles remain unchanged; the new full-page sample
is decoded-unverified. See [coverage details](pdf-coverage.md).

## PDF native page hinting

`make test-pdf-page-hinting` covers the frame/cache adapter, PDF metrics independent
of phantom advances, Form-local font lifetimes, physical point-size cache keys,
invalid instructions and work exhaustion. Optional comparisons include authored
glyphs, pinned Latin fonts and stroke/clip device outlines. Native VM/loader source
includes are shared by the standalone and page allocation probes, so the new path
is fault-instrumented throughout. See [page-hinting scope](pdf-page-hinting.md).

### CFF hint inputs

`make test-pdf-cff` tests retained private parameters and optional Type 2 stem/mask
capture. FontTools adds independent private-field and charstring-input comparisons
using pinned fonts. Trace-on and trace-off outlines must match. Fault injection
checks cleanup after trace growth and malformed programs. These are input and
ownership checks, not hinted-pixel comparisons. See
[PostScript hint data](pdf-postscript-hints.md).

`make test-pdf-ps-gridfit` adds bounded CFF fitting, allocation/cancellation and
independent outline/bitmap checks. These exercise the standalone core rather
than PDF pages. See [PostScript grid-fitting scope](pdf-postscript-gridfit.md).

`make test-pdf-type1-gridfit` covers the standalone Type 1 profile, composite
origins, open-contour preservation, selected private fields, allocation failure
and shared budgets. Optional independent checks compare complete fitted fill
contours and grayscale bitmaps. Installed PFB fonts can be added explicitly;
the default suite uses authored programs.

`make test-pdf-type1-page-hinting` checks the Type 1 page option, composite
placement, FontMatrix transforms, PDF advances, text clipping, Form-local fonts,
Sixel output and allocation cleanup. Optional reference checks compare grayscale
pages. See [Type 1 page hinting](pdf-type1-page-hinting.md).

`make test-pdf-cff-page-hinting` checks the CFF page option, composite/CID
placement, FontMatrix rules, fallback programs, PDF advances, Sixel output and
allocation cleanup. Optional independent page masks cover lines, cubic curves,
rotations, reflections and shears. The default and hinted paths both test the
CID FD-only matrix regression. See [CFF page hinting](pdf-cff-page-hinting.md).

`make test-visual-review` includes offline option/fingerprint tests and 30 probe,
audit and simulated-terminal checks using owned one-page fixtures. The tests
compare named and positional PDF options, verify recorded pixel hashes, reject
stale probes without changing reports, and ensure profile changes cannot reuse
a previous rating. Simulated skips are kept only in temporary test reports; the
suite does not perform a manual corpus rating pass.

Type 1 counter-data checks run with `make test-pdf-type1`. They exercise chunk
ordering, malformed groups, budgets, allocation failure and compatibility drawing.
See [the counter-data guide](pdf-type1-counter-data.md) for the tested scope.
