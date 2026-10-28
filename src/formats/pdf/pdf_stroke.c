#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ISO 32000-1, 8.4.3: lengths and dash phase are measured in user space. */
int cupidimage_pdf_set_dash(pdf_graphics_state *gs, const unsigned char *data, size_t length,
                            double phase) {
    double values[32], sum = 0;
    int count = 0;
    size_t pos = 0;
    if (!isfinite(phase) || fabs(phase) > 1e30)
        return 0;
    while (1) {
        cupidimage_pdf_skip_ws_and_comments(data, length, &pos);
        if (pos == length)
            break;
        if (count == 32 || !cupidimage_pdf_parse_pdf_number(data, length, &pos, &values[count]) ||
            !isfinite(values[count]) || values[count] < 0 || values[count] > 1e30)
            return 0;
        if (pos < length && !cupidimage_pdf_is_ws(data[pos]) && data[pos] != '%')
            return 0;
        sum += values[count++];
    }
    if (count && sum == 0)
        return 0;
    memcpy(gs->dash, values, (size_t)count * sizeof(double));
    gs->dash_count = count;
    gs->dash_phase = phase;
    return 1;
}

static int stroke_skip_reference(const pdf_object_view *view, size_t *pos) {
    double ignored;
    for (int i = 0; i < 2; i++) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
        if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &ignored))
            return 0;
    }
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    return *pos < view->len && view->data[(*pos)++] == 'R';
}

static int stroke_read_array(const pdf_render_state *st, const pdf_object_view *view, size_t *pos,
                             pdf_graphics_state *gs, unsigned depth) {
    pdf_object_id object;
    if (depth == 16)
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    if (cupidimage_pdf_page_reference(view, *pos, &object)) {
        pdf_object_view indirect = {0};
        size_t p = 0;
        int ok = cupidimage_pdf_state_object(st, object, &indirect) &&
                 stroke_read_array(st, &indirect, &p, gs, depth + 1) &&
                 stroke_skip_reference(view, pos);
        cupidimage_pdf_object_view_release(&indirect);
        return ok;
    }
    if (*pos == view->len || view->data[(*pos)++] != '[')
        return 0;
    double sum = 0;
    gs->dash_count = 0;
    while (1) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
        if (*pos < view->len && view->data[*pos] == ']') {
            (*pos)++;
            break;
        }
        double value;
        if (gs->dash_count == 32 || !cupidimage_pdf_read_number(st, view, *pos, 0, &value) ||
            !isfinite(value) || value < 0 || value > 1e30)
            return 0;
        if (cupidimage_pdf_page_reference(view, *pos, &object)) {
            if (!stroke_skip_reference(view, pos))
                return 0;
        } else {
            double ignored;
            if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &ignored))
                return 0;
        }
        sum += value;
        gs->dash[gs->dash_count++] = value;
    }
    return !gs->dash_count || sum > 0;
}

static int stroke_read_dash(const pdf_render_state *st, const pdf_object_view *view, size_t pos,
                            pdf_graphics_state *gs, unsigned depth) {
    pdf_object_id object;
    if (depth == 16)
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    if (cupidimage_pdf_page_reference(view, pos, &object)) {
        pdf_object_view indirect = {0};
        int ok = cupidimage_pdf_state_object(st, object, &indirect) &&
                 stroke_read_dash(st, &indirect, 0, gs, depth + 1);
        cupidimage_pdf_object_view_release(&indirect);
        return ok;
    }
    if (pos == view->len || view->data[pos++] != '[' || !stroke_read_array(st, view, &pos, gs, 0))
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    double phase;
    if (!cupidimage_pdf_read_number(st, view, pos, 0, &phase) || !isfinite(phase) ||
        fabs(phase) > 1e30)
        return 0;
    if (cupidimage_pdf_page_reference(view, pos, &object)) {
        if (!stroke_skip_reference(view, &pos))
            return 0;
    } else {
        double ignored;
        if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, &pos, &ignored))
            return 0;
    }
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    if (pos == view->len || view->data[pos] != ']')
        return 0;
    gs->dash_phase = phase;
    return 1;
}

int cupidimage_pdf_set_flatness(pdf_graphics_state *state, double value) {
    if (!isfinite(value) || value < 0 || value > 100)
        return 0;
    state->flatness = value ? value : 1;
    return 1;
}

double cupidimage_pdf_flatness(const pdf_graphics_state *state, double ceiling) {
    return fmin(state->flatness > 0 ? state->flatness : 1, ceiling);
}

int cupidimage_pdf_apply_stroke_view(const pdf_render_state *st, const pdf_object_view *view,
                                     pdf_graphics_state *next) {
    double flatness = next->flatness;
    if (!cupidimage_pdf_graphics_number(st, view, "FL", 0, 100, &flatness) ||
        !cupidimage_pdf_set_flatness(next, flatness))
        return 0;
    const char *keys[] = {"LC", "LJ", "ML"};
    double numbers[3] = {next->line_cap, next->line_join, next->miter_limit};
    for (int i = 0; i < 3; i++) {
        if (!cupidimage_pdf_graphics_number(st, view, keys[i], i == 2 ? 1 : 0, i == 2 ? 1e6 : 2,
                                            &numbers[i]) ||
            (i != 2 && floor(numbers[i]) != numbers[i]))
            return 0;
    }
    next->line_cap = (int)numbers[0];
    next->line_join = (int)numbers[1];
    next->miter_limit = numbers[2];
    pdf_object_view dash = {0};
    int present = cupidimage_pdf_optional_value(st->document, st->document_size, view, "D", &dash);
    int ok = present == 0 || (present > 0 && stroke_read_dash(st, &dash, 0, next, 0));
    cupidimage_pdf_object_view_release(&dash);
    return ok;
}

typedef struct {
    double x, y;
} stroke_point;

typedef struct {
    stroke_point a, b;
    int previous, closed;
} stroke_piece;

typedef struct {
    pdf_render_state *state;
    unsigned char *mask;
    double matrix[4], inverse[4], radius;
    unsigned primitives;
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
                         stroke_point b, int previous) {
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
    (*pieces)[(*count)++] = (stroke_piece){a, b, previous, 0};
    return 1;
}

void cupidimage_pdf_stroke_segments(pdf_render_state *st, const pdf_path *path,
                                    unsigned char *target) {
    if (st && path && path->overflow) {
        st->image_failed = 1;
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: path precision or segment limit exceeded");
        return;
    }
    if (!st || !st->img || !path || !path->seg_count)
        return;
    stroke_raster r = {0};
    r.state = st;
    r.radius = st->gs.line_width_pts * .5;
    const float *m = st->gs.ctm;
    double ratio = 96.0 / 72.0;
    r.matrix[0] = m[0] * ratio;
    r.matrix[1] = -m[1] * ratio;
    r.matrix[2] = m[2] * ratio;
    r.matrix[3] = -m[3] * ratio;
    double det = r.matrix[0] * r.matrix[3] - r.matrix[1] * r.matrix[2];
    if (!isfinite(det) || !isfinite(r.radius)) {
        st->image_failed = 1;
        return;
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
        return;
    }
    if (!r.radius) {
        r.radius = .5;
        r.matrix[0] = r.matrix[3] = 1;
        r.matrix[1] = r.matrix[2] = 0;
        det = 1;
    }
    if (!det)
        return;
    r.inverse[0] = r.matrix[3] / det;
    r.inverse[1] = -r.matrix[1] / det;
    r.inverse[2] = -r.matrix[2] / det;
    r.inverse[3] = r.matrix[0] / det;
    size_t pixels = (size_t)st->img->width * st->img->height;
    r.mask = target ? target : calloc((pixels + 7) / 8, 1);
    stroke_piece *pieces = NULL;
    unsigned count = 0, capacity = 0, steps = 0;
    int ok = r.mask != NULL;
    double period = 0;
    for (int i = 0; i < st->gs.dash_count; i++)
        period += st->gs.dash[i];
    if (st->gs.dash_count % 2)
        period *= 2;
    for (int first = 0; ok && first < path->seg_count;) {
        int end = first + 1;
        while (end < path->seg_count && !path->segs[end].starts_subpath)
            end++;
        int all_zero = 1;
        for (int k = first; k < end; k++)
            if (path->segs[k].x0 != path->segs[k].x1 || path->segs[k].y0 != path->segs[k].y1)
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
                                              (stroke_point){path->segs[i].x0, path->segs[i].y0}),
                         b = stroke_transform(r.inverse,
                                              (stroke_point){path->segs[i].x1, path->segs[i].y1});
            stroke_point delta = stroke_sub(b, a);
            double length = hypot(delta.x, delta.y);
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
                ok = stroke_append(&pieces, &count, &capacity, a, b, count > start);
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
                if (on && !stroke_append(&pieces, &count, &capacity, p, q, continues)) {
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
        if (ok && !st->gs.dash_count && count > start && path->segs[end - 1].closes_subpath) {
            stroke_point origin = stroke_transform(
                r.inverse, (stroke_point){path->segs[first].x0, path->segs[first].y0});
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
            if (ok && piece->previous)
                ok = stroke_join(&r, pieces + (i == start ? count - 1 : i - 1), piece);
        }
        first = end;
    }
    if (ok && !target)
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
}
