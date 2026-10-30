#include "region.h"
#include "region_ellipse.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define REGION_EVENTS 65536
#define REGION_CROSSINGS 1024

typedef long double region_real;

typedef struct {
    region_real x, y, dx, dy, low, high;
    unsigned region;
    int direction;
} region_edge;

typedef struct {
    region_real x, integral;
    unsigned region;
    int direction;
} region_crossing;

typedef struct {
    region_real *events;
    const region_edge **candidates;
    unsigned event_count;
    size_t *work;
    region_real x, y;
    pdf_region_ellipse_frame ellipse_frame;
} region_sweep;

static int region_charge(region_sweep *s, size_t work) {
    if (work > *s->work)
        return 0;
    *s->work -= work;
    return 1;
}

static int region_event(region_sweep *s, region_real y) {
    if (y <= s->y || y >= s->y + 1)
        return 1;
    if (!isfinite(y) || s->event_count == REGION_EVENTS || !region_charge(s, 1))
        return 0;
    s->events[s->event_count++] = y;
    return 1;
}

static int region_real_compare(const void *a, const void *b) {
    region_real x = *(const region_real *)a, y = *(const region_real *)b;
    return (x > y) - (x < y);
}

static int region_crossing_compare(const void *a, const void *b) {
    region_real x = ((const region_crossing *)a)->x, y = ((const region_crossing *)b)->x;
    return (x > y) - (x < y);
}

static region_real region_x(const region_edge *edge, region_real y) {
    return edge->x + (y - edge->y) * edge->dx / edge->dy;
}

static int region_inside(const int32_t winding[64], const unsigned char *even_odd, unsigned count) {
    for (unsigned i = 0; i < count; i++)
        if (even_odd[i] ? winding[i] % 2 == 0 : winding[i] == 0)
            return 0;
    return 1;
}

static int region_pair(region_sweep *s, const region_edge *a, const region_edge *b) {
    region_real det = a->dx * b->dy - a->dy * b->dx;
    if (!det)
        return 1;
    region_real x = b->x - a->x, y = b->y - a->y;
    region_real t = (x * b->dy - y * b->dx) / det, u = (x * a->dy - y * a->dx) / det;
    if (t < 0 || t > 1 || u < 0 || u > 1)
        return 1;
    region_real ix = a->x + t * a->dx;
    if (ix < s->x || ix > s->x + 1)
        return 1;
    return region_event(s, a->y + t * a->dy);
}

static int region_pixel(region_sweep *s, const region_edge *active, unsigned count,
                        const pdf_region_ellipse *ellipses, unsigned ellipse_count,
                        const unsigned char *even_odd, unsigned regions, region_real *area) {
    pdf_region_ellipse selected[REGION_CROSSINGS];
    unsigned selected_count = 0;
    if (!region_charge(s, ellipse_count))
        return 0;
    for (unsigned i = 0; i < ellipse_count; i++) {
        const pdf_region_ellipse *e = ellipses + i;
        if (e->x + s->ellipse_frame.extent_x <= s->x ||
            e->x - s->ellipse_frame.extent_x >= s->x + 1)
            continue;
        if (selected_count == REGION_CROSSINGS)
            return 0;
        selected[selected_count++] = *e;
    }
    ellipses = selected;
    ellipse_count = selected_count;
    region_crossing crossings[REGION_CROSSINGS];
    unsigned candidates = 0;
    s->event_count = 0;
    s->events[s->event_count++] = s->y;
    s->events[s->event_count++] = s->y + 1;
    if (!region_charge(s, count))
        return 0;
    for (unsigned i = 0; i < count; i++) {
        const region_edge *e = active + i;
        if (!region_event(s, e->low) || !region_event(s, e->high))
            return 0;
        region_real lo = fmaxl(e->low, s->y), hi = fminl(e->high, s->y + 1);
        region_real a = region_x(e, lo), b = region_x(e, hi);
        if (fmaxl(a, b) <= s->x || fminl(a, b) >= s->x + 1)
            continue;
        if (candidates == REGION_CROSSINGS)
            return 0;
        if (e->dx)
            for (unsigned side = 0; side < 2; side++) {
                region_real t = (s->x + side - e->x) / e->dx;
                if (t >= 0 && t <= 1 && !region_event(s, e->y + t * e->dy))
                    return 0;
            }
        if (!region_charge(s, candidates))
            return 0;
        for (unsigned j = 0; j < candidates; j++)
            if (!region_pair(s, e, s->candidates[j]))
                return 0;
        s->candidates[candidates++] = e;
    }
    for (unsigned i = 0; i < ellipse_count; i++) {
        const pdf_region_ellipse *e = ellipses + i;
        if (!region_charge(s, 2) || !region_event(s, e->y - s->ellipse_frame.height) ||
            !region_event(s, e->y + s->ellipse_frame.height))
            return 0;
        region_real ys[2];
        unsigned n;
        for (unsigned side = 0; side < 2; side++) {
            n = cupidimage_pdf_region_line_ellipse(&s->ellipse_frame, s->x + side, s->y,
                                                   s->x + side, s->y + 1, e, ys);
            for (unsigned k = 0; k < n; k++)
                if (!region_event(s, ys[k]))
                    return 0;
        }
        if (!region_charge(s, (size_t)candidates + i))
            return 0;
        for (unsigned j = 0; j < candidates; j++) {
            const region_edge *p = s->candidates[j];
            n = cupidimage_pdf_region_line_ellipse(&s->ellipse_frame, p->x, p->y, p->x + p->dx,
                                                   p->y + p->dy, e, ys);
            for (unsigned k = 0; k < n; k++)
                if (!region_event(s, ys[k]))
                    return 0;
        }
        for (unsigned j = 0; j < i; j++) {
            n = cupidimage_pdf_region_ellipse_pair(&s->ellipse_frame, e, ellipses + j, ys);
            for (unsigned k = 0; k < n; k++)
                if (!region_event(s, ys[k]))
                    return 0;
        }
    }
    if (!region_charge(s, (size_t)s->event_count * 16))
        return 0;
    qsort(s->events, s->event_count, sizeof(*s->events), region_real_compare);
    *area = 0;
    for (unsigned j = 1; j < s->event_count; j++) {
        region_real lo = s->events[j - 1], hi = s->events[j];
        if (hi <= lo)
            continue;
        if (!region_charge(s, (size_t)count + regions + ellipse_count * 2))
            return 0;
        region_real y = (lo + hi) / 2;
        unsigned used = 0;
        int32_t winding[64] = {0};
        for (unsigned i = 0; i < count; i++) {
            const region_edge *e = active + i;
            if (y <= e->low || y >= e->high)
                continue;
            region_real x = region_x(e, y);
            if (x <= s->x)
                winding[e->region] += e->direction;
            else if (x < s->x + 1) {
                if (used == REGION_CROSSINGS)
                    return 0;
                crossings[used++] =
                    (region_crossing){x, (x - s->x) * (hi - lo), e->region, e->direction};
            }
        }
        for (unsigned i = 0; i < ellipse_count; i++) {
            const pdf_region_ellipse *e = ellipses + i;
            if (y <= e->y - s->ellipse_frame.height || y >= e->y + s->ellipse_frame.height)
                continue;
            for (int side = -1; side <= 1; side += 2) {
                region_real x = cupidimage_pdf_region_ellipse_x(&s->ellipse_frame, e, y, side);
                int direction = e->reverse ? -side : side;
                if (x <= s->x)
                    winding[e->region] += direction;
                else if (x < s->x + 1) {
                    if (used == REGION_CROSSINGS)
                        return 0;
                    region_real integral = cupidimage_pdf_region_ellipse_integral(
                        &s->ellipse_frame, e, side, s->x, lo, hi);
                    crossings[used++] = (region_crossing){x, integral, e->region, direction};
                }
            }
        }
        if (!region_charge(s, (size_t)used * (10 + regions)))
            return 0;
        qsort(crossings, used, sizeof(*crossings), region_crossing_compare);
        region_real previous = 0, contribution = 0;
        for (unsigned i = 0; i < used; i++) {
            if (region_inside(winding, even_odd, regions))
                contribution += crossings[i].integral - previous;
            previous = crossings[i].integral;
            winding[crossings[i].region] += crossings[i].direction;
        }
        if (region_inside(winding, even_odd, regions))
            contribution += hi - lo - previous;
        *area += contribution;
    }
    return isfinite(*area);
}

int cupidimage_pdf_region_coverage_ellipses(const pdf_region_edge *input, unsigned count,
                                            const pdf_region_ellipse *ellipses,
                                            unsigned ellipse_count, const double axes[4],
                                            const unsigned char *even_odd, unsigned regions,
                                            int left, int top, int right, int bottom, size_t *work,
                                            unsigned char **output) {
    if (!output)
        return 0;
    *output = NULL;
    if (!work || ellipse_count > 65536 || (ellipse_count && !ellipses) || count > 65536 ||
        regions > 64 || (count && !input) || (regions && !even_odd) || left > right ||
        top > bottom || left < -32768 || top < -32768 || right > 32768 || bottom > 32768)
        return 0;
    for (unsigned i = 0; i < regions; i++)
        if (even_odd[i] > 1)
            return 0;
    size_t width = (size_t)(right - left), height = (size_t)(bottom - top);
    if (width && height > 16777216 / width)
        return 0;
    region_sweep s = {0};
    s.work = work;
    if (!region_charge(&s, (size_t)ellipse_count * 4))
        return 0;
    if (ellipse_count && !cupidimage_pdf_region_ellipse_frame(axes, &s.ellipse_frame))
        return 0;
    for (unsigned i = 0; i < ellipse_count; i++) {
        const pdf_region_ellipse *e = ellipses + i;
        if (!isfinite(e->x) || !isfinite(e->y) || fabs(e->x) > 32768 || fabs(e->y) > 32768 ||
            e->region >= regions || e->reverse > 1)
            return 0;
    }
    if (!region_charge(&s, (size_t)count * 4 + width * height))
        return 0;
    region_edge *edges = malloc((count ? count : 1) * sizeof(*edges));
    region_edge *active = malloc((count ? count : 1) * sizeof(*active));
    pdf_region_ellipse *row_ellipses =
        malloc((ellipse_count ? ellipse_count : 1) * sizeof(*row_ellipses));
    s.events = malloc(REGION_EVENTS * sizeof(*s.events));
    s.candidates = malloc(REGION_CROSSINGS * sizeof(*s.candidates));
    unsigned char *pixels = calloc(width && height ? width * height : 1, 1);
    int ok = 0;
    unsigned n = 0;
    if (!edges || !active || !row_ellipses || !s.events || !s.candidates || !pixels)
        goto done;
    for (unsigned i = 0; i < count; i++) {
        const pdf_region_edge *p = input + i;
        if (p->region >= regions || !isfinite(p->x0) || !isfinite(p->y0) || !isfinite(p->x1) ||
            !isfinite(p->y1) || fabs(p->x0) > 32768 || fabs(p->y0) > 32768 || fabs(p->x1) > 32768 ||
            fabs(p->y1) > 32768)
            goto done;
        if (p->y0 == p->y1)
            continue;
        edges[n++] = (region_edge){p->x0,
                                   p->y0,
                                   (region_real)p->x1 - p->x0,
                                   (region_real)p->y1 - p->y0,
                                   fminl(p->y0, p->y1),
                                   fmaxl(p->y0, p->y1),
                                   p->region,
                                   p->y1 > p->y0 ? 1 : -1};
    }
    for (size_t y = 0; y < height; y++) {
        s.y = top + (region_real)y;
        unsigned used = 0;
        unsigned row_count = 0;
        if (!region_charge(&s, n + ellipse_count))
            goto done;
        for (unsigned i = 0; i < n; i++)
            if (edges[i].low < s.y + 1 && edges[i].high > s.y)
                active[used++] = edges[i];
        for (unsigned i = 0; i < ellipse_count; i++)
            if (ellipses[i].y - s.ellipse_frame.height < s.y + 1 &&
                ellipses[i].y + s.ellipse_frame.height > s.y)
                row_ellipses[row_count++] = ellipses[i];
        for (size_t x = 0; x < width; x++) {
            region_real area;
            s.x = left + (region_real)x;
            if (!region_pixel(&s, active, used, row_ellipses, row_count, even_odd, regions, &area))
                goto done;
            pixels[y * width + x] =
                (unsigned char)fminl(255, floorl(fmaxl(0, fminl(1, area)) * 256));
        }
    }
    *output = pixels;
    pixels = NULL;
    ok = 1;
done:
    free(edges);
    free(active);
    free(row_ellipses);
    free(s.events);
    free(s.candidates);
    free(pixels);
    return ok;
}

int cupidimage_pdf_region_coverage(const pdf_region_edge *edges, unsigned count,
                                   const unsigned char *even_odd, unsigned regions, int left,
                                   int top, int right, int bottom, size_t *work,
                                   unsigned char **output) {
    return cupidimage_pdf_region_coverage_ellipses(edges, count, NULL, 0, NULL, even_odd, regions,
                                                   left, top, right, bottom, work, output);
}
