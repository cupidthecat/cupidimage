#ifndef CUPIDIMAGE_PDF_POSTSCRIPT_PAGE_HINT_H
#define CUPIDIMAGE_PDF_POSTSCRIPT_PAGE_HINT_H
#include "cff_shape.h"
#include "type1_shape.h"

/* text_matrix maps text-space em units to device pixels, before FontMatrix.
 * Finite singular transforms retain the existing unfitted path (fitted = 0). */
int cupidimage_pdf_cff_page_shape(const pdf_cff_font *font, unsigned glyph,
                                  const double text_matrix[6], size_t *work_left,
                                  pdf_cff_outline *output, int *fitted);
int cupidimage_pdf_type1_page_shape(const pdf_type1_font *font, unsigned glyph,
                                    const double text_matrix[6], size_t *work_left,
                                    pdf_cff_outline *output, int *fitted);
#endif
