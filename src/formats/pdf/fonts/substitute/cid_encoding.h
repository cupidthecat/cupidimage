#ifndef CUPIDIMAGE_PDF_CID_SUBSTITUTE_ENCODING_H
#define CUPIDIMAGE_PDF_CID_SUBSTITUTE_ENCODING_H
#include <stddef.h>
#include <stdint.h>
struct pdf_font_program;

enum {
    PDF_CID_SUBSTITUTE_NONE,
    PDF_CID_SUBSTITUTE_CP932,
    PDF_CID_SUBSTITUTE_UCS2,
    PDF_CID_SUBSTITUTE_UTF16,
    PDF_CID_SUBSTITUTE_CP936,
    PDF_CID_SUBSTITUTE_CP949,
    PDF_CID_SUBSTITUTE_CP950
};

unsigned cupidimage_pdf_cid_substitute_encoding(const char *name, const char *ordering);
int cupidimage_pdf_cid_substitute_cmap(struct pdf_font_program *program);
unsigned cupidimage_pdf_cid_substitute_glyph(const struct pdf_font_program *program,
                                             unsigned encoding, uint32_t code, size_t bytes,
                                             int vertical);
#endif
