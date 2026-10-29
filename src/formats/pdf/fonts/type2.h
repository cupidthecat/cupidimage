#ifndef CUPIDIMAGE_PDF_TYPE2_H
#define CUPIDIMAGE_PDF_TYPE2_H
#include "cff.h"

enum { PDF_CFF_MOVE, PDF_CFF_LINE, PDF_CFF_CURVE, PDF_CFF_CLOSE, PDF_CFF_END, PDF_CFF_SETPOINT };

typedef struct {
    double points[6];
    unsigned kind;
} pdf_cff_segment;

typedef struct {
    pdf_cff_segment *segments;
    unsigned count, capacity;
    double width, width_y;
    int composite;
    unsigned base, accent;
    double accent_x, accent_y;
} pdf_cff_outline;

int cupidimage_pdf_type2_outline(const pdf_cff_font *font, unsigned glyph,
                                 pdf_cff_outline *outline);
/* Captures one charstring only. Composite components retain separate hint contexts. */
int cupidimage_pdf_type2_outline_hints(const pdf_cff_font *font, unsigned glyph,
                                       pdf_cff_outline *outline, pdf_ps_hint_program *hints);
/* Shares a caller budget, with a per-charstring execution ceiling of 65,536 tokens. */
int cupidimage_pdf_type2_outline_budget(const pdf_cff_font *font, unsigned glyph,
                                        pdf_cff_outline *outline, pdf_ps_hint_program *hints,
                                        size_t *work_left);
void cupidimage_pdf_type2_free(pdf_cff_outline *outline);
int cupidimage_pdf_cff_glyph_outline(const pdf_cff_font *font, unsigned glyph,
                                     pdf_cff_outline *outline);
#endif
