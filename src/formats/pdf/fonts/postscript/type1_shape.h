#ifndef CUPIDIMAGE_PDF_TYPE1_HINT_SHAPE_H
#define CUPIDIMAGE_PDF_TYPE1_HINT_SHAPE_H
#include "../type1.h"
#include "gridfit.h"

int cupidimage_pdf_type1_hint_outline(const pdf_type1_font *font, unsigned glyph,
                                      const pdf_ps_gridfit_size *size, pdf_cff_outline *output,
                                      size_t *work_left);
#endif
