#include "fault_alloc.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define free fault_free
#include "../src/formats/pdf/raster/stroke_union.c"
#undef malloc
#undef calloc
#undef free

static unsigned checks;
static const double unit_axes[4] = {1, 0, 0, 1};

static pdf_stroke_union_shape rectangle(double x0, double y0, double x1, double y1) {
    return (pdf_stroke_union_shape){.points = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, .count = 4};
}

static unsigned coverage(const pdf_stroke_union_shape *p, unsigned n, const double axes[4]) {
    size_t work = 10000000;
    unsigned char *out = NULL;
    assert(cupidimage_pdf_stroke_union(p, n, axes, 0, 0, 1, 1, &work, &out));
    unsigned value = out[0];
    fault_free(out);
    assert(!live_count);
    checks++;
    return value;
}

static void check_geometry(void) {
    pdf_stroke_union_shape p[4];
    for (unsigned i = 0; i <= 16; i++)
        for (unsigned j = 0; j <= 16; j++) {
            p[0] = rectangle(0, 0, i / 16.0, j / 16.0);
            unsigned expected = i * j;
            if (expected > 255)
                expected = 255;
            assert(coverage(p, 1, unit_axes) == expected);
            p[1] = p[0];
            assert(coverage(p, 2, unit_axes) == expected);
        }
    p[0] = rectangle(0, 0, .75, 1);
    p[1] = rectangle(.25, 0, 1, 1);
    assert(coverage(p, 2, unit_axes) == 255);
    p[0] = (pdf_stroke_union_shape){.points = {{0, 0}, {1, 0}, {0, 1}}, .count = 3};
    assert(coverage(p, 1, unit_axes) == 128);
    p[1] = (pdf_stroke_union_shape){.points = {{1, 1}, {1, 0}, {0, 1}}, .count = 3};
    assert(coverage(p, 2, unit_axes) == 255);
    /* Crossing thin strips must not paint their shared area twice. */
    p[0] = rectangle(0, .25, 1, .75);
    p[1] = rectangle(.25, 0, .75, 1);
    assert(coverage(p, 2, unit_axes) == 192);
    /* Thick rectangle strokes can have no remaining inner region. */
    p[0] = rectangle(-1, -1, 2, .75);
    p[1] = rectangle(-1, .25, 2, 2);
    assert(coverage(p, 2, unit_axes) == 255);
    for (unsigned i = 1; i <= 16; i++) {
        double radius = i / 32.0, axes[4] = {radius, 0, 0, radius};
        p[0] = (pdf_stroke_union_shape){.points = {{.5, .5}}, .count = 1};
        p[1] = p[0];
        unsigned expected = (unsigned)(acos(-1) * radius * radius * 256);
        assert(coverage(p, 1, axes) == expected);
        assert(coverage(p, 2, axes) == expected);
    }
    double axes[4] = {.25, 0, 0, .25};
    p[0] = (pdf_stroke_union_shape){.points = {{.375, .5}}, .count = 1};
    p[1] = (pdf_stroke_union_shape){.points = {{.625, .5}}, .count = 1};
    double overlap = 2 * .0625 * acos(.5) - .125 * sqrt(.1875);
    assert(coverage(p, 2, axes) == (unsigned)((2 * acos(-1) * .0625 - overlap) * 256));
    /* Affine ellipse area is pi times the determinant, including reflections. */
    double skew[4] = {.25, .0625, .125, .25};
    assert(coverage(p, 1, skew) == (unsigned)(acos(-1) * (.0625 - .0078125) * 256));
    skew[0] = -skew[0];
    skew[1] = -skew[1];
    assert(coverage(p, 1, skew) == (unsigned)(acos(-1) * (.0625 - .0078125) * 256));
    p[0] = (pdf_stroke_union_shape){.points = {{0, 0}}, .count = 1};
    assert(coverage(p, 1, unit_axes) == 201); /* quarter circle */
    p[1] = rectangle(0, 0, 1, 1);
    assert(coverage(p, 2, unit_axes) == 255);
}

static void check_failures(void) {
    pdf_stroke_union_shape p[] = {rectangle(.125, .25, .875, .75),
                                  {.points = {{.6, .4}}, .count = 1}};
    double axes[4] = {.4, .1, .15, .3};
    for (size_t failure = 0; failure <= 5; failure++)
        for (size_t budget = 0; budget < 5000; budget += 13) {
            allocation_count = 0;
            fail_at = failure;
            size_t work = budget;
            unsigned char *out = (void *)1;
            int ok = cupidimage_pdf_stroke_union(p, 2, axes, 0, 0, 4, 4, &work, &out);
            assert(work <= budget);
            if (ok)
                fault_free(out);
            else
                assert(!out);
            assert(!live_count);
            checks++;
        }
    fail_at = 0;
    for (unsigned bad = 0; bad < 6; bad++) {
        pdf_stroke_union_shape q = p[0];
        size_t work = 100000;
        unsigned char *out = (void *)1;
        if (bad == 0)
            q.count = 2;
        if (bad == 1)
            q.points[0].x = NAN;
        if (bad == 2)
            q.points[0].y = INFINITY;
        if (bad == 3)
            q.points[0].x = 65537;
        if (bad == 4)
            q.points[2] = (pdf_stroke_union_point){.5, .3};
        if (bad == 5)
            q = (pdf_stroke_union_shape){.points = {{0, 0}}, .count = 1};
        double zero[4] = {0};
        assert(
            !cupidimage_pdf_stroke_union(&q, 1, bad == 5 ? zero : axes, 0, 0, 1, 1, &work, &out));
        assert(!out && !live_count);
        checks++;
    }
}

static void check_limits(void) {
    pdf_stroke_union_shape p = rectangle(.125, .25, .875, .75);
    for (int origin = -32768; origin <= 32760; origin += 8191) {
        pdf_stroke_union_shape translated = p;
        for (unsigned i = 0; i < 4; i++) {
            translated.points[i].x += origin;
            translated.points[i].y += origin;
        }
        size_t work = 100000;
        unsigned char *out = NULL;
        assert(cupidimage_pdf_stroke_union(&translated, 1, unit_axes, origin, origin, origin + 1,
                                           origin + 1, &work, &out));
        assert(out[0] == 96);
        fault_free(out);
        assert(!live_count);
        checks++;
    }
    pdf_stroke_union_shape dense[257];
    for (unsigned i = 0; i < 257; i++)
        dense[i] = p;
    size_t work = 1000000;
    unsigned char *out = (void *)1;
    assert(!cupidimage_pdf_stroke_union(dense, 257, unit_axes, 0, 0, 1, 1, &work, &out));
    assert(!out && !live_count);
    checks++;
    double tiny[4] = {1e-80, 0, 0, 1e-80};
    assert(!cupidimage_pdf_stroke_union(&p, 1, tiny, 0, 0, 1, 1, &work, &out));
    assert(!out && !live_count);
    checks++;
    assert(!cupidimage_pdf_stroke_union(&p, 65537, unit_axes, 0, 0, 1, 1, &work, &out));
    assert(!out && !live_count);
    checks++;
    assert(!cupidimage_pdf_stroke_union(&p, 1, unit_axes, 0, 0, 4097, 4097, &work, &out));
    assert(!out && !live_count);
    checks++;
    assert(!cupidimage_pdf_stroke_union(&p, 1, unit_axes, -32769, 0, 1, 1, &work, &out));
    assert(!out && !live_count);
    checks++;
    assert(cupidimage_pdf_stroke_union(NULL, 0, unit_axes, 0, 0, 0, 0, &work, &out));
    fault_free(out);
    assert(!live_count);
    checks++;
}

static uint32_t seed = 0x84373819;

static double random_unit(void) {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) / 16777216.0;
}

static int sampled_inside(const pdf_stroke_union_shape *p, double x, double y, const double a[4]) {
    if (p->count == 1) {
        double dx = x - p->points[0].x, dy = y - p->points[0].y, det = a[0] * a[3] - a[1] * a[2];
        double u = (a[3] * dx - a[2] * dy) / det, v = (a[0] * dy - a[1] * dx) / det;
        return u * u + v * v <= 1;
    }
    int inside = 0;
    for (unsigned i = 0, j = p->count - 1; i < p->count; j = i++) {
        double xi = p->points[i].x, yi = p->points[i].y, xj = p->points[j].x, yj = p->points[j].y;
        if ((yi > y) != (yj > y))
            inside ^= x < (xj - xi) * (y - yi) / (yj - yi) + xi;
    }
    return inside;
}

static void check_sampled_unions(void) {
    /* Independent midpoint sampling is a bounded cross-check, not the coverage
     * algorithm. Analytic cases above require exact bytes; this allows two
     * levels for finite sampling error near thin and curved boundaries. */
    for (unsigned iteration = 0; iteration < 100; iteration++) {
        double a[4] = {.1 + .6 * random_unit(), .4 * random_unit() - .2, .4 * random_unit() - .2,
                       .1 + .6 * random_unit()};
        if (fabs(a[0] * a[3] - a[1] * a[2]) < .01) {
            iteration--;
            continue;
        }
        pdf_stroke_union_shape p[5];
        for (unsigned i = 0; i < 3; i++)
            p[i] = (pdf_stroke_union_shape){.points = {{random_unit(), random_unit()}}, .count = 1};
        p[3] = rectangle(random_unit() * .4, random_unit() * .4, .6 + random_unit() * .4,
                         .6 + random_unit() * .4);
        p[4] = (pdf_stroke_union_shape){
            .points = {{-.2, random_unit()}, {1.2, random_unit()}, {random_unit(), 1.1}},
            .count = 3};
        unsigned total = 0;
        for (unsigned y = 0; y < 256; y++)
            for (unsigned x = 0; x < 256; x++) {
                for (unsigned i = 0; i < 5; i++)
                    if (sampled_inside(p + i, (x + .5) / 256, (y + .5) / 256, a)) {
                        total++;
                        break;
                    }
            }
        int expected = (int)(total / 256);
        if (expected > 255)
            expected = 255;
        unsigned got = coverage(p, 5, a);
        assert(abs((int)got - expected) <= 2);
        pdf_stroke_union_shape reversed[5];
        for (unsigned i = 0; i < 5; i++)
            reversed[i] = p[4 - i];
        assert(coverage(reversed, 5, a) == got);
    }
}

int main(void) {
    (void)fault_realloc;
    check_geometry();
    check_failures();
    check_sampled_unions();
    check_limits();
    printf("PDF analytic stroke union: %u geometry and cleanup checks passed\n", checks);
    return 0;
}
