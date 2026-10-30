# CFF glyph selection in CID fonts

A PDF Type 0 font and a CFF CID-keyed program describe different layers of the
font model. A Type 0 font can contain either kind of CFF Top DICT. Rejecting a
name-keyed CFF program solely because the PDF font uses CIDs loses a supported
case in ISO 32000-1 and ISO 32000-2, 9.7.4.2.

The renderer now follows both selection rules:

- A CID-keyed CFF program maps the PDF CID through its charset to a glyph index.
- A name-keyed CFF program uses the PDF CID directly as a CharStrings index.

In the second case, a charset SID is not a CID-to-glyph map. The CFF Encoding and
optional PDF ToUnicode map also do not select the glyph. The program remains
name-keyed internally, so deprecated Type 2 accent composites still resolve their
component names through StandardEncoding.

Raw FontFile3 CFF and OpenType-wrapped CFF use the same rule. Invalid direct indices
consult the encoding CMap's notdef mapping and then CID zero. PDF DW/W and DW2/W2
continue to determine spacing and vertical origins. Neither CFF widths nor
extraction text replace those metrics. Simple PDF fonts still reject CID-keyed CFF
programs rather than interpreting their CIDs as glyph names.

## Verification

`make test-pdf-cff-bindings` runs 236 page checks and 428 allocation-failure checks.
The controls cover raw and wrapped programs, all eight text rendering modes,
horizontal and vertical writing, both rendering profiles, custom matrices, accent
composites, notdef selection, spacing and Form-local cache lifetimes. The font-cache
probe also checks direct-index selection and presence across the full CID range
and the first out-of-range value.

The two reference assets, cff-direct-cid.pdf and opentype-cff-direct-cid.pdf, draw
literal blue rectangles. Their source programs and regeneration scripts are owned
fixtures; no sampled document's embedded font is redistributed.

Three pinned documents that previously failed font admission now render with all
five PDF options. Their mean absolute channel errors against an independent render
are 3.58590, 0.00385 and 0.00535, with maximum differences 242, 168 and 217. These
are successful renders with remaining differences, not complete fidelity results.
Two other selected documents still fail earlier during catalog resolution.

This change does not add CFF2, variable-font behavior, non-embedded CID font
substitution or broader font coverage. See the [font guide](pdf-fonts.md) and the
[feature inventory](pdf-feature-matrix.json) for the remaining work.

References: ISO 32000-1:2008 and ISO 32000-2:2020, 9.7.4.2 and 9.7.6.3; Adobe
[Technical Note 5176, CFF](https://adobe-type-tools.github.io/font-tech-notes/pdfs/5176.CFF.pdf)
and [Technical Note 5177, Type 2 charstrings](https://adobe-type-tools.github.io/font-tech-notes/pdfs/5177.Type2.pdf).
