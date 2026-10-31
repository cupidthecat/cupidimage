#ifndef CUPIDIMAGE_PDF_FONT_MONOCHROME_H
#define CUPIDIMAGE_PDF_FONT_MONOCHROME_H
#include "../fonts/truetype_hint/hint.h"

int cupidimage_pdf_font_monochrome(const pdf_tt_hint_shape *shape, int left, int top, int right,
                                   int bottom, size_t *work_left, unsigned char **output);
#endif
