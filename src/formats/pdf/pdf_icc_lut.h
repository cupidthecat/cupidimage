#ifndef CUPIDIMAGE_PDF_ICC_LUT_H
#define CUPIDIMAGE_PDF_ICC_LUT_H
#include <stddef.h>

typedef struct {
    const unsigned char *input, *clut, *output;
    size_t strides[4];
    unsigned inputs, bytes, grids[4], input_entries, output_entries;
} pdf_icc_lut;

/* Tables borrow validated, immutable profile storage. */
int cupidimage_pdf_icc_lut_parse(pdf_icc_lut *, const unsigned char *, size_t, unsigned, int);
int cupidimage_pdf_icc_clut_parse(pdf_icc_lut *, const unsigned char *, size_t, unsigned);
int cupidimage_pdf_icc_lut_value(const pdf_icc_lut *, const double *, double[3]);
#endif
