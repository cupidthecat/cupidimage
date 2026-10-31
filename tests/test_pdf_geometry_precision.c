#include "formats/pdf/pdf_render_internal.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned checks = 0;
    for (unsigned power = 20; power <= 45; power++) {
        for (unsigned axis = 0; axis < 2; axis++) {
            for (int sign = -1; sign <= 1; sign += 2) {
                double m[6], translation[6], amount = ldexp((double)sign, (int)power);
                cupidimage_pdf_matrix_identity(m);
                cupidimage_pdf_matrix_identity(translation);
                translation[4 + axis] = amount;
                cupidimage_pdf_matrix_concat(m, translation);
                translation[4 + axis] = .125;
                cupidimage_pdf_matrix_concat(m, translation);
                translation[4 + axis] = -amount;
                cupidimage_pdf_matrix_concat(m, translation);
                assert(m[4 + axis] == .125 && m[5 - axis] == 0);
                assert(m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 1);
                checks++;
            }
        }
    }
    uint32_t random = 72563;
    for (unsigned iteration = 0; iteration < 20000; iteration++) {
        double a[6], b[6], expected[6], actual[6];
        for (unsigned i = 0; i < 12; i++) {
            random = random * 1664525u + 1013904223u;
            (i < 6 ? a : b)[i % 6] = ((int)(random % 2049) - 1024) / 256.0;
        }
        for (unsigned axis = 0; axis < 2; axis++) {
            expected[axis] =
                (double)((long double)a[axis] * b[0] + (long double)a[axis + 2] * b[1]);
            expected[axis + 2] =
                (double)((long double)a[axis] * b[2] + (long double)a[axis + 2] * b[3]);
            expected[axis + 4] = (double)((long double)a[axis] * b[4] +
                                          (long double)a[axis + 2] * b[5] + a[axis + 4]);
        }
        memcpy(actual, a, sizeof(a));
        cupidimage_pdf_matrix_concat(actual, b);
        for (unsigned i = 0; i < 6; i++) {
            assert(actual[i] == expected[i]);
            checks++;
        }
        memcpy(actual, a, sizeof(a));
        memcpy(expected, a, sizeof(a));
        cupidimage_pdf_matrix_concat(expected, a);
        cupidimage_pdf_matrix_concat(actual, actual);
        assert(!memcmp(actual, expected, sizeof(actual)));
        checks++;
    }
    pdf_render_state state = {0};
    cupidimage_pdf_matrix_identity(state.gs.ctm);
    state.page_x0_pts = 16777216;
    state.page_top_pts = 16777217;
    double x, y;
    cupidimage_pdf_user_to_pixel(&state, 16777216.125, 16777216.25, &x, &y);
    assert(x == 1.0 / 6.0 && y == 1);
    checks++;
    puts(
        "PDF geometry precision: double CTMs, aliased composition and page-origin controls passed");
    printf("%u exact checks\n", checks);
    return 0;
}
