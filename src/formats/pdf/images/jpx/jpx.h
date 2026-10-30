#ifndef CUPIDIMAGE_PDF_JPX_H
#define CUPIDIMAGE_PDF_JPX_H

#include <stddef.h>
#include <stdint.h>

#define PDF_JPX_COMPONENTS 64

typedef struct {
    uint32_t x0, y0, width, height, dx, dy;
    unsigned precision, is_signed;
    const uint32_t *samples;
} pdf_jpx_plane;

typedef struct {
    uint32_t x0, y0, width, height;
    unsigned components;
    uint32_t *storage;
    pdf_jpx_plane planes[PDF_JPX_COMPONENTS];
} pdf_jpx_samples;

/* Decode a bounded raw codestream; container/color interpretation stays separate. */
int cupidimage_pdf_jpx_codestream(const unsigned char *, size_t, pdf_jpx_samples *, size_t *,
                                  char *, size_t);
void cupidimage_pdf_jpx_samples_free(pdf_jpx_samples *);

#endif
