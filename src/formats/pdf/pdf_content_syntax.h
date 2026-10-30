#ifndef CUPIDIMAGE_PDF_CONTENT_SYNTAX_H
#define CUPIDIMAGE_PDF_CONTENT_SYNTAX_H
#include <stddef.h>

int cupidimage_pdf_content_number(const unsigned char *, size_t, size_t *, double *);
int cupidimage_pdf_content_name(const unsigned char *, size_t, size_t *, char **, size_t *);
int cupidimage_pdf_content_literal(const unsigned char *, size_t, size_t *, char **, size_t *);
int cupidimage_pdf_content_hex(const unsigned char *, size_t, size_t *, char **, size_t *);
int cupidimage_pdf_content_array(const unsigned char *, size_t, size_t *, char **, size_t *);
int cupidimage_pdf_content_dictionary(const unsigned char *, size_t, size_t *, char **, size_t *);
#endif
