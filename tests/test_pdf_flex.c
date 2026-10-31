#include "formats/pdf/fonts/postscript/flex.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    unsigned checks = 0;
    const double start[2] = {0, 0};
    pdf_cff_segment curves[2] = {{{20, 0, 40, 4, 60, 4}, PDF_CFF_CURVE},
                                 {{80, 4, 100, 0, 120, 0}, PDF_CFF_CURVE}};
    for (unsigned turn = 0; turn < 4; turn++) {
        for (unsigned mirror = 0; mirror < 2; mirror++) {
            double transforms[][4] = {{1, 0, 0, 1}, {0, 1, -1, 0}, {-1, 0, 0, -1}, {0, -1, 1, 0}};
            double matrix[4];
            for (unsigned i = 0; i < 4; i++)
                matrix[i] = transforms[turn][i] * .125 * (mirror && i < 2 ? -1 : 1);
            for (unsigned threshold = 0; threshold <= 100; threshold++) {
                int flatten = -1;
                assert(cupidimage_pdf_ps_flex_test(start, curves, threshold, matrix, &flatten));
                assert(flatten == (threshold > 50));
                checks++;
            }
        }
    }
    double matrix[] = {.25, 0, .125, .0625};
    int flatten;
    assert(cupidimage_pdf_ps_flex_test(start, curves, 25, matrix, &flatten) && !flatten);
    assert(cupidimage_pdf_ps_flex_test(start, curves, 25.01, matrix, &flatten) && flatten);
    checks += 2;
    matrix[0] = NAN;
    assert(!cupidimage_pdf_ps_flex_test(start, curves, 50, matrix, &flatten) && !flatten);
    matrix[0] = .125;
    curves[1].kind = PDF_CFF_LINE;
    assert(!cupidimage_pdf_ps_flex_test(start, curves, 50, matrix, &flatten));
    curves[1].kind = PDF_CFF_CURVE;
    curves[0].points[0] = INFINITY;
    assert(!cupidimage_pdf_ps_flex_test(start, curves, 50, matrix, &flatten));
    checks += 3;
    printf("PostScript flex depth: %u transform, threshold and input checks\n", checks);
    return 0;
}
