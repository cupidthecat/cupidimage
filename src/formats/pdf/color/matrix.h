#ifndef CUPIDIMAGE_PDF_COLOR_MATRIX_H
#define CUPIDIMAGE_PDF_COLOR_MATRIX_H
/* Row-major 3-by-3 transforms. Inputs may alias outputs. Inverse preparation
 * rejects singular or ill-conditioned matrices instead of returning an unstable
 * color transform; failure clears the output. */
int cupidimage_pdf_color_matrix_inverse(const double[9], double[9]);
int cupidimage_pdf_color_matrix_apply(const double[9], const double[3], double[3]);
#endif
