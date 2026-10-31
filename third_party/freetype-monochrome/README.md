# Monochrome glyph scan converter

`ftraster.inc` is adapted from FreeType 2.15.0's `src/raster/ftraster.c`.
`ftmisc.h` is unchanged. The shared image declarations and original FreeType
Project License are in `../freetype-raster/ftimage.h` and `../freetype-raster/FTL.TXT`.
This component uses that license, not the alternative GPL license. Retain those
files with source distributions and `THIRD_PARTY_NOTICES.md` with binaries.

Pinned upstream sources, tag `VER-2-15-0`:

- https://github.com/freetype/freetype/blob/VER-2-15-0/src/raster/ftraster.c
- https://github.com/freetype/freetype/blob/VER-2-15-0/src/raster/ftmisc.h

`manifest.json` records the original and adapted hashes and every source patch.
`scripts/check_pdf_monochrome_sources.py` reverses the patches and verifies both
sets of hashes without downloading anything.

## Adaptations

- Omit public raster factories and shared standalone state. The private wrapper
  supplies a worker, outline, pool and output buffer for each call.
- Charge loop iterations against the caller's work budget. Cancellation unwinds
  through a per-call jump buffer; the wrapper releases every owned allocation
  and discards partial output.
- Pass the worker into profile insertion and stepping helpers so they can charge
  that same budget. No drawing or dropout-selection rules are replaced.
- Check remaining pool capacity before forming profile or scanline pointers,
  avoiding out-of-bounds pointer arithmetic on exhausted pools.
- Reuse the pinned image declarations already included for grayscale coverage.

The wrapper validates contour structure, tags, point coordinates, dimensions and
allocation sizes before entering the converter. Leading point and tag entries
keep the converter's temporary pointer before the first point within allocated
storage. The packed bitmap is expanded to bottom-up 0/255 coverage for PDF paint
operations. There is no runtime font-library dependency.

The stored independent bitmap tests use version 2.14.3. Older versions can differ
at clipped line intersections; the optional comparison script reports their
version and skips them instead of silently accepting different pixels.
