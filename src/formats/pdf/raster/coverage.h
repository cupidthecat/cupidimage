#ifndef CUPIDIMAGE_PDF_COVERAGE_H
#define CUPIDIMAGE_PDF_COVERAGE_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t x, y;      /* Device coordinates, in 1/64 pixel units. */
    unsigned char tag; /* 0: quadratic control, 1: on-curve, 2: cubic control. */
    unsigned char end;
} pdf_coverage_point;

/* Rows increase with y. The result is owned by the caller and is NULL on failure.
 * Geometry is limited to +/-32768 pixels, dimensions to 32767, points to 65535,
 * and the output to 16 MiB. Work is charged even when rendering fails. */
int cupidimage_pdf_coverage(const pdf_coverage_point *points, unsigned count, unsigned width,
                            unsigned height, int even_odd, size_t *work_left,
                            unsigned char **output);
#endif
