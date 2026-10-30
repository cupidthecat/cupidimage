#ifndef CUPIDIMAGE_PDF_REGION_ELLIPSE_H
#define CUPIDIMAGE_PDF_REGION_ELLIPSE_H
#include "region.h"

typedef struct {
    long double axes[4], inverse[4], height, half_width, skew, extent_x;
} pdf_region_ellipse_frame;

int cupidimage_pdf_region_ellipse_frame(const double axes[4], pdf_region_ellipse_frame *frame);
long double cupidimage_pdf_region_ellipse_x(const pdf_region_ellipse_frame *,
                                            const pdf_region_ellipse *, long double, int);
long double cupidimage_pdf_region_ellipse_integral(const pdf_region_ellipse_frame *,
                                                   const pdf_region_ellipse *, int, long double,
                                                   long double, long double);
unsigned cupidimage_pdf_region_line_ellipse(const pdf_region_ellipse_frame *, long double,
                                            long double, long double, long double,
                                            const pdf_region_ellipse *, long double[2]);
unsigned cupidimage_pdf_region_ellipse_pair(const pdf_region_ellipse_frame *,
                                            const pdf_region_ellipse *, const pdf_region_ellipse *,
                                            long double[2]);
#endif
