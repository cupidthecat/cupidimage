#ifndef CUPIDIMAGE_PDF_ICC_CURVE_INVERSE_H
#define CUPIDIMAGE_PDF_ICC_CURVE_INVERSE_H
#include "../pdf_icc_curve.h"

typedef struct {
    double first, last, low, high;
    double gamma, a, b, offset;
    int power;
} pdf_icc_inverse_piece;

typedef struct {
    pdf_icc_curve curve;
    pdf_icc_inverse_piece pieces[2];
    unsigned piece_count;
    int direction, valid;
    double low, high;
} pdf_icc_inverse_curve;

/* Borrows sample bytes from the parsed curve. ICC.1:2022 Annex F.1 defines
 * plateau selection and nearest-range recovery. Constant/nonmonotone curves
 * cannot be inverted. Preparation validates once; evaluation allocates nothing. */
int cupidimage_pdf_icc_inverse_prepare(const pdf_icc_curve *, pdf_icc_inverse_curve *, size_t *);
int cupidimage_pdf_icc_inverse_value(const pdf_icc_inverse_curve *, double, double *);
#endif
