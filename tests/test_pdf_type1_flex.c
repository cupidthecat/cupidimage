#include "formats/pdf/fonts/postscript/type1_flex.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    double input[12] = {115, -10, 125, 0, 150, 0, 175, 0, 185, -10, 200, -10};
    double reference[2] = {150, -10};
    double matrix[6] = {.016, 0, 0, .016, 0, 0};
    double output[12];
    int line;
    for (int sign = -1; sign <= 1; sign += 2) {
        assert(
            cupidimage_pdf_ps_type1_flex(input, reference, sign * 50, matrix, 0, 1, output, &line));
        assert(line);
        for (unsigned i = 0; i < 6; i++)
            assert(output[2 * i + 1] == -10);
    }
    matrix[0] = matrix[3] = .015625;
    assert(cupidimage_pdf_ps_type1_flex(input, reference, 15.625, matrix, 0, 1, output, &line));
    assert(!line); /* Exact equality retains and adjusts the curves. */
    assert(cupidimage_pdf_ps_type1_flex(input, reference, 15.626, matrix, 0, 1, output, &line));
    assert(line);
    matrix[0] = matrix[3] = .1;
    assert(cupidimage_pdf_ps_type1_flex(input, reference, 50, matrix, 0, 1, output, &line));
    assert(!line && !memcmp(input, output, sizeof(input)));
    matrix[5] = .25;
    for (unsigned stroke = 0; stroke < 2; stroke++) {
        assert(
            cupidimage_pdf_ps_type1_flex(input, reference, 50, matrix, stroke, 1, output, &line));
        assert(!line);
        for (unsigned i = 0; i < 6; i++) {
            assert(output[i * 2] == input[i * 2]);
            assert(fabs(output[i * 2 + 1] -
                        (i >= 1 && i <= 3 ? (stroke ? 5 : -5) : input[i * 2 + 1])) < 1e-12);
        }
    }
    matrix[0] = matrix[3] = .012;
    matrix[1] = .009;
    matrix[2] = -.009;
    assert(cupidimage_pdf_ps_type1_flex(input, reference, 50, matrix, 0, 1, output, &line));
    assert(!line && !memcmp(input, output, sizeof(input)));
    for (unsigned i = 0; i < 12; i++)
        output[i] = 123;
    matrix[0] = NAN;
    assert(!cupidimage_pdf_ps_type1_flex(input, reference, 50, matrix, 0, 1, output, &line));
    for (unsigned i = 0; i < 12; i++)
        assert(output[i] == 123);
    unsigned state = 3200099;
    for (unsigned trial = 0; trial < 8192; trial++) {
        state = state * 1664525u + 1013904223u;
        double scale = (5 + state % 96) / 1000.0;
        double transform[6] = {scale, 0, 0, scale, (trial % 64) / 64.0, ((trial / 64) % 64) / 64.0};
        if (trial & 1)
            transform[0] = -transform[0];
        if (trial & 2)
            transform[3] = -transform[3];
        if (trial & 4) {
            transform[1] = transform[0];
            transform[2] = -transform[3];
            transform[0] = transform[3] = 0;
        }
        double adjusted[12], saved[12];
        memcpy(saved, input, sizeof(saved));
        assert(cupidimage_pdf_ps_type1_flex(input, reference, trial % 101, transform, trial % 2,
                                            (trial % 3) / 2.0, adjusted, &line));
        assert(!memcmp(saved, input, sizeof(saved)));
        for (unsigned i = 0; i < 12; i++)
            assert(isfinite(adjusted[i]));
        assert(line == (adjusted[4] == adjusted[10] || adjusted[5] == adjusted[11]));
    }
    puts("Type 1 flex: literal collapse, phase, erosion, rotation and 8192 bounded cases passed");
}
