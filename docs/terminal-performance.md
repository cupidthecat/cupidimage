# Terminal rendering performance

Dithered Sixel output builds a palette, then chooses the nearest palette color while applying the selected dither. The initial undithered mapping is skipped for Floyd-Steinberg, Atkinson, and ordered dithering. Output bytes are unchanged. If a dither error-buffer allocation fails, the renderer still falls back to the undithered mapping.

A 368-by-276 opaque gradient with 256 colors and Floyd-Steinberg dithering took a median 100.4 ms before this change and 63.6 ms afterward across seven interleaved optimized runs on one WSL host. This measures the encoder writing to memory, including output allocation. It excludes decoding, terminal transport, display latency, and GPU work.

Run the output-equivalence and existing Sixel checks with:

```sh
make test-sixel-dither test-sixel
```

The 196 frozen reference cases cover opaque, mixed-alpha, fully transparent, checkerboard, and one-pixel-wide/high images; 2/16/256-color palettes; all dither modes and unknown-mode fallback; and each dither error-buffer allocation failure. The references were captured from the unchanged renderer at `585c32c6b2ef8ce0d2d354eb32bd0530a9bbbbe8`, using the new test driver with `SIXEL_SOURCE` set to that renderer and `--record` enabled. They are checked in as `tests/fixtures/sixel_dither_reference.txt`.

For a file manager preview, fit the decoded image to the pixel viewport before uploading it through Kitty. Reusing encoded results avoids repeated compression or Sixel quantization when revisiting unchanged images. File metadata, dimensions, and repaint state must invalidate those caches.
