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
