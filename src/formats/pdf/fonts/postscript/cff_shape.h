#ifndef CUPIDIMAGE_PDF_CFF_HINT_SHAPE_H
#define CUPIDIMAGE_PDF_CFF_HINT_SHAPE_H
#include "gridfit.h"

int cupidimage_pdf_cff_hint_outline(const pdf_cff_font *font, unsigned glyph,
                                    const pdf_ps_gridfit_size *size, pdf_cff_outline *output,
                                    size_t *work_left);
#endif
