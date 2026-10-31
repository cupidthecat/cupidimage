#include "pdf_render_internal.h"
#include "raster/stroke_union.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double x, y;
} stroke_point;

typedef struct {
    stroke_point a, b, tangent;
    int previous, closed, point_dash;
} stroke_piece;

typedef struct {
    pdf_stroke_union_shape *shapes;
    unsigned count, capacity;
    double axes[4];
} stroke_aa_path;

typedef struct {
    pdf_render_state *state;
    unsigned char *mask;
    double matrix[4], inverse[4], radius;
    unsigned primitives;
    stroke_aa_path *aa;
} stroke_raster;

static stroke_point stroke_transform(const double m[4], stroke_point p) {
    return (stroke_point){m[0] * p.x + m[2] * p.y, m[1] * p.x + m[3] * p.y};
}

static double stroke_cross(stroke_point a, stroke_point b) {
    return a.x * b.y - a.y * b.x;
}

static stroke_point stroke_sub(stroke_point a, stroke_point b) {
    return (stroke_point){a.x - b.x, a.y - b.y};
}

static stroke_point stroke_add(stroke_point a, stroke_point b) {
    return (stroke_point){a.x + b.x, a.y + b.y};
}

static stroke_point stroke_scale(stroke_point a, double s) {
    return (stroke_point){a.x * s, a.y * s};
}

static void stroke_square_points(const stroke_raster *r, const stroke_piece *piece,
                                 stroke_point points[4]) {
    stroke_point v = stroke_scale(piece->tangent, r->radius);
    stroke_point n = {-v.y, v.x};
    points[0] = stroke_add(stroke_add(piece->a, n), v);
    points[1] = stroke_add(stroke_sub(piece->a, n), v);
    points[2] = stroke_sub(stroke_sub(piece->a, n), v);
    points[3] = stroke_sub(stroke_add(piece->a, n), v);
}

static int stroke_aa_append(stroke_raster *r, const stroke_point *points, unsigned count) {
    stroke_aa_path *path = r->aa;
    size_t *work = &r->state->clip_budget->tests_left;
    if (path->count == 65536 || *work < count)
        return 0;
    *work -= count;
    if (path->count == path->capacity) {
        unsigned capacity = path->capacity ? path->capacity * 2 : 64;
        if (path->count > *work)
            return 0;
        *work -= path->count;
        pdf_stroke_union_shape *next = realloc(path->shapes, (size_t)capacity * sizeof(*next));
        if (!next)
            return 0;
        path->shapes = next;
        path->capacity = capacity;
    }
    pdf_stroke_union_shape *shape = path->shapes + path->count++;
    memset(shape, 0, sizeof(*shape));
    shape->count = count;
    for (unsigned i = 0; i < count; i++)
        shape->points[i] = (pdf_stroke_union_point){points[i].x, points[i].y};
    return 1;
}

/* Each primitive ORs into one coverage mask: overlapping joins/caps paint once. */
static int stroke_primitive(stroke_raster *r, const stroke_point *points, int count, int circle) {
    if (++r->primitives > 262144)
        return 0;
    stroke_point device[4];
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (int i = 0; i < count; i++) {
        device[i] = stroke_transform(r->matrix, points[i]);
        x0 = fmin(x0, device[i].x);
        x1 = fmax(x1, device[i].x);
        y0 = fmin(y0, device[i].y);
        y1 = fmax(y1, device[i].y);
    }
    if (r->aa)
        return stroke_aa_append(r, device, (unsigned)count);
    if (circle) {
        double mx = r->radius * hypot(r->matrix[0], r->matrix[2]),
               my = r->radius * hypot(r->matrix[1], r->matrix[3]);
        x0 -= mx;
        x1 += mx;
        y0 -= my;
        y1 += my;
    }
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1))
        return 0;
    cupidimage_image *img = r->state->img;
    int left = (int)fmax(0, fmin(img->width, floor(x0))),
        right = (int)fmax(0, fmin(img->width, ceil(x1)));
    int top = (int)fmax(0, fmin(img->height, floor(y0))),
        bottom = (int)fmax(0, fmin(img->height, ceil(y1)));
    size_t tests = (size_t)(right - left) * (bottom - top);
    if (tests > r->state->clip_budget->tests_left)
        return 0;
    r->state->clip_budget->tests_left -= tests;
    for (int y = top; y < bottom; y++)
        for (int x = left; x < right; x++) {
            stroke_point p = {x + .5, y + .5};
            int inside = 1;
            if (circle) {
                p = stroke_sub(stroke_transform(r->inverse, p), points[0]);
                inside = p.x * p.x + p.y * p.y <= r->radius * r->radius;
            } else {
                int positive = 0, negative = 0;
                for (int i = 0; i < count; i++) {
                    double c = stroke_cross(stroke_sub(device[(i + 1) % count], device[i]),
                                            stroke_sub(p, device[i]));
                    positive |= c > 0;
                    negative |= c < 0;
                }
                inside = !(positive && negative);
            }
            if (inside) {
                size_t pixel = (size_t)y * img->width + x;
                r->mask[pixel / 8] |= (unsigned char)(1u << (pixel % 8));
            }
        }
    return 1;
}

static int stroke_cap(stroke_raster *r, stroke_point p, stroke_point direction, int cap) {
    if (cap == 0)
        return 1;
    if (cap == 1)
        return stroke_primitive(r, &p, 1, 1);
    stroke_point n = {-direction.y * r->radius, direction.x * r->radius},
                 v = stroke_scale(direction, r->radius);
    stroke_point box[4] = {stroke_add(p, n), stroke_add(stroke_add(p, n), v),
                           stroke_add(stroke_sub(p, n), v), stroke_sub(p, n)};
    return stroke_primitive(r, box, 4, 0);
}

static int stroke_join(stroke_raster *r, const stroke_piece *a, const stroke_piece *b) {
    stroke_point u = stroke_sub(a->b, a->a), v = stroke_sub(b->b, b->a), p = b->a;
    double lu = hypot(u.x, u.y), lv = hypot(v.x, v.y);
    if (!lu || !lv)
        return 1;
    u = stroke_scale(u, 1 / lu);
    v = stroke_scale(v, 1 / lv);
    double cross = stroke_cross(u, v);
    if (fabs(cross) < 1e-12)
        return u.x * v.x + u.y * v.y < 0 && r->state->gs.line_join == 1
                   ? stroke_primitive(r, &p, 1, 1)
                   : 1;
    if (r->state->gs.line_join == 1)
        return stroke_primitive(r, &p, 1, 1);
    double side = cross > 0 ? -1 : 1;
    stroke_point n1 = {-u.y * side * r->radius, u.x * side * r->radius},
                 n2 = {-v.y * side * r->radius, v.x * side * r->radius};
    stroke_point polygon[4] = {p, stroke_add(p, n1), stroke_add(p, n2), p};
    int count = 3;
    if (r->state->gs.line_join == 0) {
        double t = stroke_cross(stroke_sub(n2, n1), v) / cross;
        stroke_point m = stroke_add(n1, stroke_scale(u, t));
        if (hypot(m.x, m.y) <= r->radius * r->state->gs.miter_limit) {
            polygon[2] = stroke_add(p, m);
            polygon[3] = stroke_add(p, n2);
            count = 4;
        }
    }
    return stroke_primitive(r, polygon, count, 0);
}

static int stroke_append(stroke_piece **pieces, unsigned *count, unsigned *capacity, stroke_point a,
                         stroke_point b, int previous, stroke_point tangent, int point_dash) {
    if (*count == 65536)
        return 0;
    if (*count == *capacity) {
        unsigned next = *capacity ? *capacity * 2 : 256;
        stroke_piece *p = realloc(*pieces, (size_t)next * sizeof(*p));
        if (!p)
            return 0;
        *pieces = p;
        *capacity = next;
    }
    (*pieces)[(*count)++] = (stroke_piece){
        .a = a, .b = b, .tangent = tangent, .previous = previous, .point_dash = point_dash};
    return 1;
}

static int stroke_geometry(pdf_render_state *st, const pdf_path *path, unsigned char *target,
                           stroke_aa_path *aa) {
    if (st && path && path->overflow) {
        st->image_failed = 1;
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: path precision or segment limit exceeded");
        return 0;
    }
    if (!st || !st->img || !path)
        return 0;
    if (!path->seg_count)
        return 1;
    if (!cupidimage_pdf_path_valid(path))
        return 0;
    const pdf_seg *segments = cupidimage_pdf_path_segments(path);
    stroke_raster r = {0};
    r.state = st;
    r.aa = aa;
    r.radius = st->gs.line_width_pts * .5;
    const double *m = st->gs.ctm;
    double ratio = 96.0 / 72.0;
    r.matrix[0] = m[0] * ratio;
    r.matrix[1] = -m[1] * ratio;
    r.matrix[2] = m[2] * ratio;
    r.matrix[3] = -m[3] * ratio;
    double det = r.matrix[0] * r.matrix[3] - r.matrix[1] * r.matrix[2];
    if (!isfinite(det) || !isfinite(r.radius)) {
        st->image_failed = 1;
        return 0;
    }
    double dash_inverse[4] = {0};
    if (det) {
        dash_inverse[0] = r.matrix[3] / det;
        dash_inverse[1] = -r.matrix[1] / det;
        dash_inverse[2] = -r.matrix[2] / det;
        dash_inverse[3] = r.matrix[0] / det;
    }
    int hairline = !r.radius;
    if (!det && st->gs.dash_count) {
        st->image_failed = 1;
        return 0;
    }
    if (!r.radius) {
        r.radius = .5;
        r.matrix[0] = r.matrix[3] = 1;
        r.matrix[1] = r.matrix[2] = 0;
        det = 1;
    }
    if (!det)
        return 1;
    r.inverse[0] = r.matrix[3] / det;
    r.inverse[1] = -r.matrix[1] / det;
    r.inverse[2] = -r.matrix[2] / det;
    r.inverse[3] = r.matrix[0] / det;
    if (aa)
        for (unsigned i = 0; i < 4; i++)
            aa->axes[i] = r.matrix[i] * r.radius;
    size_t pixels = (size_t)st->img->width * st->img->height;
    r.mask = aa ? NULL : (target ? target : calloc((pixels + 7) / 8, 1));
    stroke_piece *pieces = NULL;
    unsigned count = 0, capacity = 0, steps = 0;
    int ok = aa || r.mask != NULL;
    double period = 0;
    for (int i = 0; i < st->gs.dash_count; i++)
        period += st->gs.dash[i];
    if (st->gs.dash_count % 2)
        period *= 2;
    for (int first = 0; ok && first < path->seg_count;) {
        int end = first + 1;
        while (end < path->seg_count && !segments[end].starts_subpath)
            end++;
        int all_zero = 1;
        for (int k = first; k < end; k++)
            if (segments[k].x0 != segments[k].x1 || segments[k].y0 != segments[k].y1)
                all_zero = 0;
        unsigned start = count;
        int index = 0, continues = 0;
        double remaining = 0;
        if (st->gs.dash_count) {
            double phase = fmod(st->gs.dash_phase, period);
            if (phase < 0)
                phase += period;
            remaining = st->gs.dash[0];
            while (phase > 0 && phase >= remaining) {
                phase -= remaining;
                index++;
                remaining = st->gs.dash[index % st->gs.dash_count];
                if (++steps > 65536) {
                    ok = 0;
                    break;
                }
            }
            remaining -= phase;
        }
        for (int i = first; ok && i < end; i++) {
            stroke_point a = stroke_transform(r.inverse,
                                              (stroke_point){segments[i].x0, segments[i].y0}),
                         b = stroke_transform(r.inverse,
                                              (stroke_point){segments[i].x1, segments[i].y1});
            stroke_point delta = stroke_sub(b, a);
            double geometry_length = hypot(delta.x, delta.y);
            double length = geometry_length;
            if (hairline && st->gs.dash_count) {
                stroke_point metric = stroke_transform(dash_inverse, delta);
                length = hypot(metric.x, metric.y);
            }
            if (!isfinite(length)) {
                ok = 0;
                break;
            }
            if (!length && (!all_zero || i != first))
                continue;
            if (!st->gs.dash_count) {
                ok = stroke_append(&pieces, &count, &capacity, a, b, count > start,
                                   (stroke_point){0, 0}, 0);
                continue;
            }
            double pos = 0;
            do {
                if (++steps > 65536) {
                    ok = 0;
                    break;
                }
                double take = fmin(remaining, length - pos);
                int on = (index % 2) == 0;
                stroke_point p = length ? stroke_add(a, stroke_scale(delta, pos / length)) : a,
                             q = length ? stroke_add(a, stroke_scale(delta, (pos + take) / length))
                                        : a;
                stroke_point tangent = geometry_length ? stroke_scale(delta, 1 / geometry_length)
                                                       : (stroke_point){0, 0};
                if (on && !stroke_append(&pieces, &count, &capacity, p, q, continues, tangent,
                                         take == 0 && geometry_length > 0)) {
                    ok = 0;
                    break;
                }
                pos += take;
                remaining -= take;
                continues = on;
                if (remaining <= 0) {
                    index++;
                    remaining = st->gs.dash[index % st->gs.dash_count];
                    continues = 0;
                }
                if (pos == length && remaining > 0)
                    break;
            } while (pos < length || remaining == 0);
        }
        if (ok && !st->gs.dash_count && count > start && segments[end - 1].closes_subpath) {
            stroke_point origin =
                stroke_transform(r.inverse, (stroke_point){segments[first].x0, segments[first].y0});
            if (hypot(pieces[start].a.x - origin.x, pieces[start].a.y - origin.y) < 1e-8 &&
                hypot(pieces[count - 1].b.x - origin.x, pieces[count - 1].b.y - origin.y) < 1e-8) {
                pieces[start].previous = 1;
                pieces[count - 1].closed = 1;
            }
        }
        for (unsigned i = start; ok && i < count; i++) {
            stroke_piece *piece = pieces + i;
            stroke_point d = stroke_sub(piece->b, piece->a);
            double length = hypot(d.x, d.y);
            int next = i + 1 < count ? pieces[i + 1].previous : piece->closed;
            if (length) {
                d = stroke_scale(d, 1 / length);
                stroke_point n = {-d.y * r.radius, d.x * r.radius};
                stroke_point box[4] = {stroke_add(piece->a, n), stroke_add(piece->b, n),
                                       stroke_sub(piece->b, n), stroke_sub(piece->a, n)};
                ok = stroke_primitive(&r, box, 4, 0);
                if (ok && !piece->previous)
                    ok = stroke_cap(&r, piece->a, stroke_scale(d, -1), st->gs.line_cap);
                if (ok && !next)
                    ok = stroke_cap(&r, piece->b, d, st->gs.line_cap);
            } else if (st->gs.line_cap == 1)
                ok = stroke_primitive(&r, &piece->a, 1, 1);
            else if (st->gs.line_cap == 2 && piece->point_dash) {
                stroke_point square[4];
                stroke_square_points(&r, piece, square);
                ok = stroke_primitive(&r, square, 4, 0);
            }
            if (ok && piece->previous)
                ok = stroke_join(&r, pieces + (i == start ? count - 1 : i - 1), piece);
        }
        first = end;
    }
    if (ok && !target && !aa)
        for (size_t p = 0; p < pixels; p++)
            if (r.mask[p / 8] & (1u << (p % 8)))
                cupidimage_pdf_paint_pixel(st, (int)(p % st->img->width), (int)(p / st->img->width),
                                           1, 1);
    if (!target)
        free(r.mask);
    free(pieces);
    if (!ok) {
        st->image_failed = 1;
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: stroke allocation or work limit exceeded");
    }
    return ok;
}

void cupidimage_pdf_stroke_segments(pdf_render_state *st, const pdf_path *path,
                                    unsigned char *target) {
    if (st->graphics_antialias && !target) {
        (void)cupidimage_pdf_paint_path_coverage(st, path, 0, 1, 0);
        return;
    }
    (void)stroke_geometry(st, path, target, NULL);
}

static int stroke_clip_coverage(pdf_render_state *st, const stroke_aa_path *path, int left, int top,
                                int right, int bottom, unsigned char **output) {
    unsigned lines = 0, circles = 0;
    for (unsigned i = 0; i < path->count; i++) {
        if (path->shapes[i].count == 1)
            circles++;
        else
            lines += path->shapes[i].count;
    }
    if (lines > 65536 || lines + circles > st->clip_budget->tests_left)
        return 0;
    st->clip_budget->tests_left -= lines + circles;
    pdf_region_edge *edges = malloc((lines ? lines : 1) * sizeof(*edges));
    pdf_region_ellipse *ellipses = malloc((circles ? circles : 1) * sizeof(*ellipses));
    unsigned n = 0, k = 0;
    unsigned char *mask = NULL;
    int ok = 0;
    if (!edges || !ellipses)
        goto done;
    for (unsigned i = 0; i < path->count; i++) {
        const pdf_stroke_union_shape *shape = path->shapes + i;
        if (shape->count == 1) {
            ellipses[k++] = (pdf_region_ellipse){shape->points[0].x, shape->points[0].y, 0, 0};
            continue;
        }
        double area = 0;
        for (unsigned j = 0; j < shape->count; j++) {
            const pdf_stroke_union_point a = shape->points[j],
                                         b = shape->points[(j + 1) % shape->count],
                                         o = shape->points[0];
            area += (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        }
        for (unsigned j = 0; j < shape->count; j++) {
            unsigned a = area < 0 ? (shape->count - j) % shape->count : j;
            unsigned b = area < 0 ? (shape->count - j - 1) % shape->count : (j + 1) % shape->count;
            edges[n++] = (pdf_region_edge){shape->points[a].x, shape->points[a].y,
                                           shape->points[b].x, shape->points[b].y, 0};
        }
    }
    if (!cupidimage_pdf_clip_intersect(st, edges, n, 0, ellipses, k, path->axes, left, top, right,
                                       bottom, &mask))
        goto done;
    if (!cupidimage_pdf_clip_limit_mask(st, *output, 0, mask, left, top, right, bottom))
        goto done;
    free(*output);
    *output = mask;
    mask = NULL;
    ok = 1;
done:
    free(edges);
    free(ellipses);
    free(mask);
    return ok;
}

int cupidimage_pdf_stroke_path_coverage(pdf_render_state *st, const pdf_path *path, int left,
                                        int top, int right, int bottom, int clipped,
                                        unsigned char **output) {
    if (!output)
        return 0;
    *output = NULL;
    stroke_aa_path primitives = {0};
    int ok = stroke_geometry(st, path, NULL, &primitives);
    if (ok)
        ok = cupidimage_pdf_stroke_union(primitives.shapes, primitives.count, primitives.axes, left,
                                         top, right, bottom, &st->clip_budget->tests_left, output);
    if (ok && clipped)
        ok = stroke_clip_coverage(st, &primitives, left, top, right, bottom, output);
    free(primitives.shapes);
    if (!ok) {
        free(*output);
        *output = NULL;
    }
    return ok;
}
