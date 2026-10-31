#include "fault_alloc.h"
#include <assert.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define free fault_free
#include "../src/formats/pdf/raster/region.c"
#include "../src/formats/pdf/raster/region_ellipse.c"
#undef malloc
#undef calloc
#undef free

static unsigned checks;

static void rectangle(pdf_region_edge *out, unsigned region, double x0, double y0, double x1,
                      double y1, int reverse) {
    double p[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    for (unsigned i = 0; i < 4; i++) {
        unsigned a = reverse ? (4 - i) % 4 : i, b = reverse ? (3 - i) % 4 : (i + 1) % 4;
        out[i] = (pdf_region_edge){p[a][0], p[a][1], p[b][0], p[b][1], region};
    }
}

static unsigned raster(const pdf_region_edge *p, unsigned count, const unsigned char *rule,
                       unsigned regions) {
    size_t work = 1000000;
    unsigned char *out = NULL;
    assert(cupidimage_pdf_region_coverage(p, count, rule, regions, 0, 0, 1, 1, &work, &out));
    unsigned v = out[0];
    fault_free(out);
    assert(!live_count);
    checks++;
    return v;
}

typedef struct {
    double x, y;
} test_point;

static uint32_t random_state = 0x78237991u;

static double random_value(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return (random_state >> 8) / 16777216.0;
}

static double side(test_point a, test_point b, test_point p) {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

static unsigned clip_polygon(test_point *polygon, unsigned count, test_point a, test_point b) {
    test_point output[32];
    unsigned used = 0;
    for (unsigned i = 0; i < count; i++) {
        test_point p = polygon[(i + count - 1) % count], q = polygon[i];
        double sp = side(a, b, p), sq = side(a, b, q);
        if ((sp >= 0) != (sq >= 0)) {
            double t = sp / (sp - sq);
            assert(used < 32);
            output[used++] = (test_point){p.x + t * (q.x - p.x), p.y + t * (q.y - p.y)};
        }
        if (sq >= 0) {
            assert(used < 32);
            output[used++] = q;
        }
    }
    memcpy(polygon, output, used * sizeof(*polygon));
    return used;
}

static void independent_convex_cases(void) {
    unsigned char rules[2] = {0, 0};
    for (unsigned trial = 0; trial < 1000; trial++) {
        pdf_region_edge edges[8];
        test_point clipped[32] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        unsigned count = 4;
        for (unsigned group = 0; group < 2; group++) {
            double angle = random_value() * 6.283185307179586, cx = random_value(),
                   cy = random_value();
            double u = .05 + .6 * random_value(), v = .05 + .6 * random_value(), co = cos(angle),
                   si = sin(angle);
            test_point p[4];
            double sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
            for (unsigned i = 0; i < 4; i++)
                p[i] = (test_point){cx + co * sx[i] * u - si * sy[i] * v,
                                    cy + si * sx[i] * u + co * sy[i] * v};
            for (unsigned i = 0; i < 4; i++) {
                test_point a = p[i], b = p[(i + 1) % 4];
                edges[group * 4 + i] = (pdf_region_edge){a.x, a.y, b.x, b.y, group};
                count = clip_polygon(clipped, count, a, b);
            }
        }
        double area = 0;
        for (unsigned i = 0; i < count; i++)
            area += clipped[i].x * clipped[(i + 1) % count].y -
                    clipped[i].y * clipped[(i + 1) % count].x;
        unsigned expected = (unsigned)fmin(255, floor(fabs(area) * 128));
        unsigned got = raster(edges, 8, rules, 2);
        assert(abs((int)got - (int)expected) <= 1);
        /* Reversing each contour and exchanging intersection operands must not
         * change area or its quantization. */
        for (unsigned i = 0; i < 8; i++) {
            double x = edges[i].x0, y = edges[i].y0;
            edges[i].x0 = edges[i].x1;
            edges[i].y0 = edges[i].y1;
            edges[i].x1 = x;
            edges[i].y1 = y;
            edges[i].region = 1 - edges[i].region;
        }
        assert(raster(edges, 8, rules, 2) == got);
    }
}

static unsigned ellipse_raster(const pdf_region_edge *edges, unsigned count,
                               const pdf_region_ellipse *ellipses, unsigned n, const double axes[4],
                               const unsigned char *rules, unsigned regions) {
    size_t work = 1000000;
    unsigned char *out = NULL;
    assert(cupidimage_pdf_region_coverage_ellipses(edges, count, ellipses, n, axes, rules, regions,
                                                   0, 0, 1, 1, &work, &out));
    unsigned v = out[0];
    fault_free(out);
    assert(!live_count);
    checks++;
    return v;
}

static void ellipse_cases(void) {
    double axes[4] = {.5, 0, 0, .5};
    unsigned char rules[3] = {0};
    pdf_region_ellipse circles[2] = {{.5, .5, 0, 0}, {.5, .5, 1, 0}};
    assert(ellipse_raster(NULL, 0, circles, 1, axes, rules, 1) == 201);
    assert(ellipse_raster(NULL, 0, circles, 2, axes, rules, 2) == 201);
    pdf_region_edge edges[4];
    rectangle(edges, 1, .5, 0, 1, 1, 0);
    assert(ellipse_raster(edges, 4, circles, 1, axes, rules, 2) == 100);
    edges[0] = (pdf_region_edge){0, 0, 1, 0, 1};
    edges[1] = (pdf_region_edge){1, 0, 1, 1, 1};
    edges[2] = (pdf_region_edge){1, 1, 0, 0, 1};
    assert(ellipse_raster(edges, 3, circles, 1, axes, rules, 2) == 100);
    axes[0] = axes[3] = .25;
    circles[0].x = .375;
    circles[1].x = .625;
    double overlap = 2 * .0625 * acos(.5) - .125 * sqrt(.1875);
    assert(ellipse_raster(NULL, 0, circles, 2, axes, rules, 2) == (unsigned)(overlap * 256));
    circles[1].region = 0;
    assert(ellipse_raster(NULL, 0, circles, 2, axes, rules, 1) ==
           (unsigned)((2 * acos(-1) * .0625 - overlap) * 256));
    rectangle(edges, 0, 0, 0, 1, 1, 0);
    circles[0].x = .5;
    circles[0].reverse = 1;
    assert(ellipse_raster(edges, 4, circles, 1, axes, rules, 1) ==
           (unsigned)((1 - acos(-1) * .0625) * 256));
    circles[0].reverse = 0;
    assert(ellipse_raster(edges, 4, circles, 1, axes, rules, 1) == 255);
    rules[0] = 1;
    assert(ellipse_raster(edges, 4, circles, 1, axes, rules, 1) ==
           (unsigned)((1 - acos(-1) * .0625) * 256));
    rules[0] = 0;
    axes[1] = .0625;
    axes[2] = .125;
    rectangle(edges, 1, .5, 0, 1, 1, 0);
    assert(ellipse_raster(edges, 4, circles, 1, axes, rules, 2) ==
           (unsigned)(acos(-1) * (.0625 - .0078125) * 128));
    for (size_t fail = 0; fail <= 6; fail++)
        for (size_t budget = 0; budget < 3000; budget += 17) {
            allocation_count = 0;
            fail_at = fail;
            size_t work = budget;
            unsigned char *out = (void *)1;
            int ok = cupidimage_pdf_region_coverage_ellipses(edges, 4, circles, 1, axes, rules, 2,
                                                             0, 0, 1, 1, &work, &out);
            if (ok)
                fault_free(out);
            else
                assert(!out);
            assert(!live_count && work <= budget);
            checks++;
        }
    fail_at = 0;
}

static void sampled_ellipse_intersections(void) {
    unsigned char rule[2] = {0};
    for (unsigned trial = 0; trial < 50; trial++) {
        double a[4] = {.1 + .5 * random_value(), .2 * random_value() - .1, .2 * random_value() - .1,
                       .1 + .5 * random_value()};
        double det = a[0] * a[3] - a[1] * a[2];
        if (fabs(det) < .01) {
            trial--;
            continue;
        }
        pdf_region_ellipse e[2] = {{random_value(), random_value(), 0, 0},
                                   {random_value(), random_value(), 0, 0}};
        double x0 = random_value() * .4, y0 = random_value() * .4, x1 = .6 + random_value() * .4,
               y1 = .6 + random_value() * .4;
        pdf_region_edge edges[4];
        rectangle(edges, 1, x0, y0, x1, y1, 0);
        unsigned hits = 0;
        for (unsigned y = 0; y < 256; y++)
            for (unsigned x = 0; x < 256; x++) {
                double px = (x + .5) / 256, py = (y + .5) / 256;
                if (px < x0 || px > x1 || py < y0 || py > y1)
                    continue;
                for (unsigned j = 0; j < 2; j++) {
                    double dx = px - e[j].x, dy = py - e[j].y;
                    double u = (a[3] * dx - a[2] * dy) / det, v = (-a[1] * dx + a[0] * dy) / det;
                    if (u * u + v * v <= 1) {
                        hits++;
                        break;
                    }
                }
            }
        unsigned expected = hits / 256;
        if (expected > 255)
            expected = 255;
        unsigned got = ellipse_raster(edges, 4, e, 2, a, rule, 2);
        assert(abs((int)got - (int)expected) <= 2);
    }
}

static void invalid_regions(void) {
    pdf_region_edge edges[4];
    rectangle(edges, 0, 0, 0, 1, 1, 0);
    unsigned char rules[2] = {0};
    double axes[4] = {.5, 0, 0, .5};
    for (unsigned which = 0; which < 14; which++) {
        pdf_region_edge changed[4];
        memcpy(changed, edges, sizeof(changed));
        pdf_region_ellipse ellipse = {.5, .5, 0, 0};
        double pen[4];
        memcpy(pen, axes, sizeof(pen));
        unsigned count = 4, ec = 0, regions = 1;
        int left = 0, right = 1, bottom = 1;
        if (which == 0)
            count = 65537;
        if (which == 1)
            regions = 65;
        if (which == 2)
            changed[0].x0 = NAN;
        if (which == 3)
            changed[0].y1 = INFINITY;
        if (which == 4)
            changed[0].region = 1;
        if (which == 5)
            changed[0].x1 = 32769;
        if (which == 6)
            left = 2;
        if (which == 7)
            right = bottom = 4097;
        if (which == 8) {
            ec = 1;
            pen[0] = 0;
        }
        if (which == 9) {
            ec = 1;
            ellipse.reverse = 2;
        }
        if (which == 10) {
            ec = 1;
            ellipse.region = 1;
        }
        if (which == 11) {
            ec = 1;
            ellipse.x = NAN;
        }
        if (which == 12) {
            ec = 1;
            pen[0] = pen[3] = 1e-80;
        }
        if (which == 13)
            ec = 65537;
        size_t work = 1000000;
        unsigned char *out = (void *)1;
        assert(!cupidimage_pdf_region_coverage_ellipses(changed, count, &ellipse, ec, pen, rules,
                                                        regions, left, 0, right, bottom, &work,
                                                        &out));
        assert(!out && !live_count && work <= 1000000);
        checks++;
    }
}

static void localized_events(void) {
    unsigned char rule[1] = {0};
    pdf_region_edge p[8];
    rectangle(p, 0, -10, .25, 10, .75, 0);
    assert(raster(p, 4, rule, 1) == 128); /* horizontal boundaries span the pixel */
    rectangle(p, 0, 0, 0, 1, 1, 0);
    rectangle(p + 4, 0, 0, 0, 1, 1, 1);
    assert(raster(p, 8, rule, 1) == 0);
    p[0] = (pdf_region_edge){0, 0, 1, 1, 0};
    p[1] = (pdf_region_edge){1, 1, 0, 1, 0};
    p[2] = (pdf_region_edge){0, 1, 1, 0, 0};
    p[3] = (pdf_region_edge){1, 0, 0, 0, 0};
    assert(raster(p, 4, rule, 1) == 128); /* self-intersection changes edge ordering */
    pdf_region_edge split[5] = {{-10, 0, 10, 0, 0},
                                {10, 0, 10, 1, 0},
                                {10, 1, -10, 1, 0},
                                {-10, 1, -10, .5, 0},
                                {-10, .5, -10, 0, 0}};
    assert(raster(split, 5, rule, 1) == 255); /* midpoint on a vertex outside the pixel */
    for (unsigned i = 0; i < 5; i++) {
        double x = split[i].x0, y = split[i].y0;
        split[i].x0 = split[i].x1;
        split[i].y0 = split[i].y1;
        split[i].x1 = x;
        split[i].y1 = y;
    }
    assert(raster(split, 5, rule, 1) == 255);
    for (unsigned i = 0; i < 5; i++) {
        if (split[i].x0 == -10)
            split[i].x0 = 0;
        if (split[i].x1 == -10)
            split[i].x1 = 0;
    }
    assert(raster(split, 5, rule, 1) == 255); /* vertex on the left pixel boundary */
    for (unsigned i = 0; i < 5; i++)
        split[i].region = 1;
    unsigned char two_rules[2] = {0};
    double axes[4] = {.25, 0, 0, .25};
    pdf_region_ellipse circle = {.5, .5, 0, 0};
    assert(ellipse_raster(split, 5, &circle, 1, axes, two_rules, 2) == 50);
    p[0] = (pdf_region_edge){0, 0, .484375, 0, 0};
    p[1] = (pdf_region_edge){.484375, 0, .5, 1, 0};
    p[2] = (pdf_region_edge){.5, 1, 0, 1, 0};
    p[3] = (pdf_region_edge){0, 1, 0, 0, 0};
    rectangle(p + 4, 0, 10, .3, 11, .8, 0);
    /* The distant contour must not disturb the exact 63/128 area here. */
    assert(raster(p, 8, rule, 1) == 126);
    assert(raster(p, 4, two_rules, 2) == 0); /* an empty intersection operand */
    pdf_region_edge many[800];
    for (unsigned i = 0; i < 200; i++)
        rectangle(many + i * 4, 0, i + .25, .25, i + .5, .75, 0);
    size_t work = 1000000;
    unsigned char *out = NULL;
    assert(cupidimage_pdf_region_coverage(many, 800, rule, 1, 0, 0, 200, 1, &work, &out));
    for (unsigned i = 0; i < 200; i++)
        assert(out[i] == 32);
    fault_free(out);
    assert(!live_count);
    checks++;
}

static void coincident_edges(void) {
    pdf_region_edge *edges = malloc(65536 * sizeof(*edges));
    assert(edges);
    const unsigned copies[] = {256, 257, 1000, 1001, 16384};
    unsigned char rules[2] = {0, 0};
    for (unsigned c = 0; c < sizeof(copies) / sizeof(*copies); c++) {
        unsigned count = copies[c];
        for (unsigned i = 0; i < count; i++)
            rectangle(edges + 4 * i, 0, .125, .125, .875, .875, 0);
        rules[0] = 0;
        assert(raster(edges, count * 4, rules, 1) == 144);
        rules[0] = 1;
        assert(raster(edges, count * 4, rules, 1) == (count % 2 ? 144 : 0));
        for (unsigned i = 0; i < count; i++)
            rectangle(edges + 4 * i, 0, .125, .125, .875, .875, i % 2);
        rules[0] = 0;
        assert(raster(edges, count * 4, rules, 1) == (count % 2 ? 144 : 0));
    }
    for (unsigned i = 0; i < 516; i++)
        rectangle(edges + i * 4, i < 257 ? 0 : 1, .125, .125, .875, .875, i >= 257);
    rules[0] = rules[1] = 1;
    assert(raster(edges, 2064, rules, 2) == 144);
    rules[0] = rules[1] = 0;
    allocation_count = 0;
    assert(raster(edges, 2064, rules, 2) == 144);
    size_t allocations = allocation_count;
    for (size_t fail = 0; fail <= allocations; fail++) {
        fail_at = fail;
        allocation_count = 0;
        size_t work = 1000000;
        unsigned char *out = (void *)1;
        int ok = cupidimage_pdf_region_coverage(edges, 2064, rules, 2, 0, 0, 1, 1, &work, &out);
        assert(ok == !fail);
        if (ok)
            fault_free(out);
        else
            assert(!out);
        assert(!live_count);
        checks++;
    }
    fail_at = 0;
    for (size_t budget = 0; budget < 20000; budget += 37) {
        size_t work = budget;
        unsigned char *out = (void *)1;
        int ok = cupidimage_pdf_region_coverage(edges, 2064, rules, 2, 0, 0, 1, 1, &work, &out);
        assert(work <= budget);
        if (ok)
            fault_free(out);
        else
            assert(!out);
        assert(!live_count);
        checks++;
    }
    free(edges);
}

int main(void) {
    (void)fault_realloc;
    pdf_region_edge p[16];
    unsigned char rule[4] = {0};
    assert(raster(NULL, 0, NULL, 0) == 255);
    assert(raster(NULL, 0, rule, 1) == 0);
    for (unsigned x = 0; x <= 16; x++)
        for (unsigned y = 0; y <= 16; y++) {
            rectangle(p, 0, 0, 0, x / 16.0, y / 16.0, 0);
            unsigned expected = x * y;
            if (expected > 255)
                expected = 255;
            assert(raster(p, 4, rule, 1) == expected);
            rectangle(p + 4, 1, 0, 0, x / 16.0, y / 16.0, 1);
            assert(raster(p, 8, rule, 2) == expected); /* idempotent geometric intersection */
        }
    rectangle(p, 0, 0, 0, .375, 1, 0);
    rectangle(p + 4, 1, .625, 0, 1, 1, 0);
    assert(raster(p, 8, rule, 2) == 0); /* both cover a fraction of the same pixel */
    rectangle(p, 0, 0, 0, .75, 1, 0);
    rectangle(p + 4, 1, .25, 0, 1, 1, 0);
    assert(raster(p, 8, rule, 2) == 128);
    rectangle(p, 0, 0, 0, 1, 1, 0);
    rectangle(p + 4, 0, .25, .25, .75, .75, 1);
    assert(raster(p, 8, rule, 1) == 192);
    rectangle(p + 4, 0, .25, .25, .75, .75, 0);
    assert(raster(p, 8, rule, 1) == 255);
    rule[0] = 1;
    assert(raster(p, 8, rule, 1) == 192);
    rule[0] = 0;
    p[0] = (pdf_region_edge){0, 0, 1, 1, 0};
    p[1] = (pdf_region_edge){1, 1, 0, 1, 0};
    p[2] = (pdf_region_edge){0, 1, 0, 0, 0};
    p[3] = (pdf_region_edge){1, 0, 0, 1, 1};
    p[4] = (pdf_region_edge){0, 1, 1, 1, 1};
    p[5] = (pdf_region_edge){1, 1, 1, 0, 1};
    assert(raster(p, 6, rule, 2) == 64);
    for (size_t fail = 0; fail <= 6; fail++)
        for (size_t budget = 0; budget < 3000; budget += 7) {
            fail_at = fail;
            allocation_count = 0;
            size_t work = budget;
            unsigned char *out = (void *)1;
            int ok = cupidimage_pdf_region_coverage(p, 6, rule, 2, 0, 0, 2, 2, &work, &out);
            assert(work <= budget);
            if (ok)
                fault_free(out);
            else
                assert(!out);
            assert(!live_count);
            checks++;
        }
    fail_at = 0;
    independent_convex_cases();
    ellipse_cases();
    sampled_ellipse_intersections();
    invalid_regions();
    localized_events();
    coincident_edges();
    printf("PDF region intersection: %u analytic and ownership checks passed\n", checks);
    return 0;
}
