#include "formats/pdf/clipping/scanline.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t random_state = 42;

static float coordinate(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return (int)(random_state >> 24) / 8.0f - 8;
}

static int oracle(const pdf_region_edge *e, float x, float y) {
    float y0 = e->y0, y1 = e->y1, x0 = e->x0, x1 = e->x1;
    if (!((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) || fabsf(y0 - y1) <= 1e-6f)
        return 0;
    float t = (y - y0) / (y1 - y0);
    return x0 + t * (x1 - x0) > x ? (y1 > y0 ? 1 : -1) : 0;
}

int main(void) {
    pdf_region_edge edges[192];
    pdf_scan_crossing crossings[192];
    unsigned checks = 0;
    for (unsigned trial = 0; trial < 256; trial++) {
        unsigned count = trial % 193;
        for (unsigned i = 0; i < count; i++) {
            edges[i] = (pdf_region_edge){coordinate(), coordinate(), coordinate(), coordinate(), 0};
            if (i % 7 == 0)
                edges[i].y1 = edges[i].y0;
            if (i % 11 == 0)
                edges[i].x1 = edges[i].x0;
            if (i % 13 == 0 && i)
                edges[i] = edges[i - 1];
        }
        for (int row = -8; row < 24; row++) {
            float y = row + .5f;
            size_t work = 100000;
            int n = cupidimage_pdf_scanline(edges, count, y, crossings, &work);
            assert(n >= 0 && (unsigned)n <= count && work <= 100000 - count);
            for (int i = 1; i < n; i++)
                assert(crossings[i - 1].x <= crossings[i].x);
            for (int col = -8; col < 24; col++) {
                float x = col + .5f;
                int expected = 0, got = 0;
                for (unsigned i = 0; i < count; i++)
                    expected += oracle(edges + i, x, y);
                for (int i = 0; i < n; i++)
                    if (crossings[i].x > x)
                        got += crossings[i].direction;
                assert(got == expected);
                checks++;
            }
        }
    }
    for (unsigned i = 0; i < 192; i++)
        edges[i] = (pdf_region_edge){(192 - i) / 8.0, 0, (192 - i) / 8.0, 2, 0};
    size_t full = 100000;
    assert(cupidimage_pdf_scanline(edges, 192, .5f, crossings, &full) == 192);
    size_t required = 100000 - full;
    for (size_t limit = 0; limit <= required; limit++) {
        size_t work = limit;
        assert((cupidimage_pdf_scanline(edges, 192, .5f, crossings, &work) == 192) ==
               (limit == required));
        assert(work <= limit);
        checks++;
    }
    size_t work = 1000;
    edges[0].x0 = NAN;
    assert(cupidimage_pdf_scanline(edges, 1, .5f, crossings, &work) == -1);
    assert(cupidimage_pdf_scanline(NULL, 1, .5f, crossings, &work) == -1);
    assert(cupidimage_pdf_scanline(edges, 262145, .5f, crossings, &work) == -1);
    printf("PDF binary scanlines: %u winding and budget checks passed\n", checks + 3);
    return 0;
}
