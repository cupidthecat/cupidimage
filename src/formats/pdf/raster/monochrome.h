#ifndef CUPIDIMAGE_PDF_MONOCHROME_H
#define CUPIDIMAGE_PDF_MONOCHROME_H
#include "coverage.h"

/* Bottom-up binary coverage. Modes 0/1 use simple dropout, 4/5 smart dropout;
 * modes 2/3/6/7 disable it. Tags may carry a mode override at contour starts. */
int cupidimage_pdf_monochrome(const pdf_coverage_point *points, unsigned count, unsigned width,
                              unsigned height, unsigned scan_mode, int high_precision,
                              size_t *work_left, unsigned char **output);
#endif
