#ifndef CUPIDIMAGE_PDF_FONT_PROGRAM_H
#define CUPIDIMAGE_PDF_FONT_PROGRAM_H
#include "truetype.h"
struct pdf_tt_program {
    pdf_tt_font font;
    unsigned char *bytes, *gid_map;
    size_t length, gid_length;
    unsigned glyphs[256];
    int cid;
};
#endif
