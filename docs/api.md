# Library API

Include [`cupidimage.h`](../src/cupidimage.h) and link `libcupidimage.a` plus the
platform math library where needed. The public header is the authoritative list
of declarations; internal codec headers are not part of the supported API.
The [preview example](../examples/preview.c) is a complete small caller.

## Images, ownership, and errors

`cupidimage_image` contains `width`, `height`, an allocated row-major RGBA buffer,
and cursor hotspot coordinates. Pixels have four 8-bit channels in R, G, B, A
order, with no row padding. Rows start at the top of the image. The hotspot fields
are relevant to CUR images and otherwise normally zero.

Initialize output objects to zero. Use a fresh output object for a load, and call
`cupidimage_free` when finished. Do not overwrite a live image with another load
without releasing it first. A shallow copy shares its pixel allocation; it is not
an independent image and must not be freed twice.

Most load and render functions return 1 on success and 0 on failure. Functions
with `err` and `errcap` accept a caller-owned error buffer. Check the return value
rather than matching diagnostic text, which is not a stable error-code API.
Terminal writes can fail too; do not ignore renderer return values.

The library has no general thread-safety guarantee. In particular, terminal output
and automatic protocol identifiers should be serialized by the caller.

## Loaders

`cupidimage_load_image(data, size, ...)` selects a decoder from memory contents.
`cupidimage_load_image_file(path, ...)` reads a file and performs the same selection.
These are single-image interfaces: they do not automatically return every frame
or page of a multi-image file.

Format-specific memory loaders use the names `cupidimage_load_png`, `load_jpeg`,
`load_webp`, `load_gif`, `load_bmp`, `load_ico`, `load_tiff`, `load_tga`,
`load_svg`, and `load_pdf`, each with the `cupidimage_` prefix. File wrappers exist
for all of those except the ICO single-image wrapper; use the generic file loader
or the ICO page API there.

A TGA type-0 file can succeed as a metadata-only document with no pixel allocation.
Check dimensions and pixel availability before passing such an image to a renderer.

See [formats](formats.md) for codec subsets and resource limits. A successful
load does not imply that every metadata item is exposed or that a document's
complex visual features are reproduced.

## Pages and icon directories

All library page indices are **zero-based**.

- PDF: `cupidimage_get_pdf_page_count`, `cupidimage_load_pdf_page`, and their
  `_file` wrappers enumerate and load pages. The default PDF loader loads page 0.
- TIFF: `cupidimage_get_tiff_page_count` and `cupidimage_load_tiff_page` operate on
  memory. The default TIFF loader loads the first image.
- ICO/CUR: `cupidimage_ico_get_directory` reads a file and allocates an array of
  `cupidimage_ico_entry`. Release that directory array with standard `free()`.
  `cupidimage_load_ico_page(path, page, ...)` selects an entry. CUR hotspot values
  are returned in the decoded `cupidimage_image`.

The directory entry width/height bytes use zero for 256 pixels. Directory fields
are metadata, not proof that the embedded image is valid; decoding can still fail.

## PDF text rendering options

`cupidimage_load_pdf_page_with_options` and
`cupidimage_load_pdf_page_file_with_options` accept a `cupidimage_pdf_options`
pointer. Set `text_antialias` to 1 for grayscale TrueType/CFF/Type 1 fill coverage.
Set `truetype_hinting` to 1 to execute the native legacy TrueType profile, and
`cff_hinting` to 1 for the bounded CFF profile. Set `type1_hinting` to 1 for the
bounded Type 1 profile. Each field accepts only 0 or 1.
Initialize the options struct to zero before setting fields. A null pointer or
all-zero fields preserve unhinted center-point fills. Page indices remain
zero-based.

The option propagates into Forms, patterns and soft masks. It does not change
PDF metrics or antialias strokes, clipping and bitmap glyphs. Hinting must be
requested separately for supported TrueType, CFF and Type 1 programs. See the
[CFF page guide](pdf-cff-page-hinting.md) and
[Type 1 page guide](pdf-type1-page-hinting.md) for their scope.
See [coverage limits and checks](pdf-coverage.md) before treating it as complete
font rendering. All returned images use the ordinary ownership rules above.

## GIF animation

`cupidimage_load_gif_animation` and `_file` return `cupidimage_animation`.
It owns its frame array, each frame's pixels, delays, and optional user-input flags.
Release the whole object with `cupidimage_free_animation`, not a series of
independent frees of shallow-copied frames.

Frames are composed canvases. `delays` are milliseconds. `loop_count` is used by
the terminal animation APIs as the number of plays, with zero for indefinite
playback. GIF metadata also includes the pixel-aspect byte, color resolution,
and per-frame user-input flags. Supporting a metadata field does not mean the
CLI provides an interactive user-input gate for it.

## SVG options

`cupidimage_load_svg_with_options` and `_file_with_options` take
`cupidimage_svg_options`. `cupidimage_svg_get_dimensions` reads dimensions without
requesting a complete render.

Options include explicit output width/height, scale, DPI, sampled animation time
in seconds, supersampling, and initial background alpha. Defaults and zero values
are handled by the SVG loader; a typical explicit configuration is:

```c
cupidimage_svg_options options = {0};
options.scale = 1.0f;
options.dpi = 96.0f;
options.supersampling = 2;
options.animation_time = 1.5f;
```

Scale, DPI, and animation time must be finite. Dimension and supersampling budgets
are checked before allocation. This is a sampled renderer, not a browser DOM or
scripting environment. Local image references are subject to the caller's process
filesystem access; see [safe use](troubleshooting.md).

## TGA metadata

`cupidimage_load_tga_with_metadata` can return author/comments, timestamp, job and
software identifiers, aspect/gamma information, extension attributes, and a
thumbnail. `apply_corrections` controls the decoder's gamma/color-correction path.
Release metadata-owned allocations with `cupidimage_free_tga_metadata` and release
the image separately with `cupidimage_free`.

## ANSI output

`cupidimage_render_ansi(image, stream, max_width, max_height)` emits truecolor
background escapes and spaces. It blends alpha against white. Positive bounds
limit output dimensions; nonpositive bounds do not impose a limit on that axis.
It is terminal data, not a portable image-file encoding.

## Kitty output

`cupidimage_is_kitty_terminal` is a detection helper. Use `cupidimage_render_kitty`
or `_with_options` for still images, and `cupidimage_render_kitty_animation` or
`_with_options` for a composed animation.

`cupidimage_kitty_options` selects image/placement IDs, compression, and deletion
of previous content. Passing NULL uses defaults, including compression. An
explicit zero-initialized options object disables compression unless the field
is set. A small image may be sent uncompressed even when compression is requested.

Deletion APIs are `cupidimage_kitty_delete_image`, `delete_placement`, and
`delete_all`, with the same prefix. Check their output status. The library sends
protocol commands; it does not guarantee the terminal executed them successfully.

## Sixel output

`cupidimage_is_sixel_terminal` detects likely support. Still and animation render
functions have basic and `_with_options` forms, parallel to Kitty.

`cupidimage_sixel_options` contains:

- `max_colors`: palette size, up to 256.
- `dither_mode`: 0 none, 1 Floyd–Steinberg, 2 Atkinson, 3 ordered.
- `use_transparency`: leave samples with alpha below 128 unpainted; other samples
  use opaque palette colors. This is not continuous alpha blending.
- `background_color`: packed RGB used when blending alpha instead.
- `pixel_aspect_ratio`: explicit positive ratio, or zero for automatic/default behavior.
- `delete_previous`: request clearing before rendering.

Use the convenience wrapper for default options, or set all relevant fields when
passing an explicit options structure. Palette quantization can change colors;
protocol correctness and image quality are separate test concerns.
