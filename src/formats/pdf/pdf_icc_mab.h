#ifndef CUPIDIMAGE_PDF_ICC_MAB_H
#define CUPIDIMAGE_PDF_ICC_MAB_H
#include "pdf_icc_curve.h"
#include "pdf_icc_lut.h"

typedef struct {
    pdf_icc_curve a[4], m[3], b[3];
    pdf_icc_lut clut;
    double matrix[12];
    unsigned inputs;
    int has_clut, has_matrix;
} pdf_icc_mab;

int cupidimage_pdf_icc_mab_parse(pdf_icc_mab *, const unsigned char *, size_t, unsigned);
int cupidimage_pdf_icc_mab_value(const pdf_icc_mab *, const double *, double[3]);
#endif
