# PostScript outline hinting components

The blue-zone and hint-map components are adapted from FreeType 2.15.0, tag
`VER-2-15-0`, under the FreeType Project License. Their source headers retain
Adobe's copyright and patent-license terms. `FTL.TXT` is unchanged. Keep these
notices with source distributions and `THIRD_PARTY_NOTICES.md` with binaries.

Original files:

- https://github.com/freetype/freetype/blob/VER-2-15-0/src/psaux/psblues.c
- https://github.com/freetype/freetype/blob/VER-2-15-0/src/psaux/psblues.h
- https://github.com/freetype/freetype/blob/VER-2-15-0/src/psaux/pshints.c
- https://github.com/freetype/freetype/blob/VER-2-15-0/src/psaux/pshints.h
- https://github.com/freetype/freetype/blob/VER-2-15-0/src/psaux/psfixed.h

`upstream-sha256.json` records the original hashes. The two C files have `.inc`
extensions and their external includes are removed. Header includes are removed
as well. Private type and macro names have a `PSGF_` prefix so the components
can coexist with the coverage converter in fault-instrumented tests. The private bridge supplies only the required types, fixed-point
operations, masks, bounded arrays and callbacks. Interface structures in that
bridge are adapted from `psglue.h`, `psfont.h` and `psarrst.h`; their copyright
notice is retained there.

The surrounding C99 wrappers use CupidImage's CFF and Type 1 parsers and native
charstring interpreters. They do not use the upstream font loader or charstring
interpreter and have no runtime font-library dependency. State is per call. Allocation and
work-budget failures unwind to an owning context and discard partial output.

The current wrapper enables hinting without stem darkening or synthetic bold.
It accepts positive axis scales, validates bounded inputs, and returns an owned
outline quantized to 1/64 pixel. Separate native wrappers handle composite
placement. CFF and Type 1 share a page adapter. Broader flex policy remains
unfinished.
These limits are not claims about the complete upstream library's capabilities.
