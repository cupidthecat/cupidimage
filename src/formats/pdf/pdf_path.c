#include "core/internal.h"
#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void cupidimage_pdf_path_init(pdf_path *p) {
    if (!p)
        return;
    p->extra = NULL;
    p->capacity = 0;
    p->seg_count = 0;
    p->subpath_first_seg = 0;
    p->has_current = 0;
    p->has_subpath = 0;
    p->overflow = 0;
    p->current_x = 0.0f;
    p->current_y = 0.0f;
    p->subpath_start_x = 0.0f;
    p->subpath_start_y = 0.0f;
}

void cupidimage_pdf_path_clear(pdf_path *p) {
    if (!p)
        return;
    free(p->extra);
    cupidimage_pdf_path_init(p);
}

int cupidimage_pdf_path_reserve(pdf_path *p, unsigned additional) {
    if (!cupidimage_pdf_path_valid(p) ||
        additional > PDF_PATH_SEGMENT_LIMIT - (unsigned)p->seg_count) {
        if (p)
            p->overflow = 1;
        return 0;
    }
    unsigned needed = (unsigned)p->seg_count + additional;
    unsigned capacity = p->extra ? p->capacity : PDF_PATH_INLINE_SEGMENTS;
    if (needed <= capacity)
        return 1;
    while (capacity < needed)
        capacity = capacity > PDF_PATH_SEGMENT_LIMIT / 2 ? PDF_PATH_SEGMENT_LIMIT : capacity * 2;
    pdf_seg *segments = p->extra ? realloc(p->extra, (size_t)capacity * sizeof(*segments))
                                 : malloc((size_t)capacity * sizeof(*segments));
    if (!segments) {
        p->overflow = 1;
        return 0;
    }
    if (!p->extra)
        memcpy(segments, p->segs, (size_t)p->seg_count * sizeof(*segments));
    p->extra = segments;
    p->capacity = capacity;
    return 1;
}

static void pdf_path_add_seg(pdf_path *p, float x0, float y0, float x1, float y1) {
    if (!p)
        return;
    if (!cupidimage_pdf_path_reserve(p, 1))
        return;
    pdf_seg *segments = cupidimage_pdf_path_mutable(p);
    segments[p->seg_count] = (pdf_seg){0};
    segments[p->seg_count].x0 = x0;
    segments[p->seg_count].y0 = y0;
    segments[p->seg_count].x1 = x1;
    segments[p->seg_count].y1 = y1;
    segments[p->seg_count].starts_subpath = p->seg_count == p->subpath_first_seg;
    segments[p->seg_count].closes_subpath = 0;
    p->seg_count++;
}

static void pdf_path_move_to(pdf_path *p, float x, float y) {
    if (!p)
        return;
    p->subpath_first_seg = p->seg_count;
    p->current_x = x;
    p->current_y = y;
    p->subpath_start_x = x;
    p->subpath_start_y = y;
    p->has_current = 1;
    p->has_subpath = 1;
}

static void pdf_path_line_to(pdf_path *p, float x, float y) {
    if (!p)
        return;
    if (!p->has_current) {
        pdf_path_move_to(p, x, y);
        return;
    }
    pdf_path_add_seg(p, p->current_x, p->current_y, x, y);
    p->current_x = x;
    p->current_y = y;
}

void cupidimage_pdf_path_close(pdf_path *p) {
    if (!p || !p->has_current || !p->has_subpath)
        return;
    /* A short closing edge still determines the join direction. A move followed
     * by closepath is a closed point, which round caps can paint. */
    if (p->seg_count == p->subpath_first_seg || p->current_x != p->subpath_start_x ||
        p->current_y != p->subpath_start_y) {
        pdf_path_add_seg(p, p->current_x, p->current_y, p->subpath_start_x, p->subpath_start_y);
    }
    if (p->seg_count > p->subpath_first_seg)
        cupidimage_pdf_path_mutable(p)[p->seg_count - 1].closes_subpath = 1;
    p->current_x = p->subpath_start_x;
    p->current_y = p->subpath_start_y;
}

/* Paths retain device coordinates when cm or q/Q changes the graphics state. */
void cupidimage_pdf_path_move_user(pdf_render_state *st, float x, float y) {
    float px, py;
    cupidimage_pdf_user_to_pixel(st, x, y, &px, &py);
    pdf_path_move_to(&st->path, px, py);
}

void cupidimage_pdf_path_line_user(pdf_render_state *st, float x, float y) {
    float px, py;
    cupidimage_pdf_user_to_pixel(st, x, y, &px, &py);
    pdf_path_line_to(&st->path, px, py);
}

static double pdf_point_to_segment_dist_sq(double px, double py, double x0, double y0, double x1,
                                           double y1) {
    double vx = x1 - x0;
    double vy = y1 - y0;
    double wx = px - x0;
    double wy = py - y0;
    double c1 = vx * wx + vy * wy;
    if (c1 <= 0.0f) {
        double dx = px - x0;
        double dy = py - y0;
        return dx * dx + dy * dy;
    }
    double c2 = vx * vx + vy * vy;
    if (c2 == 0) {
        double dx = px - x0;
        double dy = py - y0;
        return dx * dx + dy * dy;
    }
    if (c1 >= c2) {
        double dx = px - x1;
        double dy = py - y1;
        return dx * dx + dy * dy;
    }
    double t = c1 / c2;
    double qx = x0 + t * vx;
    double qy = y0 + t * vy;
    double dx = px - qx;
    double dy = py - qy;
    return dx * dx + dy * dy;
}

static void pdf_flatten_curve(pdf_render_state *st, float x0, float y0, float x1, float y1,
                              float x2, float y2, float x3, float y3, unsigned depth) {
    if (st->path.overflow)
        return;
    if ((unsigned)st->path.seg_count >= PDF_PATH_SEGMENT_LIMIT) {
        st->path.overflow = 1;
        return;
    }
    double tolerance = cupidimage_pdf_flatness(&st->gs, .25);
    double squared = tolerance * tolerance;
    if (pdf_point_to_segment_dist_sq(x1, y1, x0, y0, x3, y3) <= squared &&
        pdf_point_to_segment_dist_sq(x2, y2, x0, y0, x3, y3) <= squared) {
        pdf_path_line_to(&st->path, x3, y3);
        return;
    }
    if (depth == 16) {
        st->path.overflow = 1;
        return;
    }
    float ax = x0 * 0.5f + x1 * 0.5f, ay = y0 * 0.5f + y1 * 0.5f;
    float bx = x1 * 0.5f + x2 * 0.5f, by = y1 * 0.5f + y2 * 0.5f;
    float cx = x2 * 0.5f + x3 * 0.5f, cy = y2 * 0.5f + y3 * 0.5f;
    float dx = ax * 0.5f + bx * 0.5f, dy = ay * 0.5f + by * 0.5f;
    float ex = bx * 0.5f + cx * 0.5f, ey = by * 0.5f + cy * 0.5f;
    float mx = dx * 0.5f + ex * 0.5f, my = dy * 0.5f + ey * 0.5f;
    pdf_flatten_curve(st, x0, y0, ax, ay, dx, dy, mx, my, depth + 1);
    pdf_flatten_curve(st, mx, my, ex, ey, cx, cy, x3, y3, depth + 1);
}

void cupidimage_pdf_path_curve_to(pdf_render_state *st, float x1, float y1, float x2, float y2,
                                  float x3, float y3, int first_is_current) {
    if (!st->path.has_current)
        return;
    if (first_is_current) {
        x1 = st->path.current_x;
        y1 = st->path.current_y;
    } else {
        cupidimage_pdf_user_to_pixel(st, x1, y1, &x1, &y1);
    }
    cupidimage_pdf_user_to_pixel(st, x2, y2, &x2, &y2);
    cupidimage_pdf_user_to_pixel(st, x3, y3, &x3, &y3);
    if (!isfinite(x1) || !isfinite(y1) || !isfinite(x2) || !isfinite(y2) || !isfinite(x3) ||
        !isfinite(y3)) {
        st->path.overflow = 1;
        return;
    }
    int index = st->path.seg_count;
    pdf_path_add_seg(&st->path, st->path.current_x, st->path.current_y, x3, y3);
    if (st->path.seg_count == index)
        return;
    pdf_seg *segment = cupidimage_pdf_path_mutable(&st->path) + index;
    segment->curve = 1;
    segment->cx1 = x1;
    segment->cy1 = y1;
    segment->cx2 = x2;
    segment->cy2 = y2;
    st->path.current_x = x3;
    st->path.current_y = y3;
}

int cupidimage_pdf_path_flatten(pdf_render_state *st) {
    if (!cupidimage_pdf_path_valid(&st->path))
        return 0;
    int curves = 0;
    for (int i = 0; i < st->path.seg_count; i++)
        curves |= cupidimage_pdf_path_segments(&st->path)[i].curve;
    if (!curves)
        return 1;
    pdf_path *source = malloc(sizeof(*source));
    if (!source)
        return 0;
    *source = st->path;
    /* Transfer any extended storage to the retained source before rebuilding. */
    cupidimage_pdf_path_init(&st->path);
    for (int i = 0; i < source->seg_count && !st->path.overflow; i++) {
        const pdf_seg *segment = cupidimage_pdf_path_segments(source) + i;
        if (segment->starts_subpath)
            pdf_path_move_to(&st->path, segment->x0, segment->y0);
        if (segment->curve)
            pdf_flatten_curve(st, segment->x0, segment->y0, segment->cx1, segment->cy1,
                              segment->cx2, segment->cy2, segment->x1, segment->y1, 0);
        else
            pdf_path_line_to(&st->path, segment->x1, segment->y1);
        if (segment->closes_subpath && st->path.seg_count)
            cupidimage_pdf_path_mutable(&st->path)[st->path.seg_count - 1].closes_subpath = 1;
    }
    /* Preserve the current point and subpath origin for the closing paint operators. */
    st->path.current_x = source->current_x;
    st->path.current_y = source->current_y;
    st->path.subpath_start_x = source->subpath_start_x;
    st->path.subpath_start_y = source->subpath_start_y;
    if (source->subpath_first_seg == source->seg_count)
        st->path.subpath_first_seg = st->path.seg_count;
    st->path.has_current = source->has_current;
    st->path.has_subpath = source->has_subpath;
    cupidimage_pdf_path_clear(source);
    free(source);
    return !st->path.overflow;
}

/* Filling closes subpaths without changing their stroke caps or joins. */
int cupidimage_pdf_path_fill_edges(const pdf_path *path, size_t *work, pdf_region_edge **output,
                                   unsigned *count) {
    if (!output || !count)
        return 0;
    *output = NULL;
    *count = 0;
    if (!work || !cupidimage_pdf_path_valid(path))
        return 0;
    unsigned max = (unsigned)path->seg_count * 4, used = 0;
    if (max > *work)
        return 0;
    *work -= max;
    pdf_region_edge *edges = malloc((max ? max : 1) * sizeof(*edges));
    if (!edges)
        return 0;
    double sx = 0, sy = 0;
    unsigned first = 0;
    int point = 0;
    for (int i = 0; i < path->seg_count; i++) {
        const pdf_seg *s = cupidimage_pdf_path_segments(path) + i;
        if (s->curve || !isfinite(s->x0) || !isfinite(s->y0) || !isfinite(s->x1) ||
            !isfinite(s->y1)) {
            free(edges);
            return 0;
        }
        if (!i || s->starts_subpath) {
            sx = s->x0;
            sy = s->y0;
            first = used;
            point = 1;
        }
        point = point && s->x0 == sx && s->y0 == sy && s->x1 == sx && s->y1 == sy;
        edges[used++] = (pdf_region_edge){s->x0, s->y0, s->x1, s->y1, 0};
        if (i + 1 == path->seg_count || cupidimage_pdf_path_segments(path)[i + 1].starts_subpath) {
            if (point) {
                /* ISO 32000-2 8.5.3.3.1: a degenerate filled subpath encloses
                 * the device pixel under the point. A trailing move has no edge. */
                double x = floor(sx), y = floor(sy);
                used = first;
                edges[used++] = (pdf_region_edge){x, y, x + 1, y, 0};
                edges[used++] = (pdf_region_edge){x + 1, y, x + 1, y + 1, 0};
                edges[used++] = (pdf_region_edge){x + 1, y + 1, x, y + 1, 0};
                edges[used++] = (pdf_region_edge){x, y + 1, x, y, 0};
            } else {
                edges[used++] = (pdf_region_edge){s->x1, s->y1, sx, sy, 0};
            }
        }
    }
    *output = edges;
    *count = used;
    return 1;
}
