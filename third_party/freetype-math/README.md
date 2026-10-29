# Fixed-point vector normalization

The normalization routine in `src/formats/pdf/fonts/truetype_vm/vector.c` is
adapted from `FT_Vector_NormLen` in FreeType 2.13.3. It preserves the fixed-point
rounding used by that routine. This is a small source adaptation, with no runtime
font-library dependency.

Copyright (C) 1996-2024 by David Turner, Robert Wilhelm, and Werner Lemberg.
The unmodified FreeType Project License is in [FTL.TXT](FTL.TXT).

Source: [ftcalc.c, tag VER-2-13-3](https://github.com/freetype/freetype/blob/VER-2-13-3/src/base/ftcalc.c).
The SHA-256 of the retrieved source file is
`81332e600576482e3eb11c17ab6cdbd6cc81e7083f9f9beee6726315dcf80389`.
The SHA-256 of FTL.TXT is
`08c135755dd589039470f1fdbb400daaabaaa50d0b366d19cebff4d22986baa1`.

Changes from the source routine:

- Retain normalized vector components; omit the unused length result.
- Use C99 integer types and the instruction engine's error handling.
- Express signed fixed-point floor shifts and unsigned-to-signed conversion
  explicitly, with wide intermediates to avoid undefined signed arithmetic.
- Bound iteration and reject an invalid intermediate result.
- Convert the resulting 16.16 vector to 2.14 components for instruction state.

Keep the license with source distributions and include `THIRD_PARTY_NOTICES.md`
with binary distributions.
