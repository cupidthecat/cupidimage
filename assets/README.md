# Test assets

Images and documents are organized by format: `png`, `jpeg`, `webp`, `gif`,
`bmp`, `ico` (including CUR), `tiff`, `tga`, `svg`, and `pdf`.

- `<format>/reference/`: owned, known-pixel fixtures for every supported input format.
- `webp/reference/`: includes 49 lossy VP8 references plus a lossless image.
- `pdf/pypdf/`: a pinned upstream PDF with its own license.
- `<format>/negative/`: intentionally malformed or nonstandard test input.
- `manifest.json`: file inventory, hashes, and explanations of negative fixtures.

Run `make test-assets` to catch mislabeled, changed, missing, or unlisted files.
Run `make test-format-pixels test-webp-pixels test-pdf-corpus` for the reference image/page tests.
See [provenance and validation details](../docs/test-assets.md) before reusing assets.
