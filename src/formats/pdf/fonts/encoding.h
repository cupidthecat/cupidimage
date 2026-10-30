#ifndef CUPIDIMAGE_PDF_FONT_ENCODING_H
#define CUPIDIMAGE_PDF_FONT_ENCODING_H
#include <stdint.h>
int cupidimage_pdf_glyph_scalar(const char *name, uint32_t *scalar);
uint32_t cupidimage_pdf_glyph_unicode(const char *name);
const char *cupidimage_pdf_encoding_name(const char *encoding, unsigned code);
unsigned cupidimage_pdf_mac_roman_code(uint32_t scalar);
#endif
