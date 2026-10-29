#ifndef CUPIDIMAGE_PDF_CFF_HINT_SHAPE_H
#define CUPIDIMAGE_PDF_CFF_HINT_SHAPE_H
#include "gridfit.h"

int cupidimage_pdf_cff_hint_outline(const pdf_cff_font *font, unsigned glyph,
                                    const pdf_ps_gridfit_size *size, pdf_cff_outline *output,
                                    size_t *work_left);
/* Transform an owned 1/64-pixel outline transactionally. Advances are unchanged. */
int cupidimage_pdf_ps_outline_transform(pdf_cff_outline *outline, const int32_t matrix[4],
                                        const int32_t offset[2], size_t *work_left);
#endif
