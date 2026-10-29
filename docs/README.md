# Documentation

Start with [building](building.md) and the [CLI reference](cli.md) to preview an
image. For embedding the library, use the [API guide](api.md) and the compilable
[preview example](../examples/preview.c).

- [Source architecture](architecture.md): module responsibilities and build boundaries.
- [Formats](formats.md): decoder features, rendering behavior, and limits.
- [PDF objects and revisions](pdf-objects.md): cross-reference tables, streams, generations and limits.
- [PDF content syntax](pdf-content.md): names, text strings, operands and parser limits.
- [PDF fonts and text](pdf-fonts.md): glyph selection, outline support, tests and remaining gaps.
- [TrueType instruction engine](pdf-truetype-vm.md): tested foundations and the unfinished hinting integration.
- [Native TrueType loading](pdf-truetype-loader.md): glyphs, phantom points and preparation lifetimes.
- [TrueType composites](pdf-truetype-composites.md): component transforms, attachments, shared state and bounds.
- [PDF colors](pdf-colors.md): device and calibrated source conversion, tests and limits.
- [PDF images](pdf-images.md): native samples, DCT transforms, filter chains and image limits.
- [PDF patterns](pdf-patterns.md): tiling, backdrop-dependent evaluation and cache limits.
- [PDF shading](pdf-shading.md): gradients, triangle streams, interpolation and validation limits.
- [PDF transparency](pdf-transparency.md): compositing, RGB/gray groups, validation and remaining limits.
- [PDF standards](pdf-standards.md): reference editions, extensions and applicability.
- [PDF feature inventory](pdf-feature-matrix.md): checklist requirements, evidence and unassessed work.
- [Conformance tracker](format-conformance.md): standards, evidence, and unfinished work.
- [Interactive visual review](visual-review.md): render selections and save manual ratings.
- [Testing](testing.md): focused suites, all-asset tests, and the full PDF corpus runner.
- [Test assets](test-assets.md): provenance, licenses, generation, and hashes.
- [Troubleshooting](troubleshooting.md): terminal problems, unsupported features, and safe use.

Successful decoding is not the same as matching the source image. The testing
reports keep those claims separate, especially for SVG text and PDF rendering.
