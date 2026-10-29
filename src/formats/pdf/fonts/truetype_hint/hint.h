#ifndef CUPIDIMAGE_PDF_TT_HINT_H
#define CUPIDIMAGE_PDF_TT_HINT_H
#include "../truetype.h"
#include "../truetype_vm/vm.h"

typedef struct {
    pdf_tt_font font;
    pdf_tt_vm vm;
    pdf_tt_outline raw;
    size_t glyph_budget;
} pdf_tt_hint;

/* The checked font's immutable backing bytes must outlive this instance. Initialize once;
 * free before reinitializing. Failure invalidates the instance until freed. */
int cupidimage_pdf_tt_hint_init(pdf_tt_hint *, const pdf_tt_font *, int32_t, int32_t, int, size_t);
int cupidimage_pdf_tt_hint_init_xy(pdf_tt_hint *, const pdf_tt_font *, int32_t, int32_t, int32_t,
                                   int, unsigned, size_t);
void cupidimage_pdf_tt_hint_free(pdf_tt_hint *);

typedef struct {
    pdf_tt_point *points;
    unsigned count;
} pdf_tt_hint_shape;

/* Copy a completed glyph into device pixels after a 16.16 matrix and F26Dot6
 * offset. Remove the horizontal phantom origin before transforming. The caller
 * owns the returned shape and must free it before reuse; PDF advances remain
 * separate. This does not rasterize, clip, or modify the VM's glyph geometry. */
int cupidimage_pdf_tt_hint_shape_build(pdf_tt_hint *, const int32_t[4], const int32_t[2],
                                       pdf_tt_hint_shape *);
void cupidimage_pdf_tt_hint_shape_free(pdf_tt_hint_shape *);
/* Bounded simple/composite loading and instruction execution.
 * Results remain owned by vm.zones[1], including four final phantom points. */
int cupidimage_pdf_tt_hint_glyph(pdf_tt_hint *, unsigned);
#endif
