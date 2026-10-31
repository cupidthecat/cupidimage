#ifndef CUPIDIMAGE_PDF_STROKE_UNION_H
#define CUPIDIMAGE_PDF_STROKE_UNION_H
#include <stddef.h>

typedef struct {
    double x, y;
} pdf_stroke_union_point;

/* A convex polygon (3 or 4 points), or an ellipse (count=1, its centre).
 * All ellipses share the two axis vectors supplied to the rasterizer. */
typedef struct {
    pdf_stroke_union_point points[4];
    unsigned count;
} pdf_stroke_union_shape;

/* Integrates the union, not the sum, of stroke primitive areas in each pixel.
 * Output rows increase with y; the caller owns the allocation. Failure clears
 * output. Limits: 65536 shapes, 16 MiB output, 256 intersecting shapes per pixel.
 * All geometry and intersection work consumes the caller's shared budget. */
int cupidimage_pdf_stroke_union(const pdf_stroke_union_shape *shapes, unsigned count,
                                const double axes[4], int left, int top, int right, int bottom,
                                size_t *work, unsigned char **output);
#endif
