#ifndef CUPIDIMAGE_PDF_CID_SUBSTITUTE_DATA_H
#define CUPIDIMAGE_PDF_CID_SUBSTITUTE_DATA_H
#include <stddef.h>

typedef struct pdf_cid_font_source {
    const char *name, *registry, *ordering;
    unsigned supplement;
    const unsigned char *data;
    size_t length, decoded_length;
} pdf_cid_font_source;

extern const pdf_cid_font_source cupidimage_pdf_cid_gothic;
extern const pdf_cid_font_source cupidimage_pdf_cid_mincho;
#endif
