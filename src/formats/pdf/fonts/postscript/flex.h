#ifndef CUPIDIMAGE_PDF_POSTSCRIPT_FLEX_H
#define CUPIDIMAGE_PDF_POSTSCRIPT_FLEX_H
#include "../type2.h"

/* Measure the join's perpendicular distance to the endpoint chord in device
 * space. Translation cancels. Equality retains the curves. Inputs are borrowed. */
int cupidimage_pdf_ps_flex_test(const double start[2], const pdf_cff_segment curves[2],
                                double threshold, const double matrix[4], int *flatten);
#endif
