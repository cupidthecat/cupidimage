#include "fonts/program.h"
#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double x, y;
} outline_point;
typedef struct {
    outline_point a, control, b;
    int quadratic;
} outline_edge;
static outline_point outline_mid(outline_point a, outline_point b) {
    return (outline_point){(a.x + b.x) * .5, (a.y + b.y) * .5};
}
static int outline_line(pdf_path *path, outline_point a, outline_point b, int first) {
    if (path->seg_count == 4096)
        return 0;
    if (!isfinite(a.x) || !isfinite(a.y) || !isfinite(b.x) || !isfinite(b.y) || fabs(a.x) > 1e30 ||
        fabs(a.y) > 1e30 || fabs(b.x) > 1e30 || fabs(b.y) > 1e30)
        return 0;
    path->segs[path->seg_count++] =
        (pdf_seg){(float)a.x, (float)a.y, (float)b.x, (float)b.y, first, 0};
    return 1;
}
static int outline_quad(pdf_path *path, outline_point a, outline_point control, outline_point b,
                        int first, unsigned depth) {
    double vx = b.x - a.x, vy = b.y - a.y, dx = control.x - a.x, dy = control.y - a.y;
    double length = vx * vx + vy * vy,
           t = length ? fmax(0, fmin(1, (dx * vx + dy * vy) / length)) : 0;
    dx -= t * vx;
    dy -= t * vy;
    if (dx * dx + dy * dy <= .015625)
        return outline_line(path, a, b, first);
    if (depth == 16)
        return 0;
    outline_point ac = outline_mid(a, control), cb = outline_mid(control, b),
                  mid = outline_mid(ac, cb);
    return outline_quad(path, a, ac, mid, first, depth + 1) &&
           outline_quad(path, mid, cb, b, 0, depth + 1);
}
static int outline_path(pdf_tt_outline *outline, pdf_path *path, outline_edge *edges,
                        unsigned *edge_count, const double m[6]) {
    for (unsigned i = 0; i < outline->count; i++) {
        double x = outline->points[i].x, y = outline->points[i].y;
        outline->points[i].x = m[0] * x + m[2] * y + m[4];
        outline->points[i].y = m[1] * x + m[3] * y + m[5];
        if (!isfinite(outline->points[i].x) || !isfinite(outline->points[i].y) ||
            fabs(outline->points[i].x) > 1e30 || fabs(outline->points[i].y) > 1e30)
            return 0;
        outline->points[i].x = (float)outline->points[i].x;
        outline->points[i].y = (float)outline->points[i].y;
    }
    unsigned first = 0;
    while (first < outline->count) {
        unsigned last = first;
        while (last < outline->count && !outline->points[last].end)
            last++;
        if (last == outline->count)
            return 0;
        pdf_tt_point *points = outline->points;
        outline_point a = {points[first].x, points[first].y}, z = {points[last].x, points[last].y};
        outline_point start = points[first].on  ? a
                              : points[last].on ? z
                                                : outline_mid(z, a),
                      current = start;
        unsigned index = first + (points[first].on ? 1 : 0),
                 remaining = last - first + 1 - (points[first].on || points[last].on ? 1 : 0);
        int new_path = 1;
        while (remaining) {
            outline_point p = {points[index].x, points[index].y};
            if (points[index].on) {
                edges[(*edge_count)++] = (outline_edge){current, current, p, 0};
                if (path && !outline_line(path, current, p, new_path))
                    return 0;
                current = p;
                index++;
                remaining--;
            } else {
                outline_point next = remaining > 1
                                         ? (outline_point){points[index + 1].x, points[index + 1].y}
                                         : start;
                int on = remaining == 1 || points[index + 1].on;
                outline_point end = on ? next : outline_mid(p, next);
                edges[(*edge_count)++] = (outline_edge){current, p, end, 1};
                if (path && !outline_quad(path, current, p, end, new_path, 0))
                    return 0;
                current = end;
                unsigned consumed = on && remaining > 1 ? 2 : 1;
                index += consumed;
                remaining -= consumed;
            }
            new_path = 0;
        }
        edges[(*edge_count)++] = (outline_edge){current, current, start, 0};
        if (path && !outline_line(path, current, start, new_path))
            return 0;
        if (path)
            path->segs[path->seg_count - 1].closes_subpath = 1;
        first = last + 1;
    }
    return 1;
}
static double outline_bezier(double a, double c, double b, double t) {
    double ac = a + (c - a) * t, cb = c + (b - c) * t;
    return ac + (cb - ac) * t;
}
/* Intersect the original quadratic, not its display-scale stroke tessellation.
   Splitting at the Y extremum gives the same half-open edge rule as lines. */
static int outline_winding(const outline_edge *edges, unsigned count, double x, double y) {
    int winding = 0;
    for (unsigned i = 0; i < count; i++) {
        const outline_edge *e = edges + i;
        if (!e->quadratic) {
            if ((e->a.y <= y && e->b.y > y) || (e->b.y <= y && e->a.y > y)) {
                double at = e->a.x + (y - e->a.y) * (e->b.x - e->a.x) / (e->b.y - e->a.y);
                if (at > x)
                    winding += e->b.y > e->a.y ? 1 : -1;
            }
            continue;
        }
        double a = e->a.y - 2 * e->control.y + e->b.y, b = 2 * (e->control.y - e->a.y),
               c = e->a.y - y;
        double cuts[3] = {0, 1, 1};
        unsigned intervals = 1;
        if (a) {
            double extremum = -b / (2 * a);
            if (extremum > 0 && extremum < 1) {
                cuts[1] = extremum;
                intervals = 2;
            }
        }
        double roots[2];
        unsigned root_count = 0;
        if (a == 0) {
            if (b)
                roots[root_count++] = -c / b;
        } else {
            double discriminant = b * b - 4 * a * c;
            if (discriminant >= 0) {
                double q = -.5 * (b + copysign(sqrt(discriminant), b));
                if (q) {
                    roots[root_count++] = q / a;
                    roots[root_count++] = c / q;
                } else
                    roots[root_count++] = -b / (2 * a);
            }
        }
        for (unsigned j = 0; j < intervals; j++) {
            double lo = cuts[j], hi = cuts[j + 1],
                   yl = outline_bezier(e->a.y, e->control.y, e->b.y, lo),
                   yh = outline_bezier(e->a.y, e->control.y, e->b.y, hi);
            if (!((yl <= y && yh > y) || (yh <= y && yl > y)))
                continue;
            for (unsigned k = 0; k < root_count; k++)
                if (roots[k] >= lo - 1e-12 && roots[k] <= hi + 1e-12) {
                    double t = fmax(lo, fmin(hi, roots[k]));
                    if (outline_bezier(e->a.x, e->control.x, e->b.x, t) > x)
                        winding += yh > yl ? 1 : -1;
                    break;
                }
        }
    }
    return winding;
}
int cupidimage_pdf_draw_truetype_glyph(pdf_render_state *st, const pdf_tt_program *program,
                                       unsigned glyph) {
    if (st->gs.text_mode == 3 || st->gs.font_size_pts == 0 || st->gs.horizontal_scale == 0)
        return 1;
    if (!st->form_budget->glyph_calls_left)
        return 0;
    st->form_budget->glyph_calls_left--;
    pdf_tt_outline outline = {0};
    pdf_path *path = NULL;
    outline_edge *edges = NULL;
    unsigned edge_count = 0;
    int mode = st->gs.text_mode & 3, clipping = st->gs.text_mode >= 4;
    unsigned char *stroke = NULL;
    int ok = 0;
    if (!cupidimage_pdf_tt_outline(&program->font, glyph, &outline))
        goto done;
    if (!outline.count) {
        ok = 1;
        goto done;
    }
    edges = malloc((size_t)outline.count * 2 * sizeof(*edges));
    if (!edges)
        goto done;
    if (mode == 1 || mode == 2) {
        path = calloc(1, sizeof(*path));
        if (!path)
            goto done;
    }
    const double *t = st->text_matrix;
    const float *c = st->gs.ctm;
    double ratio = (double)(96.0f / 72.0f), scale = st->gs.font_size_pts / program->font.units;
    double m[6] = {(c[0] * t[0] + c[2] * t[1]) * scale * st->gs.horizontal_scale * ratio,
                   -(c[1] * t[0] + c[3] * t[1]) * scale * st->gs.horizontal_scale * ratio,
                   (c[0] * t[2] + c[2] * t[3]) * scale * ratio,
                   -(c[1] * t[2] + c[3] * t[3]) * scale * ratio,
                   (c[0] * (t[4] + st->gs.text_rise * t[2]) +
                    c[2] * (t[5] + st->gs.text_rise * t[3]) + c[4] - st->page_x0_pts) *
                       ratio,
                   (st->page_top_pts - c[1] * (t[4] + st->gs.text_rise * t[2]) -
                    c[3] * (t[5] + st->gs.text_rise * t[3]) - c[5]) *
                       ratio};
    if (!outline_path(&outline, path, edges, &edge_count, m))
        goto done;
    if (clipping && !cupidimage_pdf_text_clip_prepare(st))
        goto done;
    double x0 = st->img->width, y0 = st->img->height, x1 = 0, y1 = 0;
    for (unsigned i = 0; i < outline.count; i++) {
        x0 = fmin(x0, outline.points[i].x);
        x1 = fmax(x1, outline.points[i].x);
        y0 = fmin(y0, outline.points[i].y);
        y1 = fmax(y1, outline.points[i].y);
    }
    if (mode == 1 || mode == 2) {
        size_t pixels = (size_t)st->img->width * st->img->height;
        stroke = calloc((pixels + 7) / 8, 1);
        if (!stroke)
            goto done;
        cupidimage_pdf_stroke_segments(st, path, stroke);
        if (st->image_failed)
            goto done;
        double margin =
            fmax(.5, st->gs.line_width_pts * ratio * .5 *
                         fmax(hypot(c[0], c[2]), hypot(c[1], c[3])) * fmax(1, st->gs.miter_limit)) +
            1;
        x0 -= margin;
        x1 += margin;
        y0 -= margin;
        y1 += margin;
    }
    int left = (int)fmax(0, fmin(st->img->width, floor(x0))),
        right = (int)fmax(0, fmin(st->img->width, ceil(x1)));
    int top = (int)fmax(0, fmin(st->img->height, floor(y0))),
        bottom = (int)fmax(0, fmin(st->img->height, ceil(y1)));
    size_t pixels = (size_t)(right - left) * (bottom - top);
    if (edge_count && pixels > st->clip_budget->tests_left / edge_count)
        goto done;
    st->clip_budget->tests_left -= pixels * edge_count;
    for (int y = top; y < bottom; y++)
        for (int x = left; x < right; x++) {
            int winding = outline_winding(edges, edge_count, x + .5, y + .5);
            size_t pixel = (size_t)y * st->img->width + x;
            unsigned bit = 1u << (pixel % 8);
            if (clipping && winding) {
                int64_t sum = (int64_t)st->text_winding[pixel] + winding;
                if (sum > INT32_MAX || sum < INT32_MIN)
                    goto done;
                st->text_winding[pixel] = (int32_t)sum;
                if (sum)
                    st->text_clip->bits[pixel / 8] |= (unsigned char)bit;
                else
                    st->text_clip->bits[pixel / 8] &= (unsigned char)~bit;
            }
            if (stroke && (stroke[pixel / 8] & bit))
                cupidimage_pdf_paint_pixel(st, x, y, 1, 1);
            else if (winding && (mode == 0 || mode == 2))
                cupidimage_pdf_paint_pixel(st, x, y, 0, 1);
        }
    ok = !st->image_failed;
done:
    free(stroke);
    free(path);
    free(edges);
    cupidimage_pdf_tt_outline_free(&outline);
    return ok;
}
