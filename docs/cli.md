# Command-line reference

```text
cupidimage [options] image-file [max_width] [max_height]
```

Run the executable without arguments to print usage. Diagnostics go to stderr;
rendered terminal data goes to stdout. A load or rendering error returns a
nonzero status.

| Option | Meaning |
| --- | --- |
| `--fit` | Derive output bounds from the current terminal size. |
| `--width cols` | Positive width limit; overrides the positional/fit width. |
| `--height rows` | Positive height limit; overrides the positional/fit height. |
| `--ansi` | Force ANSI truecolor blocks and disable graphics autodetection. |
| `--sixel` | Force Sixel. An explicitly forced Sixel failure is an error. |
| `--sixel-colors N` | Palette size, 2–256; default 256. |
| `--sixel-dither MODE` | `none`, `floyd-steinberg`, `atkinson`, or `ordered`; default Floyd–Steinberg. |
| `--svg-time seconds` | Render a sampled SVG animation time instead of autoplay. |
| `--pdf-page number` | Render one PDF page, numbered from 1. |
| `--pdf-text-aa` | Enable grayscale outline text fills. Stroke and clip coverage remain binary; hinting is unchanged. |
| `--pdf-tt-hinting` | Execute the native legacy TrueType instruction profile. Off by default. |
| `--pdf-cff-hinting` | Enable bounded CFF outline hinting without stem darkening. Off by default; applies only to CFF programs. |
| `--pdf-type1-hinting` | Enable bounded Type 1 outline hinting without stem darkening. Off by default. |

If both `--ansi` and `--sixel` are present, ANSI wins. Without an explicit output
choice, the CLI tries detected Sixel support, then detected Kitty support, then
ANSI. Detection is heuristic; it does not guarantee that every terminal supports
every graphics-protocol feature. Bounds are interpreted by the selected backend,
including its pixel/cell aspect handling.

```sh
./bin/cupidimage --fit assets/png/basic.png
./bin/cupidimage --ansi assets/jpeg/cat.jpeg 80 40
./bin/cupidimage --sixel-colors 128 --sixel-dither atkinson assets/png/basic.png
./bin/cupidimage --svg-time 1.5 assets/svg/animation_basic.svg
./bin/cupidimage --pdf-page 2 assets/pdf/pdfjs/basicapi.pdf
./bin/cupidimage --pdf-text-aa --sixel assets/pdf/text-coverage.pdf
./bin/cupidimage --pdf-text-aa --pdf-tt-hinting --sixel assets/pdf/native-truetype-hinting.pdf
./bin/cupidimage --pdf-text-aa --pdf-cff-hinting --sixel assets/pdf/native-cff-hinting.pdf
```

The three PDF text-rendering switches are also available in the [visual reviewer](visual-review.md)
and [corpus audit](testing.md). Those tools record the selected options with
their results; rebuild the corresponding probes after pulling changes.

## Animation and pages

GIFs with multiple frames animate when stdout is a terminal. A pipe or redirected
output receives the first composed frame. The CLI attempts protocol animation
and can fall back to timed frame-by-frame drawing.

SVG files containing recognized SMIL or CSS animation constructs autoplay in an
interactive terminal unless `--svg-time` selects a sample. Redirected SVG output
uses the initial state. SVG animation and text support remain subsets of SVG.

PDF extensions are case-insensitive. Explicit PDF options select the PDF loader
even without a filename extension; the loader still validates the document header.
PDF files render every page unless `--pdf-page` is given. The CLI prints page
progress to stderr and separates page output with newlines. PDF page numbers in
the CLI are 1-based; the C page APIs are 0-based. Page previews use 96 DPI and preserve the page bounds, including whitespace.

PDF and SVG special CLI handling recognizes `.pdf`/`.PDF` and `.svg`/`.SVG`
extensions. The general image loader also examines file contents, but giving a
misnamed file to the CLI can bypass format-specific page or animation handling.

Redirected output is still terminal escape data, not a PNG or other image export.
Use the library's RGBA output when another application needs decoded pixels.
