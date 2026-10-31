#ifndef CUPIDIMAGE_PDF_CLIPPING_SCANLINE_H
#define CUPIDIMAGE_PDF_CLIPPING_SCANLINE_H
#include "../raster/region.h"

typedef struct {
    float x;
    int direction;
} pdf_scan_crossing;

/* Return sorted center-sampled crossings, or -1 on invalid input or exhaustion. */
int cupidimage_pdf_scanline(const pdf_region_edge *, unsigned, float, pdf_scan_crossing *,
                            size_t *);
#endif
