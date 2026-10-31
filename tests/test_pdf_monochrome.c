#include "formats/pdf/raster/monochrome.h"
#include "formats/pdf/raster/monochrome_engine.h"
#define FT_BEGIN_HEADER
#define FT_END_HEADER
#define FT_STATIC_BYTE_CAST(type, value) ((type)(unsigned char)(value))
#include "third_party/freetype-raster/ftimage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void boundaries(void) {
    pdf_coverage_point points[] = {{0, 0, 1, 0}, {128, 0, 1, 0}, {128, 128, 1, 0}, {0, 128, 1, 1}};
    for (unsigned mode = 0; mode < 8; mode++) {
        for (int precision = 0; precision <= 1; precision++) {
            size_t work = 100000;
            unsigned char *pixels = NULL;
            assert(cupidimage_pdf_monochrome(points, 4, 3, 3, mode, precision, &work, &pixels));
            const unsigned char expected[] = {255, 255, 0, 255, 255, 0, 0, 0, 0};
            assert(!memcmp(pixels, expected, sizeof(expected)));
            free(pixels);
        }
    }
    for (size_t budget = 0; budget < 300; budget++) {
        size_t work = budget;
        unsigned char *pixels = (void *)1;
        int ok = cupidimage_pdf_monochrome(points, 4, 3, 3, 0, 0, &work, &pixels);
        assert(work <= budget);
        if (ok) {
            assert(pixels[0] == 255 && pixels[8] == 0);
            free(pixels);
        } else
            assert(!pixels);
    }
    size_t work = 100000;
    unsigned char *pixels;
    assert(!cupidimage_pdf_monochrome(points, 4, 32768, 1, 0, 0, &work, &pixels));
    assert(!cupidimage_pdf_monochrome(points, 4, 3, 3, 8, 0, &work, &pixels));
    assert(!cupidimage_pdf_monochrome(points, 4, 3, 3, 0, 2, &work, &pixels));
    points[3].end = 0;
    assert(!cupidimage_pdf_monochrome(points, 4, 3, 3, 0, 0, &work, &pixels));
    points[3].end = 1;
    points[1].tag = 5;
    assert(!cupidimage_pdf_monochrome(points, 4, 3, 3, 0, 0, &work, &pixels));
    points[1].tag = 1;
    points[0].x = -2097153;
    assert(!cupidimage_pdf_monochrome(points, 4, 3, 3, 0, 0, &work, &pixels));
    assert(cupidimage_pdf_monochrome(NULL, 0, 3, 3, 0, 0, &work, &pixels));
    for (unsigned i = 0; i < 9; i++)
        assert(!pixels[i]);
    free(pixels);
}

static void dense_and_extreme(void) {
    pdf_coverage_point extreme[] = {{-2097152, -2097152, 1, 0},
                                    {2097152, -2097152, 1, 0},
                                    {2097152, 2097152, 1, 0},
                                    {-2097152, 2097152, 1, 1}};
    for (int high = 0; high <= 1; high++) {
        size_t work = 100000;
        unsigned char *pixels;
        assert(cupidimage_pdf_monochrome(extreme, 4, 16, 16, 2, high, &work, &pixels));
        for (unsigned i = 0; i < 256; i++)
            assert(pixels[i] == 255);
        free(pixels);
    }
    unsigned count = 60000;
    pdf_coverage_point *points = calloc(count, sizeof(*points));
    assert(points);
    for (unsigned i = 0; i < count; i += 3) {
        points[i] = (pdf_coverage_point){-64, -64, 1, 0};
        points[i + 1] = (pdf_coverage_point){2048, -64, 1, 0};
        points[i + 2] = (pdf_coverage_point){-64, 2048, 1, 1};
    }
    size_t work = count + 256 + 1000;
    unsigned char *pixels = NULL;
    assert(!cupidimage_pdf_monochrome(points, count, 16, 16, 0, 1, &work, &pixels));
    assert(!pixels && !work);
    free(points);
}

static void diagonals_and_pool(void) {
    pdf_coverage_point triangle[] = {{0, 0, 1, 0}, {264, 0, 1, 0}, {0, 264, 1, 1}};
    size_t work = 100000;
    unsigned char *pixels = NULL;
    assert(cupidimage_pdf_monochrome(triangle, 3, 6, 6, 2, 1, &work, &pixels));
    for (unsigned y = 0; y < 6; y++)
        for (unsigned x = 0; x < 6; x++)
            assert(pixels[y * 6 + x] == (x + y <= 3 ? 255 : 0));
    free(pixels);

    /* Deliberately undersized pools exercise sub-banding and capacity checks.
     * The adapter accepts only already-validated nonempty dimensions. */
    FT_Vector points[] = {{0, 0}, {32, 32}, {992, 32}, {992, 992}, {32, 992}};
    unsigned char tags[] = {0, 1, 1, 1, 1};
    unsigned short ends[] = {3};
    FT_Outline outline = {1, 4, points + 1, tags + 1, ends, FT_OUTLINE_IGNORE_DROPOUTS};
    unsigned successes = 0, failures = 0;
    for (size_t words = 1; words <= 256; words++) {
        long *pool = calloc(words, sizeof(*pool));
        unsigned char bitmap[32] = {0};
        assert(pool);
        work = 1000000;
        int ok = cupidimage_pdf_monochrome_engine(&outline, bitmap, 16, 16, pool, words, &work);
        assert(work < 1000000);
        if (ok)
            successes++;
        else
            failures++;
        free(pool);
    }
    assert(successes && failures);
}

static unsigned random_word(unsigned *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void malformed_curves(void) {
    unsigned random = 3200093;
    for (unsigned trial = 0; trial < 4096; trial++) {
        pdf_coverage_point points[16];
        unsigned count = 1 + random_word(&random) % 16;
        for (unsigned i = 0; i < count; i++) {
            points[i].x = (int32_t)(random_word(&random) % 8193) - 4096;
            points[i].y = (int32_t)(random_word(&random) % 8193) - 4096;
            points[i].tag = (unsigned char)(random_word(&random) % 3);
            points[i].end = i + 1 == count || !(random_word(&random) % 5);
        }
        size_t work = 20000;
        unsigned char *pixels = NULL;
        int ok = cupidimage_pdf_monochrome(points, count, 16, 16, trial % 8, (trial / 8) % 2, &work,
                                           &pixels);
        assert(work <= 20000);
        if (ok) {
            assert(pixels);
            for (unsigned i = 0; i < 256; i++)
                assert(pixels[i] == 0 || pixels[i] == 255);
            free(pixels);
        } else
            assert(!pixels);
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && (!strcmp(argv[1], "--probe") || !strcmp(argv[1], "--batch"))) {
        int batch = !strcmp(argv[1], "--batch");
        do {
            unsigned count, width, height, mode, precision;
            int fields = scanf("%u %u %u %u %u", &count, &width, &height, &mode, &precision);
            if (batch && fields == EOF)
                return 0;
            if (fields != 5 || count > 65535)
                return 2;
            pdf_coverage_point *points = calloc(count ? count : 1, sizeof(*points));
            if (!points)
                return 2;
            for (unsigned i = 0; i < count; i++)
                if (scanf("%d %d %hhu %hhu", &points[i].x, &points[i].y, &points[i].tag,
                          &points[i].end) != 4)
                    return 2;
            size_t work = 20000000;
            unsigned char *pixels = NULL;
            int ok = cupidimage_pdf_monochrome(points, count, width, height, mode, precision, &work,
                                               &pixels);
            free(points);
            if (ok && width && height)
                ok = fwrite(pixels, 1, (size_t)width * height, stdout) == (size_t)width * height;
            free(pixels);
            if (!ok)
                return 3;
        } while (batch);
        return 0;
    }
    boundaries();
    dense_and_extreme();
    diagonals_and_pool();
    malformed_curves();
    puts("Monochrome raster: analytic rectangles, limits and work cancellation passed");
    return 0;
}
