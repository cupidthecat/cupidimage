#include "matrix.h"

void cupidimage_pdf_matrix_identity(double m[6]) {
    m[0] = 1.0;
    m[1] = 0.0;
    m[2] = 0.0;
    m[3] = 1.0;
    m[4] = 0.0;
    m[5] = 0.0;
}

void cupidimage_pdf_matrix_concat(double dst[6], const double left[6]) {
    double a = dst[0] * left[0] + dst[2] * left[1];
    double b = dst[1] * left[0] + dst[3] * left[1];
    double c = dst[0] * left[2] + dst[2] * left[3];
    double d = dst[1] * left[2] + dst[3] * left[3];
    double e = dst[0] * left[4] + dst[2] * left[5] + dst[4];
    double f = dst[1] * left[4] + dst[3] * left[5] + dst[5];
    dst[0] = a;
    dst[1] = b;
    dst[2] = c;
    dst[3] = d;
    dst[4] = e;
    dst[5] = f;
}
