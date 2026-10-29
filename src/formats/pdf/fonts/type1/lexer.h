#ifndef CUPIDIMAGE_PDF_TYPE1_LEXER_H
#define CUPIDIMAGE_PDF_TYPE1_LEXER_H
#include "../cff.h"

enum {
    PDF_T1_EOF,
    PDF_T1_WORD,
    PDF_T1_NAME,
    PDF_T1_NUMBER,
    PDF_T1_PROC,
    PDF_T1_OTHER,
    PDF_T1_ERROR,
    PDF_T1_IMMEDIATE
};

typedef struct {
    unsigned type;
    double number;
    char text[128];
    pdf_cff_slice body;
} pdf_type1_token;

int cupidimage_pdf_type1_space(unsigned char c);
pdf_type1_token cupidimage_pdf_type1_lex(pdf_cff_slice source, size_t *position, unsigned depth);
#endif
