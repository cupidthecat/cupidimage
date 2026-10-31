#ifndef CUPIDIMAGE_PDF_JBIG2_HALFTONE_H
#define CUPIDIMAGE_PDF_JBIG2_HALFTONE_H
#include "internal.h"

typedef struct {
    uint32_t count, width, height;
    pdf_jb2_bitmap **patterns;
} pdf_jb2_patterns;

typedef struct {
    uint32_t width, height, grid_width, grid_height;
    int32_t grid_x, grid_y;
    uint16_t vector_x, vector_y;
    unsigned mmr, template_id, skip, combination, default_pixel;
    const pdf_jb2_patterns *patterns;
} pdf_jb2_halftone;

int cupidimage_pdf_jb2_patterns_decode(pdf_jb2_context *, const unsigned char *, size_t, unsigned,
                                       unsigned, uint32_t, uint32_t, uint32_t, pdf_jb2_patterns **);
int cupidimage_pdf_jb2_halftone_decode(pdf_jb2_context *, const unsigned char *, size_t,
                                       const pdf_jb2_halftone *, pdf_jb2_bitmap **);
#endif
