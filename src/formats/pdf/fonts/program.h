#ifndef CUPIDIMAGE_PDF_FONT_PROGRAM_H
#define CUPIDIMAGE_PDF_FONT_PROGRAM_H
#include "truetype.h"
#include "type2.h"
#include "type1.h"
struct pdf_font_program {
    pdf_tt_font font;
    pdf_cff_font cff;
    int is_cff, is_type1;
    pdf_type1_font type1;
    unsigned char *bytes, *gid_map;
    size_t length, gid_length;
    unsigned glyphs[256];
    int cid;
};
#endif
