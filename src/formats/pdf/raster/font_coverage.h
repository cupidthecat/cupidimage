#ifndef CUPIDIMAGE_PDF_FONT_COVERAGE_H
#define CUPIDIMAGE_PDF_FONT_COVERAGE_H
#include "../fonts/truetype.h"
#include "../fonts/type2.h"

/* Coverage rows are bottom-up, preserving font-space winding quantization. */
int cupidimage_pdf_font_coverage(const pdf_tt_outline *tt, const pdf_cff_outline *cff,
                                 const double matrix[6], int left, int top, int right, int bottom,
                                 size_t *work_left, unsigned char **output);
#endif
