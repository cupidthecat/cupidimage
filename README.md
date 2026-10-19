cupidimage is a dependency-free C99 library for decoding common image formats and rendering them in a terminal using ANSI 24-bit color blocks. It also ships with a small CLI for quick previews.

Terminal graphics output currently supports:
- ANSI truecolor escape sequences (`48;2;R;G;B` background colors)
- Kitty graphics protocol with auto-detection (direct transmission, zlib compression, chunking, animations)
- Sixel graphics protocol with auto-detection (octree color quantization, multiple dithering modes, animations)
- No iTerm2 inline image protocol support yet

Sixel `use_transparency` keeps pixels with alpha below 128 unpainted, including
when dithering is enabled. Other pixels use opaque palette colors. With the
option disabled, the renderer blends alpha against `background_color`.

Run the Sixel stream and palette regressions with Python 3:

    make test-sixel

Supported formats (decoder status):
- SVG: static subset with shapes (rect/circle/ellipse/line/polyline/polygon/path including arc commands), embedded images (`<image>` with `data:` URIs and local file hrefs), basic text (`<text>/<tspan>/<textPath>` via built-in bitmap font; includes `dominant-baseline`/`alignment-baseline` handling and UTF-8 bullet `•` fallback glyph), presentation + inline styles + basic `<style>` selectors (descendant, child `>`, adjacent `+`, and general sibling `~` combinators, plus basic `[attr]`/`[attr=value]` matching and `display`/`visibility`), transforms, viewBox/preserveAspectRatio, linear/radial gradients, patterns, `hsl()/hsla()` colors, stroke caps/joins/dash, clipPath, luminance and alpha masks (`mask-type`), basic `<use>` references for primitive shapes and `<symbol>` (with symbol `preserveAspectRatio` handling and wrapper style attributes), basic SVG markers (`marker-start`/`marker-mid`/`marker-end`, including `orient=\"auto-start-reverse\"` and improved join-angle placement), and filter primitives: `feGaussianBlur`, `feOffset`, `feColorMatrix`, `feComponentTransfer`, `feMorphology`, `feConvolveMatrix`, `feTurbulence`, `feDisplacementMap`, `feDiffuseLighting`, `feSpecularLighting`, `feTile`, `feImage` (including `data:` URIs), `feFlood`, `feBlend`, `feComposite`, `feMerge` (including Source/Background/FillPaint/StrokePaint filter inputs and primitive subregions). Basic sampled animation support for SMIL (`<set>`, `<animate>`, `<animateTransform>`) and CSS keyframes (subset: `transform`, `stroke-dashoffset`, `stop-color`) via `cupidimage_svg_options.animation_time` / CLI `--svg-time`. Supersampled rasterization for terminal preview. No scripting/DOM, no external web fetches, no custom font loading.
- PNG: non-interlaced and Adam7; color types 0/2/3/4/6 with 1/2/4/8/16-bit depths; palette + tRNS.
- JPEG: baseline/progressive DCT, Huffman-coded, 8-bit precision; grayscale/YCbCr/CMYK/YCCK; sampling up to 2x2. No arithmetic coding or >2x2 sampling.
- WebP: VP8 lossy keyframes; VP8X still images; VP8L lossless with transforms/color cache; ALPH chunk method 0. No animation or interframes.
- GIF: single-frame decode and animated GIF composition; palettes, interlacing, transparency; disposal methods 0-3; loop counts and per-frame delays.
- BMP: Windows/OS/2; BI_RGB/BI_RLE8/BI_RLE4/BI_BITFIELDS/BI_JPEG/BI_PNG; 1/4/8/16/24/32-bit; top-down/bottom-up; pre-multiplied alpha conversion.
- ICO/CUR: directory enumeration + page-based decode; PNG-compressed and DIB icons at 1/4/8/24/32-bit; AND mask transparency; CUR hotspots.
- TIFF: Classic + BigTIFF; byte orders II/MM; compression none/LZW/PackBits/Deflate/JPEG/CCITT RLE/G3/G4; photometric bilevel/grayscale(+alpha)/RGB/RGBA/palette/CMYK/YCbCr/Lab; strips/tiles; planar 1/2; predictor=2; orientation 1-8; multi-page IFDs (SubIFD pages via page APIs).
- TGA: types 0-3/9-11; uncompressed/RLE; 8/15/16/24/32-bit; all origins; color-mapped/RGB/grayscale; TGA 2.0 metadata + thumbnail + gamma/color correction.
- PDF: basic text, FlateDecode streams, gray/RGB/CMYK paint, line and cubic Bezier paths, nonzero/even-odd fills, and path clipping. Text uses built-in bitmap glyphs. Image XObjects support 8-bit DeviceRGB and DeviceGray samples, raw/FlateDecode streams, baseline and progressive grayscale/YCbCr JPEG images through DCTDecode, TIFF/PNG predictors, Decode arrays, and affine placement. Form XObjects render nested content with local resources, transforms, and bounding-box clipping. Previews use inherited MediaBox/CropBox rectangles, nonzero page origins, and clockwise rotation in multiples of 90 degrees. Complex fonts, text clipping modes, transparency, patterns, and annotations are unsupported.

Build (Makefile):
```sh
make
```

Build via script:
```sh
scripts/make.sh
scripts/make.sh lib
scripts/make.sh cli
```

Run all non-interactive regressions with Python 3 and GNU Make:

```sh
make -j2 all test
make -j2 all test CC=clang OBJDIR=obj/clang BINDIR=bin/clang \
  CFLAGS='-O1 -g -Wall -Wextra -std=c99 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie'
```

CI runs the full suite with GCC and Clang sanitizers. The interactive asset
viewers remain available through their individual targets.

Build just the static library or CLI:
```sh
make lib
make cli

Run the generated JPEG decoder regressions (requires Python 3):

    make test-jpeg

The tests check exact pixels for small baseline and progressive images, restart
boundaries, reordered components, and malformed headers and Huffman tables.
```

Install (static library + header):
```sh
sudo install -d /usr/local/lib /usr/local/include
sudo install -m 644 bin/libcupidimage.a /usr/local/lib/
sudo install -m 644 src/cupidimage.h /usr/local/include/
```

Install via script (supports uninstall):
```sh
scripts/install.sh
PREFIX=/usr/local scripts/install.sh
PREFIX=/usr/local scripts/install.sh --uninstall
```

Build (manual, static library):
```sh
mkdir -p obj bin
cc -Isrc -c src/cupidimage.c -o obj/cupidimage.o
cc -Isrc -c src/cupidimage_internal.c -o obj/cupidimage_internal.o
cc -Isrc -c src/cupidimage_png.c -o obj/cupidimage_png.o
cc -Isrc -c src/cupidimage_jpeg.c -o obj/cupidimage_jpeg.o
cc -Isrc -c src/cupidimage_webp.c -o obj/cupidimage_webp.o
cc -Isrc -c src/cupidimage_webp_tables.c -o obj/cupidimage_webp_tables.o
cc -Isrc -c src/cupidimage_webp_lossless.c -o obj/cupidimage_webp_lossless.o
cc -Isrc -c src/cupidimage_gif.c -o obj/cupidimage_gif.o
cc -Isrc -c src/cupidimage_bmp.c -o obj/cupidimage_bmp.o
cc -Isrc -c src/cupidimage_ico.c -o obj/cupidimage_ico.o
cc -Isrc -c src/cupidimage_tiff.c -o obj/cupidimage_tiff.o
cc -Isrc -c src/cupidimage_tga.c -o obj/cupidimage_tga.o
cc -Isrc -c src/cupidimage_pdf.c -o obj/cupidimage_pdf.o
cc -Isrc -c src/cupidimage_svg.c -o obj/cupidimage_svg.o
cc -Isrc -c src/cupidimage_svg_base.c -o obj/cupidimage_svg_base.o
cc -Isrc -c src/cupidimage_kitty.c -o obj/cupidimage_kitty.o
cc -Isrc -c src/cupidimage_sixel.c -o obj/cupidimage_sixel.o
ar rcs bin/libcupidimage.a obj/cupidimage.o obj/cupidimage_internal.o obj/cupidimage_png.o obj/cupidimage_jpeg.o obj/cupidimage_webp.o obj/cupidimage_webp_tables.o obj/cupidimage_webp_lossless.o obj/cupidimage_gif.o obj/cupidimage_bmp.o obj/cupidimage_ico.o obj/cupidimage_tiff.o obj/cupidimage_tga.o obj/cupidimage_pdf.o obj/cupidimage_svg.o obj/cupidimage_svg_base.o obj/cupidimage_kitty.o obj/cupidimage_sixel.o
```

Build the CLI test app:
```sh
mkdir -p bin
cc -Isrc src/cupidimage.c src/cupidimage_internal.c src/cupidimage_png.c src/cupidimage_jpeg.c src/cupidimage_webp.c src/cupidimage_webp_tables.c src/cupidimage_webp_lossless.c src/cupidimage_gif.c src/cupidimage_bmp.c src/cupidimage_ico.c src/cupidimage_tiff.c src/cupidimage_tga.c src/cupidimage_pdf.c src/cupidimage_svg.c src/cupidimage_svg_base.c src/cupidimage_kitty.c src/cupidimage_sixel.c src/cupidimage_cli.c -o bin/cupidimage -lm
```

Use the CLI:
```sh
./bin/cupidimage test.png 120 60
./bin/cupidimage test.jpg 120 60
./bin/cupidimage test.webp 120 60
./bin/cupidimage --fit test.png
./bin/cupidimage --svg-time 1.5 assets/svg/animation_basic.svg
./bin/cupidimage document.pdf
./bin/cupidimage --pdf-page 3 document.pdf
./bin/cupidimage --sixel test.png
./bin/cupidimage --sixel-colors 128 --sixel-dither atkinson test.jpg
./bin/cupidimage --ansi test.png  # Force ANSI mode (disable auto-detection)
```

PDF pages render at 96 DPI. CropBox is intersected with MediaBox, and page
rotation applies to both text and graphics. Direct and indirect page boxes and
rotation values are supported. Invalid boxes, cyclic parent chains, and previews
larger than 64 megapixels are rejected. The renderer still trims whitespace from
the resulting preview.

Run the generated PDF page regressions with Python 3:

    make test-pdf-pages

PDF image XObjects use the page's Resources dictionary, including inherited and
indirect resource dictionaries. Images use nearest-neighbor sampling. Flate
images accept predictor 1 (none), 2 (TIFF), and 10-15 (PNG), with 8-bit samples.
Predictor Colors and Columns must match the image when prediction is enabled.
The decoded sample data is limited to 64 MiB per image.

JPEG images support DCTDecode as a name or a single-filter array, including
ColorTransform 1 and Decode array inversion. Their frame dimensions, bit depth,
and component count must match the image dictionary. FlateDecode also accepts a
single-filter array. Raw RGB JPEG images, ColorTransform 0, JPX, filter chains,
image masks, soft masks, indexed color, and inline images
are outside the supported subset.
Drawing an invalid or unsupported XObject returns a load error.

Type 1 Form XObjects accept raw or FlateDecode streams. Their Matrix transforms
the content, and BBox clips text, paths, and images, including nested forms.
Forms use their own Resources dictionary when present, or the caller's resources
otherwise. Drawing a form preserves the caller's graphics state and current path.
Built-in text glyphs follow the current transformation matrix. General text
matrices and complex font rendering remain unsupported.

Forms are limited to 16 nesting levels, 4096 invocations, and 64 MiB of combined
decoded form content per page, counting repeated invocations. Recursive forms, transparency
groups, external-reference forms, filter chains, and form DecodeParms are rejected.

Run the generated form checks with Python 3:

    make test-pdf-forms

Path clipping supports `W` (nonzero winding) and `W*` (even-odd). The clip takes
effect after the path's painting operator, including `n`, and intersects the
current clip. It applies to paths, text, images, and nested forms. `q` and `Q`
save and restore the clip. Path coordinates retain their placement when the
transformation matrix changes. Clipping samples pixel centers without antialiasing.

Each page allows 4096 clip operations, 4096 segments per clipping path, 32 saved
graphics states, and 64 MiB of live clipping masks. A shared budget limits clipping
to 268,435,456 pixel-edge tests per page. Documents exceeding these limits return
a load error.

Run the clipping and allocation-failure checks with Python 3 and a C compiler:

    make test-pdf-clipping test-pdf-clip-oom

PDF colors support DeviceGray, DeviceRGB, and DeviceCMYK through `cs`/`CS` and
`sc`/`SC`/`scn`/`SCN`, as well as `g`/`G`, `rg`/`RG`, and `k`/`K`. Fill and
stroke color spaces are independent and follow `q`/`Q` state restoration.
Selecting a color space resets its color to black.

Named ColorSpace resources can alias device spaces, including inherited and
indirect dictionaries and indirect name values. Forms use their own resources
when present and otherwise inherit the caller's resources. Alias resolution is
bounded to reject cycles. Unsupported spaces, including patterns, calibrated
colors, indexed colors, and ICC profiles, return a load error. Invalid color
operands also return an error. CMYK uses the built-in RGB conversion without
color profiles.

Run the generated color and resource checks with:

    make test-pdf-colors

Run the generated image checks with Python 3:

    make test-pdf-images

When stdout is an interactive terminal, SVG files containing SMIL animation tags (`<animate>`, `<animateTransform>`, `<set>`) or CSS `animation`/`@keyframes` auto-play.
Use `--svg-time` to render a single sampled frame. When output is redirected to a file or pipe, SVGs render their initial state and animated GIFs render their first composed frame.

Link in your app (static library):
```sh
cc -Isrc your_app.c -L/usr/local/lib -lcupidimage -lm -o your_app
```

Example usage:
```c
#include "cupidimage.h"

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    cupidimage_image img;
    char err[128];
    if (!cupidimage_load_image_file(argv[1], &img, err, sizeof(err))) {
        fprintf(stderr, "load error: %s\n", err);
        return 1;
    }
    cupidimage_render_ansi(&img, stdout, 120, 60);
    cupidimage_free(&img);
    return 0;
}
```

GIF animation usage:
```c
cupidimage_animation anim;
if (!cupidimage_load_gif_animation_file("anim.gif", &anim, err, sizeof(err))) {
    fprintf(stderr, "load error: %s\n", err);
    return 1;
}
/* render anim.frames[i] with anim.delays[i] */
cupidimage_free_animation(&anim);
```

Kitty graphics protocol usage (auto-detection):
```c
#include "cupidimage.h"

int main(void) {
    cupidimage_image img;
    char err[128];

    if (!cupidimage_load_image_file("photo.jpg", &img, err, sizeof(err))) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }

    /* Auto-detect and use Kitty protocol if available */
    if (cupidimage_is_kitty_terminal()) {
        cupidimage_render_kitty(&img, stdout, 0, 0, err, sizeof(err));
    } else {
        cupidimage_render_ansi(&img, stdout, 120, 60);
    }

    cupidimage_free(&img);
    return 0;
}
```

Kitty animation usage:
```c
cupidimage_animation anim;
if (cupidimage_load_gif_animation_file("anim.gif", &anim, err, sizeof(err))) {
    if (cupidimage_is_kitty_terminal()) {
        cupidimage_render_kitty_animation(&anim, stdout, 0, 0, err, sizeof(err));
    } else {
        /* Manual frame-by-frame rendering */
        for (uint32_t i = 0; i < anim.frame_count; i++) {
            cupidimage_render_ansi(&anim.frames[i], stdout, 120, 60);
        }
    }
    cupidimage_free_animation(&anim);
}
```

Advanced Kitty options:
```c
cupidimage_kitty_options opts = {0};
opts.compression = 0;           /* disable compression */
opts.image_id = 42;            /* custom image ID */
opts.delete_previous = 1;       /* delete previous images */

cupidimage_render_kitty_with_options(&img, stdout, 0, 0, &opts, err, sizeof(err));
```

Passing NULL for Kitty options uses the defaults, including compression. Render
and delete functions return zero on output failure. Invalid or oversized images
are rejected before encoding; animation frames are checked before transmission.
Kitty animation uses `loop_count` as the number of plays, with zero for indefinite
playback. A frame delay of zero creates a gapless frame.

Non-interactive terminal regression checks compare emitted RGBA bytes, chunk
boundaries, animation controls, placement deletion, and ANSI output. They also
exercise invalid inputs and write failures:

```sh
make test-terminal-regressions
CC=clang CFLAGS='-O1 -g -std=c99 -fsanitize=address,undefined -fno-sanitize-recover=all' make test-terminal-regressions
```

Sixel graphics protocol usage (auto-detection):
```c
#include "cupidimage.h"

int main(void) {
    cupidimage_image img;
    char err[128];

    if (!cupidimage_load_image_file("photo.jpg", &img, err, sizeof(err))) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }

    /* Auto-detect and use Sixel protocol if available */
    if (cupidimage_is_sixel_terminal()) {
        cupidimage_render_sixel(&img, stdout, 0, 0, err, sizeof(err));
    } else if (cupidimage_is_kitty_terminal()) {
        cupidimage_render_kitty(&img, stdout, 0, 0, err, sizeof(err));
    } else {
        cupidimage_render_ansi(&img, stdout, 120, 60);
    }

    cupidimage_free(&img);
    return 0;
}
```

Advanced Sixel options:
```c
cupidimage_sixel_options opts = {0};
opts.max_colors = 128;          /* Use 128-color palette */
opts.dither_mode = 2;           /* Atkinson dithering */
opts.use_transparency = 0;      /* Blend alpha with background */
opts.background_color = 0x000000;  /* Black background for blending */
opts.pixel_aspect_ratio = 2.0f; /* Manual aspect ratio */
opts.delete_previous = 1;       /* Clear screen before render */

cupidimage_render_sixel_with_options(&img, stdout, 0, 0, &opts, err, sizeof(err));
```

Sixel animation usage:
```c
cupidimage_animation anim;
if (cupidimage_load_gif_animation_file("anim.gif", &anim, err, sizeof(err))) {
    if (cupidimage_is_sixel_terminal()) {
        cupidimage_sixel_options opts = {0};
        opts.max_colors = 256;
        opts.dither_mode = 1;  /* Floyd-Steinberg */
        cupidimage_render_sixel_animation_with_options(&anim, stdout, 0, 0, &opts, err, sizeof(err));
    } else {
        /* Manual frame-by-frame rendering */
        for (uint32_t i = 0; i < anim.frame_count; i++) {
            cupidimage_render_ansi(&anim.frames[i], stdout, 120, 60);
        }
    }
    cupidimage_free_animation(&anim);
}
```

SVG sampled animation usage:
```c
cupidimage_image img;
cupidimage_svg_options opts = {0};
opts.scale = 1.0f;
opts.dpi = 96.0f;
opts.supersampling = 2;
opts.animation_time = 1.5f; /* sample at 1.5 seconds */
if (!cupidimage_load_svg_file_with_options("anim.svg", &img, &opts, err, sizeof(err))) {
    fprintf(stderr, "load error: %s\n", err);
}
```

TIFF page APIs:
```c
int count = 0;
if (cupidimage_get_tiff_page_count(data, size, &count, err, sizeof(err))) {
    cupidimage_load_tiff_page(data, size, &img, 0, err, sizeof(err));
}
```

TGA metadata APIs:
```c
cupidimage_tga_metadata meta;
if (cupidimage_load_tga_with_metadata(data, size, &img, &meta, 1, err, sizeof(err))) {
    /* use img + meta */
    cupidimage_free_tga_metadata(&meta);
}
```

ICO/CUR directory APIs:
```c
cupidimage_ico_entry *entries = NULL;
int count = 0;
int is_cursor = 0;
if (cupidimage_ico_get_directory("icons.ico", &entries, &count, &is_cursor, err, sizeof(err))) {
    cupidimage_load_ico_page("icons.ico", 0, &img, err, sizeof(err));
    free(entries);
}
```

Debug logging:
```sh
cc -DCUPIDIMAGE_JPEG_DEBUG -Isrc -c src/cupidimage_jpeg.c -o obj/cupidimage_jpeg.o
cc -DCUPIDIMAGE_BMP_DEBUG -Isrc -c src/cupidimage_bmp.c -o obj/cupidimage_bmp.o
cc -DCUPIDIMAGE_TIFF_DEBUG -Isrc -c src/cupidimage_tiff.c -o obj/cupidimage_tiff.o
cc -DCUPIDIMAGE_ICO_DEBUG -Isrc -c src/cupidimage_ico.c -o obj/cupidimage_ico.o
cc -DCUPIDIMAGE_TGA_DEBUG -Isrc -c src/cupidimage_tga.c -o obj/cupidimage_tga.o
```

Non-interactive PNG regression checks:
```sh
make test-png
```
The tests generate PNGs in memory and check RGBA pixels, palette bounds, transparency keys, and a transition from dynamic Huffman coding to a stored DEFLATE block. They cover ordinary and Adam7-interlaced images without external image libraries or fixture downloads.

Lossless WebP regression checks generate VP8L and RIFF streams with constant and
mixed channels, one-symbol and variable-length code trees, truncated data, and
repeated transforms:

```sh
make test-webp
```

To run with Clang's address and undefined-behavior sanitizers in separate build directories:
```sh
make test-png CC=clang OBJDIR=obj/png-sanitize BINDIR=bin/png-sanitize \
  CFLAGS='-O1 -g -Wall -Wextra -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer'
```

Decoder safety regression checks:
```sh
make test-decoder-safety
```
These checks cover invalid SVG animation start times and icon height bounds. CI also runs them with address and undefined-behavior sanitizers.

TIFF bounds checks cover BigTIFF offsets and byte counts, compressed blocks,
JPEG data ranges, and large RowsPerStrip values. CSS allocation checks fail each
allocation in turn and verify that temporary selectors, declarations, keyframes,
and animation bindings are freed:

```sh
make test-tiff-bounds test-svg-css-oom
```

Quick BMP smoke test (expects failures for invalid samples):
```sh
scripts/test_bmp_assets.sh
```

Quick ICO/CUR smoke test (expects failures for invalid samples):
```sh
scripts/test_ico_assets.sh
```

Quick TGA smoke test (expects failures for invalid samples):
```sh
scripts/test_tga_assets.sh
```

Quick TIFF smoke test (expects failures for invalid samples):
```sh
scripts/test_tiff_assets.sh
```

Quick SVG smoke test:
```sh
scripts/make_test_svgs.sh
scripts/test_svg_assets.sh
```

Non-interactive CLI regression checks (requires Python 3):
```sh
make test-cli
```

Non-interactive SVG regression checks:
```sh
scripts/test_svg_regressions.sh
make test-svg-regressions
```

SVG dimensions must be finite, positive, and representable as 32-bit dimensions.
Positive fractional dimensions round to at least one pixel. Non-finite scale,
DPI, and animation-time options return an error. The supersampled canvas is
limited to 67,108,864 samples (256 MiB of RGBA data), including the supersampling
factor. Oversized output requests return an error before allocation.

Large shape bounds are clipped before conversion to raster coordinates. Run the
dimension, allocation-limit, and oversized-geometry checks with:

```sh
make test-svg-numeric
```

Non-interactive PDF/TIFF compression regression checks:
```sh
make test-deflate-containers
```
These generate complete documents in memory and check decoded pixels across stored and dynamic-to-stored DEFLATE blocks.

Non-interactive PDF graphics regression checks:
```sh
make test-pdf-graphics
```
These generate complete PDFs in memory and check curves, transforms, fill rules, open subpaths, and CMYK colors. Curves are approximated by line segments, and DeviceCMYK colors use a simple RGB conversion without color profiles.

Quick PDF test:
```sh
scripts/make_test_pdfs.sh
scripts/test_pdf_assets.sh
```

Non-interactive GIF regression checks cover frame dimensions, palette backgrounds,
truncated input, frame metadata, and allocation failure cleanup:
```sh
make test-gif-regressions
CC=clang CFLAGS='-O1 -g -std=c99 -fsanitize=address,undefined -fno-sanitize-recover=all' make test-gif-regressions
```


Todo
- [ ] For each image format provide option for checkerboard or white for alpha
- [ ] HEIC / HEIF
