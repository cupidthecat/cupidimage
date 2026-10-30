# PDF content syntax

Content streams carry drawing operators and their operands. Correct text bytes
and resource names are prerequisites for choosing glyphs; successful font loading
cannot compensate for a string or name decoded incorrectly.

## Names and strings

Content resource names decode hexadecimal `#XX` escapes once. This applies to
font, image, graphics-state, color-space and pattern selections. Escaped delimiter
bytes can belong to a name; an encoded NUL is invalid. A decoded hash is not
interpreted as the start of a second escape. Resource dictionaries and content
operands therefore compare the same decoded names.

Literal strings support balanced parentheses, escaped parentheses/backslashes,
standard escapes and one-to-three-digit octal escapes. Octal results retain their
low eight bits. Unescaped CR and CRLF become one LF byte. An escaped line ending
is a continuation and contributes no byte; an explicit `\r` escape remains CR.
Embedded NUL bytes are retained, not treated as string terminators.

Hex strings accept the PDF whitespace characters, including NUL, and pad an odd
last nibble with zero. They require a closing `>` and reject nonhex data. Literal
strings likewise require their closing parenthesis. Arrays and property
dictionaries balance nested delimiters without interpreting drawing operators
inside strings or comments.

## Text operators

TJ accepts only strings and numbers. It rejects nested arrays, dictionaries,
names, booleans, nulls, malformed strings and invalid numeric tokens rather than
silently skipping them. Tj, TJ and the quote operators require their specified
operand counts and an active text object. Text-positioning operators also require
an active object. Font and other text-state operators may still appear outside
BT/ET, where the PDF model permits them.

Numeric-looking content tokens must be complete PDF numbers; exponent syntax,
trailing letters and repeated decimal points are rejected. Numbers are no longer
silently truncated before conversion. Exceeding the operand stack is an error,
not a reason to discard extra operands and execute a different operation.

## Implementation and limits

`pdf_content_syntax.c` handles content numbers, names, strings and aggregate
operands. `pdf_render.c` executes operators and validates TJ element types.
The separate syntax module is also exercised directly by byte-level tests.

- Names and decoded literal/hex strings are limited to 1 MiB each.
- Arrays and property dictionaries are limited to 1 MiB of encoded content,
  including their outer delimiters.
- Literal-string nesting and aggregate nesting are each limited to 64 levels.
- Numeric tokens are limited to 63 bytes by the shared bounded number reader.
- The content operand stack holds at most 64 operands.
- Rendering work, glyph counts and decoded stream memory have additional limits
  described in the font, transparency and format guides.

These are resource bounds, not claims of complete document-syntax coverage.
The general PDF object reader, cross-reference handling and content parser have
different responsibilities and still need further conformance work.

## Verification

Run:

    make test-pdf-content-syntax
    python3 scripts/test_pdf_content_syntax.py bin/test_pdf_pages --reference

The C suite has 821 checks, including all byte values in hex/octal strings and
escaped names, malformed input, number boundaries and exact token-size limits.
The rendering suite has 69 checks and 36 independent comparisons. Four raw-CR/CRLF
comparisons use the secondary renderer because the primary renderer preserves CR
in those fixtures instead of normalizing it. Valid comparisons are exact RGB;
there is no widened pixel tolerance.

Two generated assets check escaped font selection and literal CRLF decoding.
Rebuild them with `scripts/generate_pdf_content_syntax_fixtures.py`. Allocation
failure tests cover token growth, malformed TJ elements, and string/name cleanup.

## Specification

These rules follow [ISO 32000-1](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf),
sections 7.2, 7.3.3, 7.3.4, 7.3.5 and 9.4.3. Text arrays contain encoded character
codes, not Unicode text; font encodings and CMaps determine their glyphs.

## Curve precision

The `i` operator and ExtGState `FL` entry accept flatness values from 0 through
100. Zero selects the device default. Positive values bound curve subdivision
in device pixels, after the page and graphics transforms. The renderer retains
its finer defaults of one quarter pixel for ordinary paths and one eighth pixel
for stroked native font outlines when the requested tolerance is larger.

Flatness belongs to the saved graphics state. Forms and Type 3 glyphs inherit it,
and their changes remain local. Indirect `FL` values are resolved; a null entry
leaves the current setting unchanged. Invalid values are rejected. Subdivision
has depth and segment limits: a path that exceeds them fails when painted or
used for clipping, rather than drawing an incomplete approximation.

Ordinary paths retain their transformed cubic control points until a painting
operator executes or `n` applies pending clipping. Flatness changes between path
construction and painting therefore use the setting in effect at paint time.
Later CTM changes do not move an already constructed path. `pdf_path.c` owns
construction, closure and bounded subdivision; flattened paths are shared by
fill, stroke and clipping for that operation. Default font fill and text-clip
coverage use analytic curve intersections rather than flattened edges. Opt-in
[glyph fill coverage](pdf-coverage.md) uses a separate font scan converter. These
flatness controls do not enable font hinting or change that converter.

The implementation follows ISO 32000-1:2008, Tables 57 and 58 and section 10.6.2,
and the corresponding flatness rules in ISO 32000-2:2020 section 10.7.2.
`make test-pdf-flatness` checks sampled geometric error, state restoration,
indirect/default values, Forms, Type 3 glyphs, native glyph strokes and bounded
failure on excessive precision requests.

## Compatibility and marked-content scopes

Unknown content operators fail outside a `BX`/`EX` compatibility section. Within
one, their operands are discarded and execution continues. Known operators still
validate their inputs; compatibility mode does not turn malformed text or drawing
instructions into valid content. Sections may nest up to 64 levels and must
balance. This follows ISO 32000-1:2008 section 7.8.2 and Table 32.

`MP`, `DP`, `BMC`, `BDC` and `EMC` validate tag/property operands and balanced
marked-content nesting, also bounded to 64 levels. Their metadata is not executed
as drawing instructions. These syntax checks do not implement optional-content
visibility, tagged-text extraction or accessibility behavior. Rendering-intent
names are accepted and validated, but color-managed intent transforms remain
unimplemented.

Compatibility and marked-content scopes are independent of `q`/`Q`. Each nested
Form, pattern or Type 3 program has its own scope; a caller's `BX` cannot make an
unknown operator in a Form valid. Page `Contents` arrays are concatenated before
execution, so scopes can span their stream boundaries. Dangling operands,
unbalanced sections and stray closing delimiters are rejected. Booleans and null
remain operands rather than being mistaken for operator names.
