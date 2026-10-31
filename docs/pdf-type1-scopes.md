# Type 1 font dictionary scopes

The Type 1 reader keeps font controls separate from unrelated dictionaries in the
font program. A nested `lenIV` entry no longer changes how the real font's
charstrings are decrypted. Nested Subrs and CharStrings values are consumed using
their declared binary lengths, then discarded without replacing the font's arrays.
A later real Private dictionary resets its hints, lenIV and Subrs.

The clear-text FontMatrix and FontType entries belong to the font dictionary.
Literal arrays, procedure objects and later sibling dictionaries do not supply
replacement entries. FontMatrix accepts six literal numbers in an array or a
procedure object. FontType, FontMatrix and lenIV require a completed definition;
computed forms are rejected rather than interpreted as their first literal value.

## Literal reader and definer aliases

The bounded syntax layer recognises the standard literal `readstring` procedure,
plain or access-qualified `def` procedures, and equivalent `put` procedures.
It checks the complete procedure and its definition before treating a name as an
alias. Merely placing a procedure after a name, then discarding it, does not define
that name.

Lookup starts in the current dictionary and proceeds through its active parents.
Local redefinitions hide inherited aliases; leaving a temporary dictionary restores
the parent binding. The reader preserves active clear-text bindings across the
transition to the encrypted program. Explicit userdict writes persist for that
font load. A newer global definition does not hide a local one. Re-entering
userdict or currentdict retains the same dictionary identity.

The encrypted program begins with read-only systemdict above the caller's
context. Definitions must target a writable dictionary. This is why an alias
installed with `userdict ... put` can be valid where an unqualified definition
at the start of the encrypted section is not.

RD and -| remain compatibility reader names until explicitly shadowed. Bindings
are local to one font load; they do not modify an interpreter, the filesystem or
another font's environment.

## Bounds and remaining work

The syntax layer allows 128 live bindings and 64 nested dictionary entries, with
1,048,576 work units for traversal and lookup. Existing source-size, binary-length,
glyph, subroutine and charstring-execution limits remain in effect. Failure frees
partial font data and temporary arrays.

This is a reader for the supported font-declaration subset, not an unrestricted
PostScript evaluator. Computed dictionaries and parameters, arbitrary procedures,
operator redefinitions and general saved-dictionary expressions remain outside
that subset. Font fitting and raster coverage have separate limits; see the
[Type 1 hinting guide](pdf-type1-page-hinting.md) and the
[replacement-trace guide](pdf-type1-replacements.md).

## Verification

`make test-pdf-type1-scopes` checks:

- 549 binding, scope, shadowing, dictionary-identity and work-budget cases.
- 204 font programs across plain/encrypted outer programs and three lenIV settings.
- 920 exact page controls, including stroke/fill, clipping, antialiasing and hinting.
- 2,552 page allocation-failure checks and 1,146 parser allocation-failure checks.

The tests compare actual glyph programs, outlines and hint metadata, not just
whether a font loaded. The owned `assets/pdf/type1-dictionary-scopes.pdf` has
paired controls for six declaration cases. Its entire page also matches a version
using ordinary declarations for both columns.

An optional `--reference` run executes the owned Type 1 programs directly in a
separate PostScript interpreter and compares 153 paired outputs. That checks the
language's dictionary behavior without relying on a PDF font loader's declaration
scanning shortcuts. It does not establish full font-raster parity.

The governing rules are the [Adobe Type 1 Font Format](https://adobe-type-tools.github.io/font-tech-notes/pdfs/T1_SPEC.pdf),
ISO 32000 font-program requirements, and the [PostScript Language Reference,
third edition](https://www.adobe.com/jp/print/postscript/pdfs/PLRM.pdf), particularly
dictionary-stack lookup and the begin, end, def, put and eexec operators.

## Multiple-master PDF instances

ISO 32000-1 section 9.6.2.3 and the corresponding PDF 2.0 requirement specify that
an embedded MMType1 instance uses an ordinary Type 1 snapshot. It is not a live
multiple-master font program. The existing MMType1 snapshot page test follows
that rule. Live multiple-master interpolation is a separate compatibility
extension, not a prerequisite for replaying compliant embedded snapshots.
