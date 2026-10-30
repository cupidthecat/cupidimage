#ifndef CUPIDIMAGE_PDF_ICC_CURVE_H
#define CUPIDIMAGE_PDF_ICC_CURVE_H
#include <stddef.h>

typedef struct {
    const unsigned char *samples;
    unsigned count, function;
    int parametric;
    double parameters[7];
} pdf_icc_curve;

int cupidimage_pdf_icc_curve_parse(const unsigned char *, size_t, pdf_icc_curve *, size_t *);
int cupidimage_pdf_icc_curve_value(const pdf_icc_curve *, double, double *);
#endif
