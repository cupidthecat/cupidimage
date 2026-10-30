#include "../src/formats/pdf/color/curve_inverse.c"
#include "../src/formats/pdf/color/matrix.c"
#include "../src/formats/pdf/pdf_icc_curve.c"
#include <limits.h>
#include <stdio.h>

static unsigned checks, failures;
#define CHECK(value)                                                                               \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(value)) {                                                                            \
            failures++;                                                                            \
            fprintf(stderr, "ICC inverse failure at %u\n", __LINE__);                              \
        }                                                                                          \
    } while (0)

static int prepare(pdf_icc_curve *curve, pdf_icc_inverse_curve *inverse) {
    size_t work = 1000000;
    return cupidimage_pdf_icc_inverse_prepare(curve, inverse, &work);
}

static void expected(const pdf_icc_inverse_curve *inverse, double y, double x) {
    double actual = -1;
    CHECK(cupidimage_pdf_icc_inverse_value(inverse, y, &actual));
    CHECK(fabs(actual - x) < 1e-14);
}

int main(void) {
    pdf_icc_curve curve = {0};
    pdf_icc_inverse_curve inverse;
    CHECK(prepare(&curve, &inverse));
    for (unsigned i = 0; i <= 4096; i++)
        expected(&inverse, i / 4096.0, i / 4096.0);
    const unsigned levels[] = {0, 0, 16384, 16384, 32768, 65535, 65535};
    unsigned char samples[sizeof(levels) / sizeof(*levels) * 2];
    for (unsigned reverse = 0; reverse < 2; reverse++) {
        for (unsigned i = 0; i < 7; i++) {
            unsigned value = levels[reverse ? 6 - i : i];
            samples[i * 2] = (unsigned char)(value >> 8);
            samples[i * 2 + 1] = (unsigned char)value;
        }
        curve = (pdf_icc_curve){.samples = samples, .count = 7};
        CHECK(prepare(&curve, &inverse));
        expected(&inverse, 0, reverse ? 5 / 6.0 : 1 / 6.0);
        expected(&inverse, 1, reverse ? 1 / 6.0 : 5 / 6.0);
        expected(&inverse, 16384 / 65535.0, reverse ? 4 / 6.0 : 3 / 6.0);
        expected(&inverse, -10, reverse ? 5 / 6.0 : 1 / 6.0);
        expected(&inverse, 10, reverse ? 1 / 6.0 : 5 / 6.0);
        for (unsigned i = 0; i <= 4096; i++) {
            double y = i / 4096.0, x, value;
            CHECK(cupidimage_pdf_icc_inverse_value(&inverse, y, &x));
            CHECK(cupidimage_pdf_icc_curve_value(&curve, x, &value));
            CHECK(fabs(value - y) < 1e-14);
        }
        for (size_t quota = 0; quota < 23; quota++) {
            size_t work = quota;
            CHECK(!cupidimage_pdf_icc_inverse_prepare(&curve, &inverse, &work));
            CHECK(!inverse.valid && !work);
        }
    }
    static unsigned char full[65536 * 2];
    for (unsigned i = 0; i < 65536; i++) {
        full[i * 2] = (unsigned char)(i >> 8);
        full[i * 2 + 1] = (unsigned char)i;
    }
    curve = (pdf_icc_curve){.samples = full, .count = 65536};
    CHECK(prepare(&curve, &inverse));
    for (unsigned i = 0; i < 65536; i++)
        expected(&inverse, i / 65535.0, i / 65535.0);
    for (unsigned i = 0; i < 4; i++) {
        static const double gamma[] = {.45, 1, 2.4, 8};
        for (unsigned parametric = 0; parametric < 2; parametric++) {
            curve = (pdf_icc_curve){.count = parametric ? 0 : 1,
                                    .parametric = (int)parametric,
                                    .parameters = {gamma[i]}};
            CHECK(prepare(&curve, &inverse));
            for (unsigned j = 0; j <= 4096; j++) {
                double y = j / 4096.0, x, value;
                CHECK(cupidimage_pdf_icc_inverse_value(&inverse, y, &x));
                CHECK(cupidimage_pdf_icc_curve_value(&curve, x, &value));
                CHECK(fabs(value - y) < 1e-13);
            }
        }
    }
    curve = (pdf_icc_curve){.parametric = 1, .function = 1, .parameters = {2, 1, -.25}};
    CHECK(prepare(&curve, &inverse));
    expected(&inverse, 0, .25);
    expected(&inverse, .25, .75);
    expected(&inverse, 1, 1);
    curve.function = 2;
    curve.parameters[3] = .125;
    CHECK(prepare(&curve, &inverse));
    expected(&inverse, 0, .25);
    expected(&inverse, .125, .25);
    expected(&inverse, .375, .75);
    curve = (pdf_icc_curve){.parametric = 1, .function = 3, .parameters = {2, 2, 0, .5, .5}};
    CHECK(prepare(&curve, &inverse));
    expected(&inverse, 0, 0);
    expected(&inverse, 1, .5);
    expected(&inverse, .3, nextafter(.5, 0));
    expected(&inverse, .8, .5);
    curve = (pdf_icc_curve){.parametric = 1, .function = 4, .parameters = {1, 1, 0, -1, 2, 0, 1}};
    CHECK(prepare(&curve, &inverse));
    for (unsigned i = 0; i <= 4096; i++)
        expected(&inverse, i / 4096.0, 1 - i / 4096.0);
    curve = (pdf_icc_curve){.parametric = 1, .function = 4, .parameters = {2, 1, 0, .5, .5, .1, 0}};
    CHECK(prepare(&curve, &inverse));
    expected(&inverse, .1, .2);
    expected(&inverse, .4, sqrt(.3));
    expected(&inverse, .27, nextafter(.5, 0));
    expected(&inverse, .34, .5);
    curve = (pdf_icc_curve){.parametric = 1, .parameters = {.0001}};
    CHECK(prepare(&curve, &inverse));
    double underflow;
    CHECK(!cupidimage_pdf_icc_inverse_value(&inverse, .5, &underflow));
    expected(&inverse, 0, 0);
    expected(&inverse, 1, 1);
    for (unsigned bad = 0; bad < 9; bad++) {
        curve =
            (pdf_icc_curve){.parametric = 1, .function = 4, .parameters = {1, 1, 0, 1, .5, 0, 0}};
        if (bad == 0)
            curve.parameters[0] = 0;
        if (bad == 1)
            curve.parameters[1] = -1;
        if (bad == 2)
            curve.parameters[4] = NAN;
        if (bad == 3)
            curve.function = 5;
        if (bad == 4)
            curve.parametric = 2;
        if (bad == 5) {
            curve.parameters[3] = -1;
            curve.parameters[6] = 1;
        }
        if (bad == 6)
            curve.parameters[2] = -2;
        if (bad == 7) {
            curve.parametric = 0;
            curve.count = 2;
            curve.samples = NULL;
        }
        if (bad == 8)
            curve.count = UINT_MAX;
        CHECK(!prepare(&curve, &inverse));
        CHECK(!inverse.valid);
    }
    unsigned char constant[] = {0, 1, 0, 1}, wave[] = {0, 0, 255, 255, 0, 0};
    curve = (pdf_icc_curve){.samples = constant, .count = 2};
    CHECK(!prepare(&curve, &inverse));
    curve = (pdf_icc_curve){.samples = wave, .count = 3};
    CHECK(!prepare(&curve, &inverse));
    curve = (pdf_icc_curve){0};
    CHECK(prepare(&curve, &inverse));
    double output;
    size_t work = 100;
    CHECK(!cupidimage_pdf_icc_inverse_value(&inverse, NAN, &output));
    CHECK(!cupidimage_pdf_icc_inverse_value(&inverse, INFINITY, &output));
    CHECK(!cupidimage_pdf_icc_inverse_value(&inverse, .5, NULL));
    CHECK(!cupidimage_pdf_icc_inverse_prepare(NULL, &inverse, &work));
    CHECK(!cupidimage_pdf_icc_inverse_prepare(&curve, NULL, &work));
    CHECK(!cupidimage_pdf_icc_inverse_prepare(&curve, &inverse, NULL));
    unsigned random = 12345;
    for (unsigned trial = 0; trial < 1000; trial++) {
        double matrix[9], inv[9], point[3] = {.125, .5, .875}, mapped[3], back[3];
        for (unsigned i = 0; i < 9; i++) {
            random = random * 1664525u + 1013904223u;
            matrix[i] = (double)(random % 11) - 5 + (i % 4 == 0 ? 20 : 0);
        }
        CHECK(cupidimage_pdf_color_matrix_inverse(matrix, inv));
        CHECK(cupidimage_pdf_color_matrix_apply(matrix, point, mapped));
        CHECK(cupidimage_pdf_color_matrix_apply(inv, mapped, back));
        for (unsigned i = 0; i < 3; i++)
            CHECK(fabs(back[i] - point[i]) < 1e-13);
        for (unsigned i = 0; i < 3; i++)
            for (unsigned j = 0; j < 3; j++) {
                double value = 0;
                for (unsigned k = 0; k < 3; k++)
                    value += matrix[i * 3 + k] * inv[k * 3 + j];
                CHECK(fabs(value - (i == j)) < 1e-13);
            }
        double alias[9];
        memcpy(alias, matrix, sizeof(alias));
        CHECK(cupidimage_pdf_color_matrix_inverse(alias, alias));
        for (unsigned i = 0; i < 9; i++)
            CHECK(alias[i] == inv[i]);
    }
    for (unsigned bad = 0; bad < 4; bad++) {
        double matrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, inv[9];
        if (bad == 0)
            matrix[0] = 0;
        if (bad == 1)
            matrix[0] = 1e-13;
        if (bad == 2)
            matrix[0] = NAN;
        if (bad == 3)
            matrix[0] = 1e31;
        CHECK(!cupidimage_pdf_color_matrix_inverse(matrix, inv));
        for (unsigned i = 0; i < 9; i++)
            CHECK(inv[i] == 0);
    }
    double huge[9] = {1e30, 0, 0, 0, 1e30, 0, 0, 0, 1e30}, inv[9];
    CHECK(cupidimage_pdf_color_matrix_inverse(huge, inv));
    CHECK(fabs(inv[0] * 1e30 - 1) < 1e-15 && fabs(inv[4] * 1e30 - 1) < 1e-15 &&
          fabs(inv[8] * 1e30 - 1) < 1e-15);
    CHECK(!cupidimage_pdf_color_matrix_inverse(NULL, inv));
    CHECK(!cupidimage_pdf_color_matrix_inverse(huge, NULL));
    printf("ICC curve inverse: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
