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
void cupidimage_pdf_tt_hint_free(pdf_tt_hint *);
/* Bounded simple/composite loading and instruction execution.
 * Results remain owned by vm.zones[1], including four final phantom points. */
int cupidimage_pdf_tt_hint_glyph(pdf_tt_hint *, unsigned);
#endif
