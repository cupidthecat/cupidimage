#ifndef CUPIDIMAGE_PDF_PAGE_HINT_H
#define CUPIDIMAGE_PDF_PAGE_HINT_H
#include "truetype_hint/hint.h"
struct pdf_font_program;
typedef struct pdf_page_hint pdf_page_hint;

void cupidimage_pdf_page_hint_free(pdf_page_hint **cache);
int cupidimage_pdf_page_hint_shape(pdf_page_hint **cache, struct pdf_font_program *program,
                                   unsigned glyph, const double matrix[6], int grayscale,
                                   size_t *work_left, pdf_tt_hint_shape *shape);
#endif
