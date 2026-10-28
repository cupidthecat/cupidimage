#ifndef CUPIDIMAGE_PDF_PREDEFINED_CMAP_H
#define CUPIDIMAGE_PDF_PREDEFINED_CMAP_H
#include <stddef.h>

typedef struct {
    const char *name, *base;
    const unsigned char *data;
    size_t packed_length, length;
} pdf_predefined_cmap;

const pdf_predefined_cmap *cupidimage_pdf_predefined_cmap(const char *name);
#endif
