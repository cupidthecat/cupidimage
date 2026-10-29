#ifndef CUPIDIMAGE_PDF_STANDARD_H
#define CUPIDIMAGE_PDF_STANDARD_H
#include <stddef.h>

typedef struct {
    const char *name;
    int width;
} pdf_standard_metric;

typedef struct {
    const char *name;
    const pdf_standard_metric *metrics;
    size_t count;
    const unsigned *encoding;
} pdf_standard_font;

const pdf_standard_font *cupidimage_pdf_standard_font(const char *name);
unsigned cupidimage_pdf_standard_glyph(const void *font, const char *name);

typedef struct {
    const char *name;
    const unsigned char *data;
    size_t length;
} pdf_standard_outline;

const pdf_standard_outline *cupidimage_pdf_standard_outline(const char *name);
const pdf_standard_outline *cupidimage_pdf_standard_mark(const char *name);

typedef struct {
    const char *name;
    const unsigned char *data;
    size_t length, decoded_length;
} pdf_extended_font;

const pdf_extended_font *cupidimage_pdf_extended_font(const char *name);
#endif
