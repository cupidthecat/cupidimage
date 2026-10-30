#include "formats/pdf/raster/coverage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

static void rectangle(void) {
    for (int x = -64; x <= 128; x++)
        for (int y = -64; y <= 128; y++) {
            pdf_coverage_point p[] = {
                {x, y, 1, 0}, {x + 64, y, 1, 0}, {x + 64, y + 64, 1, 0}, {x, y + 64, 1, 1}};
            size_t work = 100000;
            unsigned char *out = NULL;
            assert(cupidimage_pdf_coverage(p, 4, 3, 3, 0, &work, &out));
            for (int py = 0; py < 3; py++)
                for (int px = 0; px < 3; px++) {
                    int a = x > px * 64 ? x : px * 64,
                        b = x + 64 < (px + 1) * 64 ? x + 64 : (px + 1) * 64;
                    int c = y > py * 64 ? y : py * 64,
                        d = y + 64 < (py + 1) * 64 ? y + 64 : (py + 1) * 64;
                    int area = b > a && d > c ? (b - a) * (d - c) : 0;
                    /* Negative signed area uses ones-complement coverage quantization. */
                    int expected = area ? (area - 1) / 16 : 0;
                    if (expected > 255)
                        expected = 255;
                    if (out[py * 3 + px] != expected) {
                        fprintf(stderr, "rect %d,%d pixel %d,%d area %d expected %d got %u\n", x, y,
                                px, py, area, expected, out[py * 3 + px]);
                        abort();
                    }
                    checks++;
                }
            free(out);
        }
}

static void limits(void) {
    pdf_coverage_point p[] = {{0, 0, 1, 0}, {128, 0, 1, 0}, {128, 128, 1, 0}, {0, 128, 1, 1}};
    for (size_t budget = 0; budget < 200; budget++) {
        size_t work = budget;
        unsigned char *out = (void *)1;
        int ok = cupidimage_pdf_coverage(p, 4, 2, 2, 0, &work, &out);
        assert(work <= budget);
        if (ok) {
            assert(out && out[0] == 255 && out[3] == 255);
            free(out);
        } else
            assert(!out);
        checks++;
    }
    size_t work = 100000;
    unsigned char *out;
    assert(!cupidimage_pdf_coverage(p, 4, 32768, 2, 0, &work, &out) && !out);
    p[3].end = 0;
    assert(!cupidimage_pdf_coverage(p, 4, 2, 2, 0, &work, &out) && !out);
    p[3].end = 1;
    p[0].tag = 3;
    assert(!cupidimage_pdf_coverage(p, 4, 2, 2, 0, &work, &out) && !out);
    p[0].tag = 1;
    p[0].x = 2097153;
    assert(!cupidimage_pdf_coverage(p, 4, 2, 2, 0, &work, &out) && !out);
    assert(cupidimage_pdf_coverage(NULL, 0, 2, 2, 0, &work, &out));
    assert(!out[0] && !out[3]);
    free(out);
    checks += 5;
}

static void overlapping(void) {
    /* More accumulated signed area than a 32-bit cell can represent. */
    const unsigned n = 60000;
    pdf_coverage_point *p = calloc(n, sizeof(*p));
    assert(p);
    for (unsigned i = 0; i < n; i += 3) {
        p[i] = (pdf_coverage_point){-640, -640, 1, 0};
        p[i + 1] = (pdf_coverage_point){6400, -640, 1, 0};
        p[i + 2] = (pdf_coverage_point){-640, 6400, 1, 1};
    }
    for (int rule = 0; rule < 2; rule++) {
        size_t work = 100000000;
        unsigned char *out;
        assert(cupidimage_pdf_coverage(p, n, 2, 2, rule, &work, &out));
        for (unsigned i = 0; i < 4; i++)
            assert(out[i] == (rule ? 0 : 255));
        free(out);
        checks += 4;
    }
    free(p);
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--probe")) {
        unsigned n, w, h, rule;
        if (scanf("%u %u %u %u", &n, &w, &h, &rule) != 4 || n > 65535)
            return 2;
        pdf_coverage_point *p = calloc(n ? n : 1, sizeof(*p));
        if (!p)
            return 2;
        for (unsigned i = 0; i < n; i++) {
            int x, y;
            unsigned tag, end;
            if (scanf("%d %d %u %u", &x, &y, &tag, &end) != 4 || tag > 255 || end > 255) {
                free(p);
                return 2;
            }
            p[i] = (pdf_coverage_point){x, y, (unsigned char)tag, (unsigned char)end};
        }
        size_t work = 100000000;
        unsigned char *out = NULL;
        int ok = cupidimage_pdf_coverage(p, n, w, h, (int)rule, &work, &out);
        if (ok && out)
            fwrite(out, 1, (size_t)w * h, stdout);
        free(out);
        free(p);
        return ok ? 0 : 1;
    }
    rectangle();
    limits();
    overlapping();
    printf("PDF coverage: %u analytic and boundary checks passed\n", checks);
    return 0;
}
