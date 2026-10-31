# Documentation

Start with [building](building.md) and the [CLI reference](cli.md) to preview an
image. For embedding the library, use the [API guide](api.md) and the compilable
[preview example](../examples/preview.c).

- [Source architecture](architecture.md): module responsibilities and build boundaries.
- [Formats](formats.md): decoder features, rendering behavior, and limits.
- [PDF password security](pdf-security.md): credentials, encrypted objects, crypt filters and limits.
- [PDF objects and revisions](pdf-objects.md): cross-reference tables, streams, generations and limits.
- [PDF annotation appearances](pdf-annotations.md): stored drawing streams, widget states, visibility and placement.
- [PDF optional content](pdf-optional-content.md): default layer states, membership expressions and hidden text.
- [PDF content syntax](pdf-content.md): names, text strings, operands and parser limits.
- [PDF dense paths](pdf-dense-paths.md): bounded path growth, coincident edges and localized clip work.
- [PDF vector coverage](pdf-graphics-coverage.md): path antialiasing, Type 3 graphics and paint composition.
- [PDF clipping](pdf-clipping.md): geometric intersections, fractional coverage, state ownership and limits.
- [CFF descendant binding](pdf-cff-bindings.md): direct glyph indices and CID-keyed charset selection.
- [Embedded CID subset recovery](pdf-subset-fonts.md): bounded compatibility for damaged font locations and selectors.
- [PDF fonts and text](pdf-fonts.md): glyph selection, outline support, tests and remaining gaps.
- [PostScript flex hints](pdf-flex.md): Type 2 depth selection, Type 1 pixel-phase adjustment and comparison limits.
- [TrueType instruction engine](pdf-truetype-vm.md): tested instructions, preparation state and remaining profile limits.
- [Native TrueType loading](pdf-truetype-loader.md): glyphs, phantom points and preparation lifetimes.
- [Monochrome TrueType rendering](pdf-monochrome.md): scan policies, dropout pixels and bounded raster tests.
- [TrueType composites](pdf-truetype-composites.md): component transforms, attachments, shared state and bounds.
- [PDF colors](pdf-colors.md): device and calibrated source conversion, tests and limits.
- [PDF blending colors](pdf-blending-colors.md): native group encodings, inverse curves, lookup transforms and luminosity.
- [PDF CMYK blending](pdf-cmyk-blending.md): ink components, black generation, state and cache growth.
- [PDF images](pdf-images.md): native samples, DCT transforms, filter chains and image limits.
- [PDF image interpolation](pdf-image-interpolation.md): decoded-component filtering, masks, Matte and profile limits.
- [PDF patterns](pdf-patterns.md): tiling, backdrop-dependent evaluation and cache limits.
- [PDF shading](pdf-shading.md): gradients, triangle streams, interpolation and validation limits.
- [PDF transparency](pdf-transparency.md): compositing, RGB/gray groups, validation and remaining limits.
- [PDF group work](pdf-group-work.md): isolated mask storage, clipped color evaluation and function endpoints.
- [PDF standards](pdf-standards.md): reference editions, extensions and applicability.
- [PDF feature inventory](pdf-feature-matrix.md): checklist requirements, evidence and unassessed work.
- [Conformance tracker](format-conformance.md): standards, evidence, and unfinished work.
- [Interactive visual review](visual-review.md): render selections and save manual ratings.
- [Testing](testing.md): focused suites, all-asset tests, and the full PDF corpus runner.
- [Test assets](test-assets.md): provenance, licenses, generation, and hashes.
- [Troubleshooting](troubleshooting.md): terminal problems, unsupported features, and safe use.

Successful decoding is not the same as matching the source image. The testing
reports keep those claims separate, especially for SVG text and PDF rendering.
