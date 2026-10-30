#ifndef CUPIDIMAGE_PDF_TT_HINT_INTERNAL_H
#define CUPIDIMAGE_PDF_TT_HINT_INTERNAL_H
#include "../truetype_data.h"
#include "../truetype_vm/internal.h"
#include "hint.h"

static inline unsigned tt_hint_u16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static inline int32_t tt_hint_i16(const unsigned char *p) {
    unsigned value = tt_hint_u16(p);
    return value < 32768 ? (int32_t)value : (int32_t)value - 65536;
}

int cupidimage_pdf_tt_hint_phantoms(pdf_tt_hint *, unsigned, const pdf_tt_table *, int32_t[4][2]);
int cupidimage_pdf_tt_hint_scale(pdf_tt_hint *, unsigned, int32_t, int32_t *);
int cupidimage_pdf_tt_hint_round(pdf_tt_hint *, int32_t, int32_t *);
int cupidimage_pdf_tt_hint_take(pdf_tt_hint *, size_t *, size_t);
int cupidimage_pdf_tt_hint_load(pdf_tt_hint *, unsigned, unsigned[16], unsigned, unsigned *,
                                size_t *);
int cupidimage_pdf_tt_hint_simple(pdf_tt_hint *, unsigned, const pdf_tt_table *, size_t *);
int cupidimage_pdf_tt_hint_composite(pdf_tt_hint *, unsigned, const pdf_tt_table *, unsigned[16],
                                     unsigned, unsigned *, size_t *);
#endif
