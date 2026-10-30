#ifndef CUPIDIMAGE_PDF_TYPE1_H
#define CUPIDIMAGE_PDF_TYPE1_H
#include "postscript/hints.h"
#include "type1/trace.h"
#include "type2.h"

typedef struct {
    char name[128];
    pdf_cff_slice code;
    unsigned order;
} pdf_type1_glyph;

typedef struct {
    unsigned char *private_data;
    size_t private_length;
    pdf_type1_glyph *glyphs;
    pdf_cff_slice *subrs;
    unsigned glyph_count, glyph_capacity, subr_count, encoding[256], notdef_slot;
    double matrix[6];
    int len_iv;
    pdf_ps_private_hints hints;
    unsigned unresolved_hints;
} pdf_type1_font;

int cupidimage_pdf_type1_init(pdf_type1_font *font, const unsigned char *data, size_t length,
                              size_t clear_length, size_t encrypted_length);
void cupidimage_pdf_type1_free(pdf_type1_font *font);
unsigned cupidimage_pdf_type1_name(const pdf_type1_font *font, const char *name);
int cupidimage_pdf_type1_outline(const pdf_type1_font *font, unsigned glyph,
                                 pdf_cff_outline *outline);
/* Captures one charstring; seac components must retain separate hint contexts. */
int cupidimage_pdf_type1_outline_hints(const pdf_type1_font *font, unsigned glyph,
                                       pdf_cff_outline *outline, pdf_type1_hint_trace *trace);
int cupidimage_pdf_type1_outline_budget(const pdf_type1_font *font, unsigned glyph,
                                        pdf_cff_outline *outline, pdf_type1_hint_trace *trace,
                                        size_t *work_left);
#endif
