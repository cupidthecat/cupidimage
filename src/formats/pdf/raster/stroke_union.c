#include "stroke_union.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define UNION_CANDIDATES 256
#define UNION_EVENTS 65536

typedef long double real;

typedef struct {
    real x, y;
} point;

typedef struct {
    point p[4];
    unsigned count;
    real box[4];
} shape;

typedef struct {
    real a, b, c, d, h;
    int curved;
} boundary;

typedef struct {
    boundary lo, hi;
    real xlo, xhi;
} interval;

typedef struct {
    real *events;
    unsigned event_count;
    size_t *work;
    real x, y, ax[4], inv[4], h, k, skew;
} sweep;

static int charge(sweep *s, size_t n) {
    if (n > *s->work)
        return 0;
    *s->work -= n;
    return 1;
}

static point sub(point a, point b) {
    return (point){a.x - b.x, a.y - b.y};
}

static point add(point a, point b) {
    return (point){a.x + b.x, a.y + b.y};
}

static point scale(point a, real b) {
    return (point){a.x * b, a.y * b};
}

static real cross(point a, point b) {
    return a.x * b.y - a.y * b.x;
}

static real dot(point a, point b) {
    return a.x * b.x + a.y * b.y;
}

static point transform(const real m[4], point p) {
    return (point){m[0] * p.x + m[2] * p.y, m[1] * p.x + m[3] * p.y};
}

static int event(sweep *s, real y) {
    if (y <= s->y || y >= s->y + 1)
        return 1;
    if (!isfinite(y) || s->event_count == UNION_EVENTS || !charge(s, 1))
        return 0;
    s->events[s->event_count++] = y;
    return 1;
}

static int compare_real(const void *a, const void *b) {
    real x = *(const real *)a, y = *(const real *)b;
    return (x > y) - (x < y);
}

static int compare_interval(const void *a, const void *b) {
    const interval *x = a, *y = b;
    return (x->xlo > y->xlo) - (x->xlo < y->xlo);
}

static int contains(const sweep *s, const shape *p, point q) {
    if (p->count == 1) {
        point v = transform(s->inv, sub(q, p->p[0]));
        return dot(v, v) <= 1;
    }
    int pos = 0, neg = 0;
    for (unsigned i = 0; i < p->count; i++) {
        real c = cross(sub(p->p[(i + 1) % p->count], p->p[i]), sub(q, p->p[i]));
        pos |= c > 0;
        neg |= c < 0;
    }
    return !(pos && neg);
}

static int line_line(sweep *s, point a, point b, point c, point d) {
    point u = sub(b, a), v = sub(d, c), w = sub(c, a);
    real det = cross(u, v);
    if (!det)
        return 1;
    real t = cross(w, v) / det, z = cross(w, u) / det;
    if (t < 0 || t > 1 || z < 0 || z > 1)
        return 1;
    return event(s, a.y + t * u.y);
}

static int line_ellipse(sweep *s, point a, point b, point centre) {
    point u = transform(s->inv, sub(b, a)), v = transform(s->inv, sub(a, centre));
    real aa = dot(u, u), bb = dot(u, v), cc = dot(v, v) - 1;
    if (!aa)
        return 1;
    real disc = bb * bb - aa * cc;
    if (disc < 0)
        return 1;
    real root = sqrtl(disc);
    /* Stable quadratic roots, including the tangent and zero-root cases. */
    real q = -bb - copysignl(root, bb), t[2];
    t[0] = q / aa;
    t[1] = q ? cc / q : -bb / aa;
    for (unsigned i = 0; i < 2; i++)
        if (t[i] >= 0 && t[i] <= 1 && !event(s, a.y + t[i] * (b.y - a.y)))
            return 0;
    return 1;
}

static int ellipse_ellipse(sweep *s, point a, point b) {
    point delta = transform(s->inv, sub(b, a));
    real d2 = dot(delta, delta);
    if (d2 == 0 || d2 > 4)
        return 1;
    real factor = sqrtl(fmaxl(0, 1 - d2 / 4) / d2);
    point offset = {-delta.y * factor, delta.x * factor}, mid = scale(delta, .5L);
    point p = add(a, transform(s->ax, add(mid, offset)));
    point q = add(a, transform(s->ax, sub(mid, offset)));
    return event(s, p.y) && event(s, q.y);
}

static int intersections(sweep *s, const shape *a, const shape *b) {
    if (!charge(s, 16))
        return 0;
    if (a->count == 1 && b->count == 1)
        return ellipse_ellipse(s, a->p[0], b->p[0]);
    if (a->count == 1) {
        const shape *t = a;
        a = b;
        b = t;
    }
    for (unsigned i = 0; i < a->count; i++) {
        point p = a->p[i], q = a->p[(i + 1) % a->count];
        if (b->count == 1) {
            if (!line_ellipse(s, p, q, b->p[0]))
                return 0;
        } else
            for (unsigned j = 0; j < b->count; j++)
                if (!line_line(s, p, q, b->p[j], b->p[(j + 1) % b->count]))
                    return 0;
    }
    return 1;
}

static real value(boundary b, real y) {
    if (!b.curved)
        return b.a + b.b * (y - b.c);
    real t = (y - b.c) / b.h;
    return b.a + b.b * (y - b.c) + b.d * sqrtl(fmaxl(0, 1 - t * t));
}

static real integral(boundary b, real lo, real hi) {
    real result = (hi - lo) * (b.a + b.b * ((lo + hi) / 2 - b.c));
    if (b.curved) {
        real u = fmaxl(-1, fminl(1, (lo - b.c) / b.h));
        real v = fmaxl(-1, fminl(1, (hi - b.c) / b.h));
        result +=
            b.d * b.h * .5L *
            (v * sqrtl(fmaxl(0, 1 - v * v)) + asinl(v) - u * sqrtl(fmaxl(0, 1 - u * u)) - asinl(u));
    }
    return result;
}

static int section(sweep *s, const shape *p, real y, interval *out) {
    boundary lo = {0}, hi = {0};
    real xmin = INFINITY, xmax = -INFINITY;
    if (p->count == 1) {
        if (y <= p->p[0].y - s->h || y >= p->p[0].y + s->h)
            return 0;
        lo = (boundary){p->p[0].x, s->skew, p->p[0].y, -s->k, s->h, 1};
        hi = lo;
        hi.d = s->k;
        xmin = value(lo, y);
        xmax = value(hi, y);
    } else
        for (unsigned i = 0; i < p->count; i++) {
            point a = p->p[i], b = p->p[(i + 1) % p->count];
            if (y <= fminl(a.y, b.y) || y >= fmaxl(a.y, b.y))
                continue;
            boundary edge = {a.x, (b.x - a.x) / (b.y - a.y), a.y, 0, 0, 0};
            real x = value(edge, y);
            if (x < xmin) {
                xmin = x;
                lo = edge;
            }
            if (x > xmax) {
                xmax = x;
                hi = edge;
            }
        }
    if (xmin >= s->x + 1 || xmax <= s->x || xmin >= xmax)
        return 0;
    if (xmin < s->x) {
        xmin = s->x;
        lo = (boundary){.a = xmin};
    }
    if (xmax > s->x + 1) {
        xmax = s->x + 1;
        hi = (boundary){.a = xmax};
    }
    *out = (interval){lo, hi, xmin, xmax};
    return 1;
}

static int pixel(sweep *s, const shape *shapes, unsigned count, real *area) {
    const shape *candidate[UNION_CANDIDATES];
    unsigned n = 0;
    interval intervals[UNION_CANDIDATES];
    *area = 0;
    s->event_count = 0;
    if (!charge(s, count))
        return 0;
    for (unsigned i = 0; i < count; i++) {
        const shape *p = shapes + i;
        if (p->box[0] >= s->x + 1 || p->box[2] <= s->x || p->box[1] >= s->y + 1 ||
            p->box[3] <= s->y)
            continue;
        if (!charge(s, 16))
            return 0;
        if (contains(s, p, (point){s->x, s->y}) && contains(s, p, (point){s->x + 1, s->y}) &&
            contains(s, p, (point){s->x, s->y + 1}) &&
            contains(s, p, (point){s->x + 1, s->y + 1})) {
            *area = 1;
            return 1;
        }
        if (n == UNION_CANDIDATES)
            return 0;
        candidate[n++] = p;
    }
    if (!n)
        return 1;
    s->events[s->event_count++] = s->y;
    s->events[s->event_count++] = s->y + 1;
    for (unsigned i = 0; i < n; i++) {
        const shape *p = candidate[i];
        if (p->count == 1) {
            if (!event(s, p->box[1]) || !event(s, p->box[3]))
                return 0;
            for (unsigned side = 0; side < 2; side++)
                if (!line_ellipse(s, (point){s->x + side, s->y}, (point){s->x + side, s->y + 1},
                                  p->p[0]))
                    return 0;
        } else
            for (unsigned j = 0; j < p->count; j++) {
                point a = p->p[j], b = p->p[(j + 1) % p->count];
                if (!event(s, a.y))
                    return 0;
                for (unsigned side = 0; side < 2; side++)
                    if (!line_line(s, a, b, (point){s->x + side, s->y},
                                   (point){s->x + side, s->y + 1}))
                        return 0;
            }
        for (unsigned j = 0; j < i; j++)
            if (!intersections(s, p, candidate[j]))
                return 0;
    }
    if (!charge(s, (size_t)s->event_count * 16))
        return 0;
    qsort(s->events, s->event_count, sizeof(real), compare_real);
    for (unsigned i = 1; i < s->event_count; i++) {
        real lo = s->events[i - 1], hi = s->events[i];
        if (hi <= lo)
            continue;
        if (!charge(s, (size_t)n * 16))
            return 0;
        unsigned used = 0;
        for (unsigned j = 0; j < n; j++)
            used += section(s, candidate[j], (lo + hi) / 2, intervals + used);
        if (!used)
            continue;
        qsort(intervals, used, sizeof(*intervals), compare_interval);
        interval current = intervals[0];
        for (unsigned j = 1; j <= used; j++) {
            if (j == used || intervals[j].xlo > current.xhi) {
                *area += integral(current.hi, lo, hi) - integral(current.lo, lo, hi);
                if (j < used)
                    current = intervals[j];
            } else if (intervals[j].xhi > current.xhi) {
                current.hi = intervals[j].hi;
                current.xhi = intervals[j].xhi;
            }
        }
    }
    return isfinite(*area);
}

int cupidimage_pdf_stroke_union(const pdf_stroke_union_shape *input, unsigned count,
                                const double axes[4], int left, int top, int right, int bottom,
                                size_t *work, unsigned char **output) {
    if (!output)
        return 0;
    *output = NULL;
    if (!work || !axes || count > 65536 || (count && !input) || left > right || top > bottom ||
        left < -32768 || top < -32768 || right > 32768 || bottom > 32768)
        return 0;
    size_t width = (size_t)(right - left), height = (size_t)(bottom - top);
    if (width && height > 16777216 / width)
        return 0;
    sweep s = {0};
    s.work = work;
    for (unsigned i = 0; i < 4; i++) {
        if (!isfinite(axes[i]) || fabs(axes[i]) > 65536)
            return 0;
        s.ax[i] = axes[i];
    }
    real det = s.ax[0] * s.ax[3] - s.ax[1] * s.ax[2];
    s.h = hypotl(s.ax[1], s.ax[3]);
    if (det && s.h) {
        s.inv[0] = s.ax[3] / det;
        s.inv[1] = -s.ax[1] / det;
        s.inv[2] = -s.ax[2] / det;
        s.inv[3] = s.ax[0] / det;
        /* Bound normalized coordinates before quadratic intersection products.
         * This also applies on platforms where long double equals double. */
        for (unsigned i = 0; i < 4; i++)
            if (!isfinite(s.inv[i]) || fabsl(s.inv[i]) > 1e64L)
                return 0;
        s.k = fabsl(det) / s.h;
        s.skew = (s.ax[0] * s.ax[1] + s.ax[2] * s.ax[3]) / (s.h * s.h);
    }
    if (!charge(&s, (size_t)count * 4 + width * height))
        return 0;
    shape *shapes = calloc(count ? count : 1, sizeof(*shapes));
    shape *tile = malloc((count ? count : 1) * sizeof(*tile));
    unsigned char *pixels = calloc(width && height ? width * height : 1, 1);
    s.events = malloc(UNION_EVENTS * sizeof(*s.events));
    int ok = 0;
    if (!shapes || !tile || !pixels || !s.events)
        goto done;
    for (unsigned i = 0; i < count; i++) {
        shape *p = shapes + i;
        p->count = input[i].count;
        if (p->count != 1 && p->count != 3 && p->count != 4)
            goto done;
        if (p->count == 1 && (!det || !s.h))
            goto done;
        p->box[0] = p->box[1] = INFINITY;
        p->box[2] = p->box[3] = -INFINITY;
        for (unsigned j = 0; j < p->count; j++) {
            double x = input[i].points[j].x, y = input[i].points[j].y;
            if (!isfinite(x) || !isfinite(y) || fabs(x) > 65536 || fabs(y) > 65536)
                goto done;
            p->p[j] = (point){x, y};
            p->box[0] = fminl(p->box[0], x);
            p->box[1] = fminl(p->box[1], y);
            p->box[2] = fmaxl(p->box[2], x);
            p->box[3] = fmaxl(p->box[3], y);
        }
        if (p->count == 1) {
            real hx = hypotl(s.ax[0], s.ax[2]);
            p->box[0] -= hx;
            p->box[2] += hx;
            p->box[1] -= s.h;
            p->box[3] += s.h;
        } else {
            int positive = 0, negative = 0;
            real twice_area = 0;
            for (unsigned j = 0; j < p->count; j++) {
                point a = p->p[j], b = p->p[(j + 1) % p->count], c = p->p[(j + 2) % p->count];
                real turn = cross(sub(b, a), sub(c, b));
                positive |= turn > 0;
                negative |= turn < 0;
                twice_area += cross(sub(a, p->p[0]), sub(b, p->p[0]));
            }
            if (positive && negative)
                goto done;
            if (!twice_area)
                p->box[2] = p->box[0];
        }
    }
    /* A small tile rejects distant primitives before the per-pixel sweep. A
     * convex primitive covering its four corners covers the whole tile. */
    for (size_t ty = 0; ty < height; ty += 8)
        for (size_t tx = 0; tx < width; tx += 8) {
            size_t ex = tx + 8 < width ? tx + 8 : width, ey = ty + 8 < height ? ty + 8 : height;
            unsigned used = 0;
            int full = 0;
            real x0 = left + (real)tx, y0 = top + (real)ty, x1 = left + (real)ex,
                 y1 = top + (real)ey;
            if (!charge(&s, count))
                goto done;
            for (unsigned i = 0; i < count; i++) {
                const shape *p = shapes + i;
                if (p->box[0] >= x1 || p->box[2] <= x0 || p->box[1] >= y1 || p->box[3] <= y0)
                    continue;
                if (!charge(&s, 16))
                    goto done;
                if (contains(&s, p, (point){x0, y0}) && contains(&s, p, (point){x1, y0}) &&
                    contains(&s, p, (point){x0, y1}) && contains(&s, p, (point){x1, y1})) {
                    full = 1;
                    break;
                }
                tile[used++] = *p;
            }
            for (size_t y = ty; y < ey; y++)
                for (size_t x = tx; x < ex; x++) {
                    real area = 1;
                    s.x = left + (real)x;
                    s.y = top + (real)y;
                    if (!full && !pixel(&s, tile, used, &area))
                        goto done;
                    pixels[y * width + x] =
                        (unsigned char)fminl(255, floorl(fmaxl(0, fminl(1, area)) * 256));
                }
        }
    *output = pixels;
    pixels = NULL;
    ok = 1;
done:
    free(shapes);
    free(tile);
    free(pixels);
    free(s.events);
    return ok;
}
