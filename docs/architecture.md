# Source architecture

The installed API remains `src/cupidimage.h`. Implementation headers stay under
`src/` and are private; applications should not depend on their types or helper
symbols.

## Shared code and outputs

- `src/core/image.c` owns decoded-image cleanup.
- `src/core/io.c` handles file reads, loader wrappers, and shared errors.
- `src/core/load.c` detects formats and dispatches to codecs. This logic is no
  longer part of the JPEG decoder.
- `src/compression/zlib.c` supplies checked DEFLATE/zlib decoding and terminal
  compression. PNG, TIFF, and PDF share this implementation.
- `src/output/ansi/`, `kitty/`, and `sixel/` own terminal protocols. They consume
  RGBA images rather than parsing source-file formats.
- `src/cli/main.c` owns argument parsing, terminal selection, animation playback,
  and PDF page iteration.

## Format modules

Each codec lives under `src/formats/<format>/`. Smaller codecs remain one focused
translation unit. Larger ones have private contracts and separate responsibilities:

- PDF: public page APIs, document/object parsing and streams, font maps and the
  bitmap fallback, and graphics/content rendering.
- SVG: public APIs, parsing, styles, definitions, CSS, geometry, paint, text,
  rasterization, render orchestration, filter parsing, filter execution, and blur.
  Shared private scene and style types live in `svg_internal.h`.
- TIFF: public loading, directory/tag parsing, compression, strip/tile raster
  assembly, and color/orientation conversion.
- WebP: RIFF/alpha handling, VP8 decoding, VP8L decoding, and coding tables.

Some formats intentionally call other codecs: ICO can contain PNG; TIFF and PDF
can contain JPEG; SVG can contain embedded raster images. Those dependencies
reuse decoders rather than adding separate copies. Terminal outputs do not depend
on a particular input format.

Module boundaries do not imply full format conformance. For example, the PDF font
module still uses a limited bitmap fallback rather than a complete font engine.
See [formats](formats.md) and the [conformance tracker](format-conformance.md).

## Build and tests

The Makefile discovers library C files recursively, excluding `src/cli/`, and
keeps matching subdirectories under the object directory. A source-list dependency
tracks additions/removals. The static archive is recreated when rebuilt so stale
members from a previous layout cannot survive.

Every production module compiles separately. The allocation-failure tests
intentionally include the relevant implementation modules under fault-injection
macros; that is test-only instrumentation, not the production build model.

Refactors must preserve the public header, reference-pixel tests, malformed-input
behavior, and output protocols. The asset runner also records full decoded pixel
hashes, which can be compared before and after a behavior-preserving refactor.
These hashes show behavior preservation, not independent visual correctness.
