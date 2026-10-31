#ifndef CUPIDIMAGE_PDF_TYPE1_SYNTAX_H
#define CUPIDIMAGE_PDF_TYPE1_SYNTAX_H
#include "lexer.h"

enum { PDF_T1_READER = 1, PDF_T1_DEFINER = 2, PDF_T1_PUTTER = 4 };

enum {
    PDF_T1_NEW_DICTIONARY,
    PDF_T1_USER_DICTIONARY,
    PDF_T1_CURRENT_DICTIONARY,
    PDF_T1_SYSTEM_DICTIONARY
};

typedef struct {
    char name[128];
    unsigned dictionary, kind;
} pdf_type1_binding;

typedef struct {
    pdf_type1_binding bindings[128];
    unsigned count, depth, next_dictionary, dictionaries[65];
    size_t work;
    int failed;
} pdf_type1_syntax;

void cupidimage_pdf_type1_syntax_init(pdf_type1_syntax *);
int cupidimage_pdf_type1_syntax_take(pdf_type1_syntax *, size_t);
int cupidimage_pdf_type1_syntax_begin(pdf_type1_syntax *);
int cupidimage_pdf_type1_syntax_begin_dictionary(pdf_type1_syntax *, unsigned);
unsigned cupidimage_pdf_type1_syntax_dictionary(const pdf_type1_syntax *);
int cupidimage_pdf_type1_syntax_end(pdf_type1_syntax *);
unsigned cupidimage_pdf_type1_syntax_lookup(pdf_type1_syntax *, const char *);
int cupidimage_pdf_type1_syntax_definition(pdf_type1_syntax *, const char *, pdf_cff_slice,
                                           size_t *);
int cupidimage_pdf_type1_syntax_put_definition(pdf_type1_syntax *, const char *, pdf_cff_slice,
                                               size_t *, int);
int cupidimage_pdf_type1_syntax_end_definition(pdf_type1_syntax *, pdf_cff_slice, size_t *);
#endif
