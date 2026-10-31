#include "../src/formats/pdf/images/sample.c"
#include <limits.h>
#include <stdio.h>

static unsigned checks, failures;
#define CHECK(value)                                                                               \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(value)) {                                                                            \
            failures++;                                                                            \
            fprintf(stderr, "image tap failure: %u\n", __LINE__);                                  \
        }                                                                                          \
    } while (0)

int main(void) {
    pdf_image_tap taps[4];
    unsigned count;
    CHECK(cupidimage_pdf_image_taps(2, 2, .5, .5, 1, taps, &count) && count == 4);
    for (unsigned i = 0; i < count; i++)
        CHECK(taps[i].weight == .25 && taps[i].x == i % 2 && taps[i].y == i / 2);
    CHECK(cupidimage_pdf_image_taps(2, 2, 0, 1, 1, taps, &count) && count == 1 && taps[0].x == 0 &&
          taps[0].y == 0 && taps[0].weight == 1);
    CHECK(cupidimage_pdf_image_taps(2, 2, .75, .25, 1, taps, &count) && count == 1 &&
          taps[0].x == 1 && taps[0].y == 1);
    CHECK(cupidimage_pdf_image_taps(2, 2, .5, .5, 0, taps, &count) && count == 1 &&
          taps[0].x == 1 && taps[0].y == 1);
    for (unsigned w = 1; w <= 17; w++)
        for (unsigned h = 1; h <= 17; h++)
            for (unsigned i = 0; i < 33; i++)
                for (unsigned j = 0; j < 33; j++) {
                    double u = (i + .5) / 33, v = (j + .5) / 33, total = 0;
                    CHECK(cupidimage_pdf_image_taps(w, h, u, v, 1, taps, &count) && count >= 1 &&
                          count <= 4);
                    for (unsigned k = 0; k < count; k++) {
                        CHECK(taps[k].x < w && taps[k].y < h && taps[k].weight > 0 &&
                              taps[k].weight <= 1);
                        total += taps[k].weight;
                    }
                    CHECK(fabs(total - 1) < 1e-15);
                }
    for (unsigned i = 0; i < 2; i++) {
        CHECK(cupidimage_pdf_image_taps(2, 2, -1, 1, i, taps, &count) && !count);
        CHECK(cupidimage_pdf_image_taps(2, 2, 1, 1, i, taps, &count) && !count);
        CHECK(cupidimage_pdf_image_taps(2, 2, 0, 0, i, taps, &count) && !count);
        CHECK(cupidimage_pdf_image_taps(1, 1, .1, .9, i, taps, &count) && count == 1 &&
              taps[0].weight == 1);
        CHECK(cupidimage_pdf_image_taps(UINT_MAX, UINT_MAX, .5, .5, i, taps, &count));
    }
    CHECK(!cupidimage_pdf_image_taps(0, 1, 0, 1, 1, taps, &count) && !count);
    CHECK(!cupidimage_pdf_image_taps(1, 0, 0, 1, 1, taps, &count) && !count);
    CHECK(!cupidimage_pdf_image_taps(1, 1, NAN, 1, 1, taps, &count) && !count);
    CHECK(!cupidimage_pdf_image_taps(1, 1, 0, INFINITY, 1, taps, &count) && !count);
    CHECK(!cupidimage_pdf_image_taps(1, 1, 0, 1, 2, taps, &count) && !count);
    CHECK(!cupidimage_pdf_image_taps(1, 1, 0, 1, 1, NULL, &count) && !count);
    CHECK(!cupidimage_pdf_image_taps(1, 1, 0, 1, 1, taps, NULL));
    printf("PDF image taps: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
