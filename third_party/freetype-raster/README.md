# Glyph coverage scan converter

`ftgrays.inc` is adapted from FreeType 2.15.0's `src/smooth/ftgrays.c`.
`ftimage.h` and `FTL.TXT` are unchanged upstream files. This component is used
under the FreeType Project License, not the alternative GPL license. Keep
`FTL.TXT` with source distributions and `THIRD_PARTY_NOTICES.md` with binaries.

Sources, tag `VER-2-15-0`:

- https://github.com/freetype/freetype/blob/VER-2-15-0/src/smooth/ftgrays.c
- https://github.com/freetype/freetype/blob/VER-2-15-0/include/freetype/ftimage.h
- https://github.com/freetype/freetype/blob/VER-2-15-0/docs/FTL.TXT

`upstream-sha256.json` records the original file hashes.

## Adaptations

- Compile the scan converter inside one private C99 translation unit. Omit the
  public raster entry points and their shared static allocation.
- Supply the cell pool and output buffer from the bounded wrapper. All state is
  per call. Allocation failure or work exhaustion discards the entire result.
- Charge work for outline callbacks, conic steps, cubic subdivision, cell lookup,
  rows and repeated render bands. Cancellation returns through a per-call jump
  buffer; the wrapper owns and frees every allocation after cancellation.
- Use unsigned outline tags, matching the supplied image header.
- Write small spans with `memset`, without compiler-specific fallthrough macros.
- Widen accumulated cell area to 64 bits to avoid wrapping densely overlapping
  contours. Point counts and coordinate bounds keep the sum within that range.
- Select the 64-bit line and quadratic arithmetic paths on all supported C99
  targets, including targets with 32-bit `long`.

The wrapper reserves a leading point and tag so the outline walker's temporary
pointer before the first entry remains within allocated storage. It validates
counts, contour ends, tags, coordinates, dimensions and output allocation size
before entering the converter. The original unused approximate `FT_HYPOT` macro
is not called by this scan converter.

Coverage is computed from signed cell areas. Curves are subdivided according to
the upstream device-space criterion. This is not exact integration of a Bézier
curve. The byte quantization can differ by one level with contour orientation;
tests retain that behavior explicitly rather than calling it exact area output.
