#ifndef CUPIDIMAGE_PDF_JBIG2_DECODE_H
#define CUPIDIMAGE_PDF_JBIG2_DECODE_H
#include <stddef.h>
#include <stdint.h>

/* Decode the PDF embedded organization. Output samples use PDF white=1 polarity. */
int cupidimage_pdf_jbig2_decode(const unsigned char *, size_t, const unsigned char *, size_t,
                                uint32_t, uint32_t, size_t *, unsigned char **, size_t *);
#endif
