#include "fonts/postscript/page.h"
#include "fonts/program.h"
#include "pdf_render_internal.h"
#include "raster/font_coverage.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double x, y;
} outline_point;

typedef struct {
    outline_point a, control, b;
    int quadratic;
    outline_point control2;
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
    path->segs[path->seg_count++] = (pdf_seg){.x0 = (float)a.x,
                                              .y0 = (float)a.y,
                                              .x1 = (float)b.x,
                                              .y1 = (float)b.y,
                                              .starts_subpath = first};
    return 1;
}

static int outline_quad(pdf_path *path, outline_point a, outline_point control, outline_point b,
                        int first, unsigned depth, double flatness_squared) {
    double vx = b.x - a.x, vy = b.y - a.y, dx = control.x - a.x, dy = control.y - a.y;
    double length = vx * vx + vy * vy,
           t = length ? fmax(0, fmin(1, (dx * vx + dy * vy) / length)) : 0;
    dx -= t * vx;
    dy -= t * vy;
    if (dx * dx + dy * dy <= flatness_squared)
        return outline_line(path, a, b, first);
    if (depth == 16)
        return 0;
    outline_point ac = outline_mid(a, control), cb = outline_mid(control, b),
                  mid = outline_mid(ac, cb);
    return outline_quad(path, a, ac, mid, first, depth + 1, flatness_squared) &&
           outline_quad(path, mid, cb, b, 0, depth + 1, flatness_squared);
}

static double outline_distance(outline_point a, outline_point b, outline_point p) {
    double x = b.x - a.x, y = b.y - a.y, dx = p.x - a.x, dy = p.y - a.y, length = x * x + y * y;
    double t = length ? fmax(0, fmin(1, (dx * x + dy * y) / length)) : 0;
    dx -= t * x;
    dy -= t * y;
    return dx * dx + dy * dy;
}

static int outline_cubic(pdf_path *path, outline_point a, outline_point c1, outline_point c2,
                         outline_point b, int first, unsigned depth, double flatness_squared) {
    if (fmax(outline_distance(a, b, c1), outline_distance(a, b, c2)) <= flatness_squared)
        return outline_line(path, a, b, first);
    if (depth == 16)
        return 0;
    outline_point ac = outline_mid(a, c1), cc = outline_mid(c1, c2), cb = outline_mid(c2, b),
                  left = outline_mid(ac, cc), right = outline_mid(cc, cb),
                  mid = outline_mid(left, right);
    return outline_cubic(path, a, ac, left, mid, first, depth + 1, flatness_squared) &&
           outline_cubic(path, mid, right, cb, b, 0, depth + 1, flatness_squared);
}

static int outline_cff_path(const pdf_cff_outline *outline, pdf_path *path, outline_edge *edges,
                            unsigned *count, const double m[6], double flatness_squared) {
    outline_point current = {0};
    int first = 1;
    for (unsigned i = 0; i < outline->count; i++) {
        const pdf_cff_segment *segment = outline->segments + i;
        unsigned n = segment->kind == PDF_CFF_CURVE ? 3 : 1;
        outline_point p[3];
        for (unsigned j = 0; j < n; j++) {
            double x = segment->points[j * 2], y = segment->points[j * 2 + 1];
            double tx = m[0] * x + m[2] * y + m[4], ty = m[1] * x + m[3] * y + m[5];
            if (!isfinite(tx) || !isfinite(ty) || fabs(tx) > 1e30 || fabs(ty) > 1e30)
                return 0;
            p[j] = (outline_point){(float)tx, (float)ty};
        }
        if (segment->kind == PDF_CFF_MOVE) {
            current = p[0];
            first = 1;
            continue;
        }
        /* Charstring position changes are already reflected in later absolute endpoints. */
        if (segment->kind == PDF_CFF_SETPOINT)
            continue;
        if (segment->kind == PDF_CFF_CURVE) {
            edges[(*count)++] = (outline_edge){current, p[0], p[2], 2, p[1]};
            if (path && !outline_cubic(path, current, p[0], p[1], p[2], first, 0, flatness_squared))
                return 0;
            current = p[2];
        } else {
            edges[(*count)++] = (outline_edge){current, current, p[0], 0, {0, 0}};
            if (path && segment->kind != PDF_CFF_END && !outline_line(path, current, p[0], first))
                return 0;
            current = p[0];
            if (path && segment->kind == PDF_CFF_CLOSE)
                path->segs[path->seg_count - 1].closes_subpath = 1;
        }
        first = 0;
    }
    return 1;
}

static outline_point outline_tt_transform(const pdf_tt_point *point, const double matrix[6]) {
    return (outline_point){(float)(matrix[0] * point->x + matrix[2] * point->y + matrix[4]),
                           (float)(matrix[1] * point->x + matrix[3] * point->y + matrix[5])};
}

static int outline_path(const pdf_tt_outline *outline, pdf_path *path, outline_edge *edges,
                        unsigned *edge_count, const double m[6], double flatness_squared) {
    for (unsigned i = 0; i < outline->count; i++) {
        double x = outline->points[i].x, y = outline->points[i].y;
        double tx = m[0] * x + m[2] * y + m[4], ty = m[1] * x + m[3] * y + m[5];
        if (!isfinite(tx) || !isfinite(ty) || fabs(tx) > 1e30 || fabs(ty) > 1e30)
            return 0;
    }
    unsigned first = 0;
    while (first < outline->count) {
        unsigned last = first;
        while (last < outline->count && !outline->points[last].end)
            last++;
        if (last == outline->count)
            return 0;
        const pdf_tt_point *points = outline->points;
        outline_point a = outline_tt_transform(points + first, m),
                      z = outline_tt_transform(points + last, m);
        outline_point start = points[first].on  ? a
                              : points[last].on ? z
                                                : outline_mid(z, a),
                      current = start;
        unsigned index = first + (points[first].on ? 1 : 0),
                 remaining = last - first + 1 - (points[first].on || points[last].on ? 1 : 0);
        int new_path = 1;
        while (remaining) {
            outline_point p = outline_tt_transform(points + index, m);
            if (points[index].on) {
                edges[(*edge_count)++] = (outline_edge){current, current, p, 0, {0, 0}};
                if (path && !outline_line(path, current, p, new_path))
                    return 0;
                current = p;
                index++;
                remaining--;
            } else {
                outline_point next =
                    remaining > 1 ? outline_tt_transform(points + index + 1, m) : start;
                int on = remaining == 1 || points[index + 1].on;
                outline_point end = on ? next : outline_mid(p, next);
                edges[(*edge_count)++] = (outline_edge){current, p, end, 1, {0, 0}};
                if (path && !outline_quad(path, current, p, end, new_path, 0, flatness_squared))
                    return 0;
                current = end;
                unsigned consumed = on && remaining > 1 ? 2 : 1;
                index += consumed;
                remaining -= consumed;
            }
            new_path = 0;
        }
        edges[(*edge_count)++] = (outline_edge){current, current, start, 0, {0, 0}};
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

static double outline_bezier3(double a, double c1, double c2, double b, double t) {
    return outline_bezier(a + (c1 - a) * t, c1 + (c2 - c1) * t, c2 + (b - c2) * t, t);
}

static int outline_cubic_winding(const outline_edge *e, double x, double y) {
    double a = -e->a.y + 3 * e->control.y - 3 * e->control2.y + e->b.y,
           b = 2 * (e->a.y - 2 * e->control.y + e->control2.y), c = e->control.y - e->a.y;
    double roots[2], cuts[4] = {0, 1, 1, 1};
    unsigned n = 0, intervals = 1;
    if (a == 0) {
        if (b)
            roots[n++] = -c / b;
    } else {
        double d = b * b - 4 * a * c;
        if (d >= 0) {
            double q = -.5 * (b + copysign(sqrt(d), b));
            if (q) {
                roots[n++] = q / a;
                roots[n++] = c / q;
            } else
                roots[n++] = -b / (2 * a);
        }
    }
    if (n == 2 && roots[0] > roots[1]) {
        double t = roots[0];
        roots[0] = roots[1];
        roots[1] = t;
    }
    for (unsigned i = 0; i < n; i++)
        if (roots[i] > 0 && roots[i] < 1 && (intervals == 1 || roots[i] > cuts[intervals - 1]))
            cuts[intervals++] = roots[i];
    cuts[intervals] = 1;
    int winding = 0;
    for (unsigned i = 0; i < intervals; i++) {
        double lo = cuts[i], hi = cuts[i + 1],
               yl = outline_bezier3(e->a.y, e->control.y, e->control2.y, e->b.y, lo),
               yh = outline_bezier3(e->a.y, e->control.y, e->control2.y, e->b.y, hi);
        if (!((yl <= y && yh > y) || (yh <= y && yl > y)))
            continue;
        for (unsigned step = 0; step < 48; step++) {
            double mid = (lo + hi) * .5,
                   value = outline_bezier3(e->a.y, e->control.y, e->control2.y, e->b.y, mid);
            if ((value < y) == (yh > yl))
                lo = mid;
            else
                hi = mid;
        }
        if (outline_bezier3(e->a.x, e->control.x, e->control2.x, e->b.x, (lo + hi) * .5) > x)
            winding += yh > yl ? 1 : -1;
    }
    return winding;
}

/* Intersect the original quadratic, not its display-scale stroke tessellation.
   Splitting at the Y extremum gives the same half-open edge rule as lines. */
static int outline_winding(const outline_edge *edges, unsigned count, double x, double y) {
    int winding = 0;
    for (unsigned i = 0; i < count; i++) {
        const outline_edge *e = edges + i;
        if (e->quadratic == 2) {
            winding += outline_cubic_winding(e, x, y);
            continue;
        }
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

int cupidimage_pdf_draw_font_program_glyph(pdf_render_state *st, pdf_font_program *program,
                                           unsigned glyph) {
    program = cupidimage_pdf_font_program_select(program, &glyph);
    int zero_text_transform = st->gs.font_size_pts == 0 || st->gs.horizontal_scale == 0;
    if (st->gs.text_mode == 3 || (zero_text_transform && st->gs.text_mode < 4))
        return 1;
    if (!st->form_budget->glyph_calls_left)
        return 0;
    st->form_budget->glyph_calls_left--;
    pdf_tt_outline outline = {0}, device_outline = {0};
    pdf_tt_hint_shape hinted = {0};
    pdf_cff_outline cff_outline = {0}, hinted_cff = {0};
    const pdf_cff_outline *draw_cff = &cff_outline;
    pdf_path *path = NULL;
    outline_edge *edges = NULL;
    unsigned edge_count = 0;
    int mode = st->gs.text_mode & 3, clipping = st->gs.text_mode >= 4;
    unsigned char *stroke = NULL, *fill = NULL;
    int ok = 0, borrowed = 0;
    pdf_paint_group combined = {0};
    if (!cupidimage_pdf_font_outline(program, glyph, &outline, &cff_outline,
                                     &st->form_budget->outline_cache_bytes_left, &borrowed))
        goto done;
    unsigned count = (program->is_cff || program->is_type1) ? cff_outline.count : outline.count;
    if (!count) {
        ok = 1;
        goto done;
    }
    const double *t = st->text_matrix;
    const float *c = st->gs.ctm;
    double ratio = (double)(96.0f / 72.0f),
           scale = st->gs.font_size_pts /
                   ((program->is_cff || program->is_type1) ? 1 : program->font.units);
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
    const pdf_tt_outline *draw_outline = &outline;
    double draw_matrix[6];
    memcpy(draw_matrix, m, sizeof(m));
    if (st->truetype_hinting && !zero_text_transform && !program->is_cff && !program->is_type1) {
        if (!cupidimage_pdf_page_hint_shape(&st->form_budget->hint_cache, program, glyph, m,
                                            st->text_antialias, &st->clip_budget->tests_left,
                                            &hinted)) {
            cupidimage_set_err(st->error, st->error_capacity,
                               "pdf: unable to grid-fit TrueType glyph");
            goto done;
        }
        device_outline.points = hinted.points;
        device_outline.count = hinted.count;
        draw_outline = &device_outline;
        count = hinted.count;
        memcpy(draw_matrix, (double[6]){1, 0, 0, 1, 0, 0}, sizeof(draw_matrix));
    }
    if (program->is_cff || program->is_type1) {
        unsigned fd =
            !program->is_type1 && program->cff.fdselect ? program->cff.fdselect[glyph] : 0;
        const double *f =
            program->is_type1 ? program->type1.matrix : program->cff.private_dicts[fd].matrix;
        double a[6];
        memcpy(a, m, sizeof(a));
        m[0] = a[0] * f[0] + a[2] * f[1];
        m[1] = a[1] * f[0] + a[3] * f[1];
        m[2] = a[0] * f[2] + a[2] * f[3];
        m[3] = a[1] * f[2] + a[3] * f[3];
        m[4] = a[0] * f[4] + a[2] * f[5] + a[4];
        m[5] = a[1] * f[4] + a[3] * f[5] + a[5];
        if (((st->cff_hinting && program->is_cff) || (st->type1_hinting && program->is_type1)) &&
            !zero_text_transform) {
            int fitted = 0;
            int shaped = program->is_type1
                             ? cupidimage_pdf_type1_page_shape(&program->type1, glyph, draw_matrix,
                                                               &st->clip_budget->tests_left,
                                                               &hinted_cff, &fitted)
                             : cupidimage_pdf_cff_page_shape(&program->cff, glyph, draw_matrix,
                                                             &st->clip_budget->tests_left,
                                                             &hinted_cff, &fitted);
            if (!shaped) {
                cupidimage_set_err(st->error, st->error_capacity,
                                   program->is_type1 ? "pdf: unable to grid-fit Type 1 glyph"
                                                     : "pdf: unable to grid-fit CFF glyph");
                goto done;
            }
            if (fitted) {
                draw_cff = &hinted_cff;
                count = hinted_cff.count;
                memcpy(m, (double[6]){1, 0, 0, 1, 0, 0}, sizeof(m));
            }
        }
    }
    if (!count) {
        ok = !clipping || cupidimage_pdf_text_clip_prepare(st);
        goto done;
    }
    edges = malloc((size_t)count * 2 * sizeof(*edges));
    if (!edges)
        goto done;
    if ((mode == 1 || mode == 2) &&
        !cupidimage_pdf_empty_color(&st->gs.stroke_space, st->gs.stroke_pattern)) {
        path = calloc(1, sizeof(*path));
        if (!path)
            goto done;
    }
    double flatness = cupidimage_pdf_flatness(&st->gs, .125);
    double flatness_squared = flatness * flatness;
    if (program->is_cff || program->is_type1) {
        if (!outline_cff_path(draw_cff, path, edges, &edge_count, m, flatness_squared))
            goto done;
    } else if (!outline_path(draw_outline, path, edges, &edge_count, draw_matrix, flatness_squared))
        goto done;
    if (!edge_count) {
        ok = 1;
        goto done;
    }
    if (clipping && !cupidimage_pdf_text_clip_prepare(st))
        goto done;
    if (zero_text_transform) {
        ok = 1;
        goto done;
    }
    double x0 = st->img->width, y0 = st->img->height, x1 = 0, y1 = 0;
    for (unsigned i = 0; i < edge_count; i++) {
        const outline_edge *e = edges + i;
        outline_point points[4] = {e->a, e->b, e->control, e->control2};
        unsigned n = e->quadratic == 2 ? 4 : e->quadratic == 1 ? 3 : 2;
        for (unsigned j = 0; j < n; j++) {
            x0 = fmin(x0, points[j].x);
            x1 = fmax(x1, points[j].x);
            y0 = fmin(y0, points[j].y);
            y1 = fmax(y1, points[j].y);
        }
    }
    if ((mode == 1 || mode == 2) &&
        !cupidimage_pdf_empty_color(&st->gs.stroke_space, st->gs.stroke_pattern)) {
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
    int need_winding = clipping || !st->text_antialias;
    if (need_winding) {
        if (edge_count && pixels > st->clip_budget->tests_left / edge_count)
            goto done;
        st->clip_budget->tests_left -= pixels * edge_count;
    }
    if (st->text_antialias && pixels && (mode == 0 || mode == 2) &&
        !cupidimage_pdf_empty_color(&st->gs.fill_space, st->gs.fill_pattern) &&
        !cupidimage_pdf_font_coverage(program->is_cff || program->is_type1 ? NULL : draw_outline,
                                      program->is_cff || program->is_type1 ? draw_cff : NULL,
                                      program->is_cff || program->is_type1 ? m : draw_matrix, left,
                                      top, right, bottom, &st->clip_budget->tests_left, &fill))
        goto done;
    if (pixels && mode != 3 && !cupidimage_pdf_text_group_begin(st))
        goto done;
    if (pixels && mode == 2 && !cupidimage_pdf_fill_stroke_begin(st, &combined))
        goto done;
    for (int y = top; y < bottom && !st->image_failed; y++)
        for (int x = left; x < right && !st->image_failed; x++) {
            int winding = need_winding ? outline_winding(edges, edge_count, x + .5, y + .5) : 0;
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
            int stroked = stroke && (stroke[pixel / 8] & bit);
            unsigned coverage =
                st->text_antialias
                    ? (fill ? fill[(size_t)(bottom - y - 1) * (right - left) + x - left] : 0)
                    : ((mode == 0 || mode == 2) && winding ? 255 : 0);
            if (coverage && (!stroked || combined.surface.image.rgba))
                cupidimage_pdf_paint_pixel(st, x, y, 0, coverage / 255.0);
            if (stroked)
                cupidimage_pdf_paint_pixel(st, x, y, 1, 1);
        }
    ok = !st->image_failed;
done:
    if (!cupidimage_pdf_fill_stroke_end(st, &combined, ok))
        ok = 0;
    cupidimage_pdf_tt_hint_shape_free(&hinted);
    cupidimage_pdf_type2_free(&hinted_cff);
    free(fill);
    free(stroke);
    free(path);
    free(edges);
    if (!borrowed) {
        cupidimage_pdf_tt_outline_free(&outline);
        cupidimage_pdf_type2_free(&cff_outline);
    }
    return ok;
}
