# Documentation

Start with [building](building.md) and the [CLI reference](cli.md) to preview an
image. For embedding the library, use the [API guide](api.md) and the compilable
[preview example](../examples/preview.c).

- [Source architecture](architecture.md): module responsibilities and build boundaries.
- [Formats](formats.md): decoder features, rendering behavior, and limits.
- [Conformance tracker](format-conformance.md): standards, evidence, and unfinished work.
- [Testing](testing.md): focused suites, all-asset tests, and the full PDF corpus runner.
- [Test assets](test-assets.md): provenance, licenses, generation, and hashes.
- [Troubleshooting](troubleshooting.md): terminal problems, unsupported features, and safe use.

Successful decoding is not the same as matching the source image. The testing
reports keep those claims separate, especially for SVG text and PDF rendering.
