#include "core/internal.h"
#include "cupidimage.h"
#include "formats/svg/cupidimage_svg_base.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wshadow"
#endif

#include "svg_internal.h"

static void svg_segments_add_arc(svg_segments *segs, float cx, float cy, float rx, float ry,
                                 float start_ang, float end_ang, int steps);
static void svg_segments_add_arc_rot(svg_segments *segs, float x0, float y0, float x1, float y1,
                                     float rx, float ry, float x_axis_rotation, int large_arc,
                                     int sweep);
static float svg_point_segment_distance(float px, float py, float x0, float y0, float x1, float y1);
static float svg_point_segment_distance_cap(float px, float py, float x0, float y0, float x1,
                                            float y1, float half, int start_cap, int end_cap);
static int svg_point_in_triangle(float px, float py, const svg_join_tri *tri);
static int svg_join_tri_add(svg_join_tri **tris, int *count, int *cap, float ax, float ay, float bx,
                            float by, float cx, float cy);
static int svg_flatten_cubic(svg_segments *segs, float x0, float y0, float x1, float y1, float x2,
                             float y2, float x3, float y3, float tol, int depth);
static int svg_flatten_quad(svg_segments *segs, float x0, float y0, float x1, float y1, float x2,
                            float y2, float tol, int depth);

void cupidimage_svg_bbox_init(svg_bbox *bbox) {
    if (!bbox) {
        return;
    }
    bbox->x = 0.0f;
    bbox->y = 0.0f;
    bbox->w = -1.0f;
    bbox->h = -1.0f;
}

void cupidimage_svg_bbox_add_rect(svg_bbox *bbox, float x, float y, float w, float h) {
    if (!bbox || w < 0.0f || h < 0.0f) {
        return;
    }
    if (bbox->w < 0.0f || bbox->h < 0.0f) {
        bbox->x = x;
        bbox->y = y;
        bbox->w = w;
        bbox->h = h;
        return;
    }
    float minx = fminf(bbox->x, x);
    float miny = fminf(bbox->y, y);
    float maxx = fmaxf(bbox->x + bbox->w, x + w);
    float maxy = fmaxf(bbox->y + bbox->h, y + h);
    bbox->x = minx;
    bbox->y = miny;
    bbox->w = maxx - minx;
    bbox->h = maxy - miny;
}

int cupidimage_svg_bbox_valid(const svg_bbox *bbox) {
    return bbox && bbox->w > 0.0f && bbox->h > 0.0f;
}

void cupidimage_svg_bbox_expand(svg_bbox *bbox, float amount) {
    if (!bbox || amount <= 0.0f) {
        return;
    }
    if (bbox->w < 0.0f || bbox->h < 0.0f) {
        return;
    }
    bbox->x -= amount;
    bbox->y -= amount;
    bbox->w += amount * 2.0f;
    bbox->h += amount * 2.0f;
}

int cupidimage_svg_segments_add(svg_segments *segs, float x0, float y0, float x1, float y1,
                                int to_stroke, int to_fill) {
    if (to_stroke) {
        if (segs->stroke_count >= segs->stroke_cap) {
            size_t new_cap = segs->stroke_cap ? segs->stroke_cap * 2 : 32;
            svg_segment *n = (svg_segment *)realloc(segs->stroke, new_cap * sizeof(svg_segment));
            if (!n) {
                return 0;
            }
            segs->stroke = n;
            segs->stroke_cap = new_cap;
        }
        segs->stroke[segs->stroke_count++] = (svg_segment){x0, y0, x1, y1};
    }
    if (to_fill) {
        if (segs->fill_count >= segs->fill_cap) {
            size_t new_cap = segs->fill_cap ? segs->fill_cap * 2 : 32;
            svg_segment *n = (svg_segment *)realloc(segs->fill, new_cap * sizeof(svg_segment));
            if (!n) {
                return 0;
            }
            segs->fill = n;
            segs->fill_cap = new_cap;
        }
        segs->fill[segs->fill_count++] = (svg_segment){x0, y0, x1, y1};
    }
    if (x0 < segs->minx)
        segs->minx = x0;
    if (y0 < segs->miny)
        segs->miny = y0;
    if (x0 > segs->maxx)
        segs->maxx = x0;
    if (y0 > segs->maxy)
        segs->maxy = y0;
    if (x1 < segs->minx)
        segs->minx = x1;
    if (y1 < segs->miny)
        segs->miny = y1;
    if (x1 > segs->maxx)
        segs->maxx = x1;
    if (y1 > segs->maxy)
        segs->maxy = y1;
    return 1;
}

void cupidimage_svg_segments_init(svg_segments *segs) {
    segs->stroke = NULL;
    segs->stroke_count = 0;
    segs->stroke_cap = 0;
    segs->fill = NULL;
    segs->fill_count = 0;
    segs->fill_cap = 0;
    segs->minx = 1e30f;
    segs->miny = 1e30f;
    segs->maxx = -1e30f;
    segs->maxy = -1e30f;
}

void cupidimage_svg_segments_free(svg_segments *segs) {
    free(segs->stroke);
    free(segs->fill);
    segs->stroke = NULL;
    segs->fill = NULL;
    segs->stroke_count = 0;
    segs->fill_count = 0;
    segs->stroke_cap = 0;
    segs->fill_cap = 0;
}

static void svg_segments_add_arc(svg_segments *segs, float cx, float cy, float rx, float ry,
                                 float start_ang, float end_ang, int steps) {
    if (!segs || steps < 1) {
        return;
    }
    float prevx = cx + cosf(start_ang) * rx;
    float prevy = cy + sinf(start_ang) * ry;
    for (int i = 1; i <= steps; i++) {
        float t = start_ang + (end_ang - start_ang) * ((float)i / (float)steps);
        float nx = cx + cosf(t) * rx;
        float ny = cy + sinf(t) * ry;
        cupidimage_svg_segments_add(segs, prevx, prevy, nx, ny, 1, 0);
        prevx = nx;
        prevy = ny;
    }
}

void cupidimage_svg_segments_add_rect(svg_segments *segs, float x, float y, float w, float h,
                                      float rx, float ry) {
    if (!segs || w <= 0.0f || h <= 0.0f) {
        return;
    }
    if (rx < 0.0f)
        rx = 0.0f;
    if (ry < 0.0f)
        ry = 0.0f;
    if (rx > w * 0.5f)
        rx = w * 0.5f;
    if (ry > h * 0.5f)
        ry = h * 0.5f;
    if (rx <= 0.0f || ry <= 0.0f) {
        cupidimage_svg_segments_add(segs, x, y, x + w, y, 1, 0);
        cupidimage_svg_segments_add(segs, x + w, y, x + w, y + h, 1, 0);
        cupidimage_svg_segments_add(segs, x + w, y + h, x, y + h, 1, 0);
        cupidimage_svg_segments_add(segs, x, y + h, x, y, 1, 0);
        return;
    }
    int steps = (int)lroundf(fmaxf(rx, ry) * 0.5f);
    if (steps < 4)
        steps = 4;
    if (steps > 24)
        steps = 24;
    float left = x + rx;
    float right = x + w - rx;
    float top = y + ry;
    float bottom = y + h - ry;
    cupidimage_svg_segments_add(segs, left, y, right, y, 1, 0);
    svg_segments_add_arc(segs, right, top, rx, ry, -0.5f * (float)M_PI, 0.0f, steps);
    cupidimage_svg_segments_add(segs, x + w, top, x + w, bottom, 1, 0);
    svg_segments_add_arc(segs, right, bottom, rx, ry, 0.0f, 0.5f * (float)M_PI, steps);
    cupidimage_svg_segments_add(segs, right, y + h, left, y + h, 1, 0);
    svg_segments_add_arc(segs, left, bottom, rx, ry, 0.5f * (float)M_PI, (float)M_PI, steps);
    cupidimage_svg_segments_add(segs, x, bottom, x, top, 1, 0);
    svg_segments_add_arc(segs, left, top, rx, ry, (float)M_PI, 1.5f * (float)M_PI, steps);
}

void cupidimage_svg_segments_add_ellipse(svg_segments *segs, float cx, float cy, float rx,
                                         float ry) {
    if (!segs || rx <= 0.0f || ry <= 0.0f) {
        return;
    }
    int steps = (int)lroundf(fmaxf(rx, ry) * 0.75f);
    if (steps < 16)
        steps = 16;
    if (steps > 128)
        steps = 128;
    float prevx = cx + rx;
    float prevy = cy;
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / (float)steps * 2.0f * (float)M_PI;
        float nx = cx + cosf(t) * rx;
        float ny = cy + sinf(t) * ry;
        cupidimage_svg_segments_add(segs, prevx, prevy, nx, ny, 1, 0);
        prevx = nx;
        prevy = ny;
    }
}

static void svg_segments_add_arc_rot(svg_segments *segs, float x0, float y0, float x1, float y1,
                                     float rx, float ry, float x_axis_rotation, int large_arc,
                                     int sweep) {
    if (!segs) {
        return;
    }
    if (rx <= 0.0f || ry <= 0.0f) {
        cupidimage_svg_segments_add(segs, x0, y0, x1, y1, 1, 1);
        return;
    }
    float phi = x_axis_rotation * (float)M_PI / 180.0f;
    float cosphi = cosf(phi);
    float sinphi = sinf(phi);
    float dx = (x0 - x1) * 0.5f;
    float dy = (y0 - y1) * 0.5f;
    float x1p = cosphi * dx + sinphi * dy;
    float y1p = -sinphi * dx + cosphi * dy;
    rx = fabsf(rx);
    ry = fabsf(ry);
    float rx2 = rx * rx;
    float ry2 = ry * ry;
    float x1p2 = x1p * x1p;
    float y1p2 = y1p * y1p;
    float lambda = x1p2 / rx2 + y1p2 / ry2;
    if (lambda > 1.0f) {
        float scale = sqrtf(lambda);
        rx *= scale;
        ry *= scale;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }
    float denom = rx2 * y1p2 + ry2 * x1p2;
    if (denom <= 1e-12f) {
        cupidimage_svg_segments_add(segs, x0, y0, x1, y1, 1, 1);
        return;
    }
    float sign = (large_arc == sweep) ? -1.0f : 1.0f;
    float numer = rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2;
    if (numer < 0.0f) {
        numer = 0.0f;
    }
    float factor = sign * sqrtf(numer / denom);
    float cxp = factor * (rx * y1p / ry);
    float cyp = factor * (-ry * x1p / rx);
    float cx = cosphi * cxp - sinphi * cyp + (x0 + x1) * 0.5f;
    float cy = sinphi * cxp + cosphi * cyp + (y0 + y1) * 0.5f;
    float ux = (x1p - cxp) / rx;
    float uy = (y1p - cyp) / ry;
    float vx = (-x1p - cxp) / rx;
    float vy = (-y1p - cyp) / ry;
    float start_ang = atan2f(uy, ux);
    float delta = atan2f(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!sweep && delta > 0.0f) {
        delta -= 2.0f * (float)M_PI;
    } else if (sweep && delta < 0.0f) {
        delta += 2.0f * (float)M_PI;
    }
    int segments = (int)ceilf(fabsf(delta) / (float)(M_PI / 8.0f));
    if (segments < 1)
        segments = 1;
    float prevx = x0;
    float prevy = y0;
    for (int i = 1; i <= segments; i++) {
        float ang = start_ang + delta * ((float)i / (float)segments);
        float cosang = cosf(ang);
        float sinang = sinf(ang);
        float px = cx + cosphi * rx * cosang - sinphi * ry * sinang;
        float py = cy + sinphi * rx * cosang + cosphi * ry * sinang;
        cupidimage_svg_segments_add(segs, prevx, prevy, px, py, 1, 1);
        prevx = px;
        prevy = py;
    }
}

static float svg_point_segment_distance(float px, float py, float x0, float y0, float x1,
                                        float y1) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float denom = dx * dx + dy * dy;
    if (denom <= 1e-12f) {
        float ux = px - x0;
        float uy = py - y0;
        return sqrtf(ux * ux + uy * uy);
    }
    float t = ((px - x0) * dx + (py - y0) * dy) / denom;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    float lx = x0 + t * dx;
    float ly = y0 + t * dy;
    float ux = px - lx;
    float uy = py - ly;
    return sqrtf(ux * ux + uy * uy);
}

static float svg_point_segment_distance_cap(float px, float py, float x0, float y0, float x1,
                                            float y1, float half, int start_cap, int end_cap) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    if (len2 <= 1e-12f) {
        float ux = px - x0;
        float uy = py - y0;
        return sqrtf(ux * ux + uy * uy);
    }
    float len = sqrtf(len2);
    float t = ((px - x0) * dx + (py - y0) * dy) / len2;
    float min_t = 0.0f;
    float max_t = 1.0f;
    if (start_cap == SVG_LINECAP_SQUARE) {
        min_t = -half / len;
    }
    if (end_cap == SVG_LINECAP_SQUARE) {
        max_t = 1.0f + half / len;
    }
    if (start_cap == SVG_LINECAP_BUTT && t < 0.0f) {
        return 1e30f;
    }
    if (end_cap == SVG_LINECAP_BUTT && t > 1.0f) {
        return 1e30f;
    }
    if (t < min_t)
        t = min_t;
    if (t > max_t)
        t = max_t;
    float lx = x0 + t * dx;
    float ly = y0 + t * dy;
    float ux = px - lx;
    float uy = py - ly;
    return sqrtf(ux * ux + uy * uy);
}

int cupidimage_svg_point_in_segments(const svg_segments *segs, int evenodd, float px, float py) {
    int winding = 0;
    int crossings = 0;
    for (size_t i = 0; i < segs->fill_count; i++) {
        float x0 = segs->fill[i].x0;
        float y0 = segs->fill[i].y0;
        float x1 = segs->fill[i].x1;
        float y1 = segs->fill[i].y1;
        if (y0 == y1) {
            continue;
        }
        int cond = (y0 <= py && y1 > py) || (y1 <= py && y0 > py);
        if (!cond) {
            continue;
        }
        float xint = x0 + (py - y0) * (x1 - x0) / (y1 - y0);
        if (xint > px) {
            if (evenodd) {
                crossings++;
            } else {
                winding += (y1 > y0) ? 1 : -1;
            }
        }
    }
    if (evenodd) {
        return (crossings & 1) != 0;
    }
    return winding != 0;
}

int cupidimage_svg_point_on_segments(const svg_segments *segs, float px, float py,
                                     float half_width) {
    float min_dist = 1e30f;
    for (size_t i = 0; i < segs->stroke_count; i++) {
        float d = svg_point_segment_distance(px, py, segs->stroke[i].x0, segs->stroke[i].y0,
                                             segs->stroke[i].x1, segs->stroke[i].y1);
        if (d < min_dist) {
            min_dist = d;
        }
    }
    return min_dist <= half_width;
}

void cupidimage_svg_segments_endpoint_flags(const svg_segments *segs, uint8_t *start_open,
                                            uint8_t *end_open) {
    const float eps = 1e-3f;
    for (size_t i = 0; i < segs->stroke_count; i++) {
        int scount = 0;
        int ecount = 0;
        float sx = segs->stroke[i].x0;
        float sy = segs->stroke[i].y0;
        float ex = segs->stroke[i].x1;
        float ey = segs->stroke[i].y1;
        for (size_t j = 0; j < segs->stroke_count; j++) {
            float ax = segs->stroke[j].x0;
            float ay = segs->stroke[j].y0;
            float bx = segs->stroke[j].x1;
            float by = segs->stroke[j].y1;
            if (fabsf(sx - ax) <= eps && fabsf(sy - ay) <= eps)
                scount++;
            else if (fabsf(sx - bx) <= eps && fabsf(sy - by) <= eps)
                scount++;
            if (fabsf(ex - ax) <= eps && fabsf(ey - ay) <= eps)
                ecount++;
            else if (fabsf(ex - bx) <= eps && fabsf(ey - by) <= eps)
                ecount++;
        }
        start_open[i] = (scount <= 1);
        end_open[i] = (ecount <= 1);
    }
}

static int svg_point_in_triangle(float px, float py, const svg_join_tri *tri) {
    float d1 = (px - tri->bx) * (tri->ay - tri->by) - (tri->ax - tri->bx) * (py - tri->by);
    float d2 = (px - tri->cx) * (tri->by - tri->cy) - (tri->bx - tri->cx) * (py - tri->cy);
    float d3 = (px - tri->ax) * (tri->cy - tri->ay) - (tri->cx - tri->ax) * (py - tri->ay);
    int has_neg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    int has_pos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(has_neg && has_pos);
}

static int svg_join_tri_add(svg_join_tri **tris, int *count, int *cap, float ax, float ay, float bx,
                            float by, float cx, float cy) {
    if (!tris || !count || !cap) {
        return 0;
    }
    if (*count >= *cap) {
        int new_cap = *cap ? *cap * 2 : 16;
        svg_join_tri *n = (svg_join_tri *)realloc(*tris, (size_t)new_cap * sizeof(svg_join_tri));
        if (!n) {
            return 0;
        }
        *tris = n;
        *cap = new_cap;
    }
    svg_join_tri *t = &(*tris)[(*count)++];
    t->ax = ax;
    t->ay = ay;
    t->bx = bx;
    t->by = by;
    t->cx = cx;
    t->cy = cy;
    return 1;
}

void cupidimage_svg_build_join_tris(const svg_segments *segs, const svg_style *style,
                                    const uint8_t *start_open, const uint8_t *end_open,
                                    svg_join_tri **out_tris, int *out_count) {
    if (!segs || !style || !out_tris || !out_count) {
        return;
    }
    *out_tris = NULL;
    *out_count = 0;
    if (style->stroke_linejoin == SVG_LINEJOIN_ROUND || segs->stroke_count < 2) {
        return;
    }
    float half = style->stroke_width * 0.5f;
    if (half <= 0.0f) {
        return;
    }
    const float eps = 1e-3f;
    int cap = 0;
    for (size_t i = 0; i < segs->stroke_count; i++) {
        for (int endpoint = 0; endpoint < 2; endpoint++) {
            if (endpoint == 0 && start_open && start_open[i]) {
                continue;
            }
            if (endpoint == 1 && end_open && end_open[i]) {
                continue;
            }
            float jx = endpoint == 0 ? segs->stroke[i].x0 : segs->stroke[i].x1;
            float jy = endpoint == 0 ? segs->stroke[i].y0 : segs->stroke[i].y1;
            int found = 0;
            size_t j_idx = 0;
            int j_endpoint = 0;
            for (size_t j = i + 1; j < segs->stroke_count && !found; j++) {
                float sx = segs->stroke[j].x0;
                float sy = segs->stroke[j].y0;
                float ex = segs->stroke[j].x1;
                float ey = segs->stroke[j].y1;
                if (fabsf(jx - sx) <= eps && fabsf(jy - sy) <= eps) {
                    if (!start_open || !start_open[j]) {
                        found = 1;
                        j_idx = j;
                        j_endpoint = 0;
                    }
                } else if (fabsf(jx - ex) <= eps && fabsf(jy - ey) <= eps) {
                    if (!end_open || !end_open[j]) {
                        found = 1;
                        j_idx = j;
                        j_endpoint = 1;
                    }
                }
            }
            if (!found) {
                continue;
            }
            float d1x = endpoint == 0 ? (segs->stroke[i].x1 - jx) : (segs->stroke[i].x0 - jx);
            float d1y = endpoint == 0 ? (segs->stroke[i].y1 - jy) : (segs->stroke[i].y0 - jy);
            float d2x =
                j_endpoint == 0 ? (segs->stroke[j_idx].x1 - jx) : (segs->stroke[j_idx].x0 - jx);
            float d2y =
                j_endpoint == 0 ? (segs->stroke[j_idx].y1 - jy) : (segs->stroke[j_idx].y0 - jy);
            float len1 = sqrtf(d1x * d1x + d1y * d1y);
            float len2 = sqrtf(d2x * d2x + d2y * d2y);
            if (len1 <= 1e-6f || len2 <= 1e-6f) {
                continue;
            }
            d1x /= len1;
            d1y /= len1;
            d2x /= len2;
            d2y /= len2;
            float cross = d1x * d2y - d1y * d2x;
            if (fabsf(cross) <= 1e-6f) {
                continue;
            }
            float sign = cross >= 0.0f ? 1.0f : -1.0f;
            float n1x = -d1y * sign;
            float n1y = d1x * sign;
            float n2x = -d2y * sign;
            float n2y = d2x * sign;
            float p1x = jx + n1x * half;
            float p1y = jy + n1y * half;
            float p2x = jx + n2x * half;
            float p2y = jy + n2y * half;

            float ax = p1x, ay = p1y;
            float bx = p2x, by = p2y;
            float cx = jx, cy = jy;
            if (style->stroke_linejoin == SVG_LINEJOIN_MITER) {
                float det = d1x * d2y - d1y * d2x;
                if (fabsf(det) > 1e-6f) {
                    float rx = p2x - p1x;
                    float ry = p2y - p1y;
                    float t = (rx * d2y - ry * d2x) / det;
                    float mx = p1x + d1x * t;
                    float my = p1y + d1y * t;
                    float mlen = sqrtf((mx - jx) * (mx - jx) + (my - jy) * (my - jy));
                    if (mlen <= style->stroke_miterlimit * half) {
                        cx = mx;
                        cy = my;
                    }
                }
            }
            svg_join_tri_add(out_tris, out_count, &cap, ax, ay, bx, by, cx, cy);
        }
    }
}

int cupidimage_svg_point_on_segments_cap(const svg_segments *segs, const svg_style *style, float px,
                                         float py, float half_width, const uint8_t *start_open,
                                         const uint8_t *end_open, const svg_join_tri *joins,
                                         int join_count) {
    float min_dist = 1e30f;
    for (size_t i = 0; i < segs->stroke_count; i++) {
        int join_cap = SVG_LINECAP_BUTT;
        if (style->stroke_linejoin == SVG_LINEJOIN_ROUND) {
            join_cap = SVG_LINECAP_ROUND;
        }
        int start_cap = start_open && start_open[i] ? style->stroke_linecap : join_cap;
        int end_cap = end_open && end_open[i] ? style->stroke_linecap : join_cap;
        float d = svg_point_segment_distance_cap(px, py, segs->stroke[i].x0, segs->stroke[i].y0,
                                                 segs->stroke[i].x1, segs->stroke[i].y1, half_width,
                                                 start_cap, end_cap);
        if (d < min_dist) {
            min_dist = d;
        }
    }
    if (min_dist <= half_width) {
        return 1;
    }
    for (int i = 0; i < join_count; i++) {
        if (svg_point_in_triangle(px, py, &joins[i])) {
            return 1;
        }
    }
    return 0;
}

int cupidimage_svg_segments_build_dashed(const svg_segments *src, const svg_style *style,
                                         svg_segments *out) {
    if (!src || !style || !out || src->stroke_count == 0 || style->stroke_dashcount == 0) {
        return 0;
    }
    float dash[SVG_MAX_DASH];
    int count = style->stroke_dashcount;
    if (count > SVG_MAX_DASH) {
        count = SVG_MAX_DASH;
    }
    float pattern_len = 0.0f;
    for (int i = 0; i < count; i++) {
        dash[i] = style->stroke_dasharray[i];
        if (dash[i] < 0.0f) {
            dash[i] = 0.0f;
        }
        pattern_len += dash[i];
    }
    if (pattern_len <= 0.0f) {
        return 0;
    }
    cupidimage_svg_segments_init(out);
    float offset = style->stroke_dashoffset;
    if (pattern_len > 0.0f) {
        offset = fmodf(offset, pattern_len);
        if (offset < 0.0f) {
            offset += pattern_len;
        }
    }
    int idx = 0;
    int draw = 1;
    float remaining = dash[0];
    int guard = 0;
    while (remaining <= 1e-6f && guard < count) {
        idx = (idx + 1) % count;
        draw = !draw;
        remaining = dash[idx];
        guard++;
    }
    if (guard >= count) {
        return 0;
    }
    while (offset > remaining && remaining > 0.0f) {
        offset -= remaining;
        idx = (idx + 1) % count;
        draw = !draw;
        remaining = dash[idx];
        guard = 0;
        while (remaining <= 1e-6f && guard < count) {
            idx = (idx + 1) % count;
            draw = !draw;
            remaining = dash[idx];
            guard++;
        }
        if (guard >= count) {
            return 0;
        }
    }
    remaining -= offset;
    for (size_t i = 0; i < src->stroke_count; i++) {
        float x0 = src->stroke[i].x0;
        float y0 = src->stroke[i].y0;
        float x1 = src->stroke[i].x1;
        float y1 = src->stroke[i].y1;
        float dx = x1 - x0;
        float dy = y1 - y0;
        float seg_len = sqrtf(dx * dx + dy * dy);
        if (seg_len <= 1e-6f) {
            continue;
        }
        float t0 = 0.0f;
        float seg_rem = seg_len;
        while (seg_rem > 1e-6f) {
            float take = remaining < seg_rem ? remaining : seg_rem;
            float t1 = t0 + take / seg_len;
            if (draw && take > 1e-6f) {
                float sx = x0 + dx * t0;
                float sy = y0 + dy * t0;
                float ex = x0 + dx * t1;
                float ey = y0 + dy * t1;
                cupidimage_svg_segments_add(out, sx, sy, ex, ey, 1, 0);
            }
            t0 = t1;
            seg_rem -= take;
            remaining -= take;
            if (remaining <= 1e-6f) {
                idx = (idx + 1) % count;
                draw = !draw;
                remaining = dash[idx];
                guard = 0;
                while (remaining <= 1e-6f && guard < count) {
                    idx = (idx + 1) % count;
                    draw = !draw;
                    remaining = dash[idx];
                    guard++;
                }
                if (guard >= count) {
                    return out->stroke_count > 0;
                }
            }
        }
    }
    return out->stroke_count > 0;
}

void cupidimage_svg_transform_bounds(const svg_matrix *m, float minx, float miny, float maxx,
                                     float maxy, float *out_minx, float *out_miny, float *out_maxx,
                                     float *out_maxy) {
    float x0 = minx, y0 = miny;
    float x1 = maxx, y1 = miny;
    float x2 = maxx, y2 = maxy;
    float x3 = minx, y3 = maxy;
    svg_matrix_transform_point(m, &x0, &y0);
    svg_matrix_transform_point(m, &x1, &y1);
    svg_matrix_transform_point(m, &x2, &y2);
    svg_matrix_transform_point(m, &x3, &y3);
    float mnx = fminf(fminf(x0, x1), fminf(x2, x3));
    float mny = fminf(fminf(y0, y1), fminf(y2, y3));
    float mxx = fmaxf(fmaxf(x0, x1), fmaxf(x2, x3));
    float mxy = fmaxf(fmaxf(y0, y1), fmaxf(y2, y3));
    *out_minx = mnx;
    *out_miny = mny;
    *out_maxx = mxx;
    *out_maxy = mxy;
}

float cupidimage_svg_apply_spread(float t, svg_spread_method spread) {
    if (spread == SVG_SPREAD_REPEAT) {
        t = t - floorf(t);
    } else if (spread == SVG_SPREAD_REFLECT) {
        t = fmodf(t, 2.0f);
        if (t < 0.0f) {
            t += 2.0f;
        }
        if (t > 1.0f) {
            t = 2.0f - t;
        }
    } else {
        if (t < 0.0f)
            t = 0.0f;
        if (t > 1.0f)
            t = 1.0f;
    }
    return t;
}

uint32_t cupidimage_svg_gradient_stop_color(const svg_gradient *g, float t) {
    if (!g || g->stop_count == 0) {
        return 0x00000000u;
    }
    if (t <= g->stops[0].offset) {
        return g->stops[0].color;
    }
    if (t >= g->stops[g->stop_count - 1].offset) {
        return g->stops[g->stop_count - 1].color;
    }
    for (int i = 0; i < g->stop_count - 1; i++) {
        float o0 = g->stops[i].offset;
        float o1 = g->stops[i + 1].offset;
        if (t >= o0 && t <= o1) {
            float span = o1 - o0;
            float u = span > 0.0f ? (t - o0) / span : 0.0f;
            uint32_t c0 = g->stops[i].color;
            uint32_t c1 = g->stops[i + 1].color;
            uint8_t r0 = (uint8_t)(c0 >> 24);
            uint8_t g0 = (uint8_t)(c0 >> 16);
            uint8_t b0 = (uint8_t)(c0 >> 8);
            uint8_t a0 = (uint8_t)(c0 & 0xFFu);
            uint8_t r1 = (uint8_t)(c1 >> 24);
            uint8_t g1 = (uint8_t)(c1 >> 16);
            uint8_t b1 = (uint8_t)(c1 >> 8);
            uint8_t a1 = (uint8_t)(c1 & 0xFFu);
            uint8_t r = (uint8_t)lroundf(r0 + (r1 - r0) * u);
            uint8_t gg = (uint8_t)lroundf(g0 + (g1 - g0) * u);
            uint8_t b = (uint8_t)lroundf(b0 + (b1 - b0) * u);
            uint8_t a = (uint8_t)lroundf(a0 + (a1 - a0) * u);
            return ((uint32_t)r << 24) | ((uint32_t)gg << 16) | ((uint32_t)b << 8) | (uint32_t)a;
        }
    }
    return g->stops[g->stop_count - 1].color;
}

float cupidimage_svg_resolve_grad_coord(const svg_grad_coord *c, float base, float origin,
                                        svg_gradient_units units) {
    if (!c) {
        return origin;
    }
    if (units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) {
        return origin + c->value * base;
    }
    if (c->is_percent) {
        return origin + c->value * base;
    }
    return c->value;
}

int cupidimage_svg_matrix_to_string(const svg_matrix *m, char *buf, size_t cap) {
    if (!m || !buf || cap == 0) {
        return 0;
    }
    int n = snprintf(buf, cap, "matrix(%.6g %.6g %.6g %.6g %.6g %.6g)", m->a, m->b, m->c, m->d,
                     m->e, m->f);
    if (n < 0 || (size_t)n >= cap) {
        return 0;
    }
    return 1;
}

int cupidimage_svg_parse_points(const char *s, float **out_points, int *out_count) {
    if (!s || !out_points || !out_count) {
        return 0;
    }
    *out_points = NULL;
    *out_count = 0;
    const char *p = s;
    int cap = 0;
    while (1) {
        float x = 0.0f, y = 0.0f;
        if (!cupidimage_svg_parse_number(&p, &x)) {
            break;
        }
        if (!cupidimage_svg_parse_number(&p, &y)) {
            break;
        }
        if (*out_count >= cap) {
            int new_cap = cap ? cap * 2 : 32;
            float *n = (float *)realloc(*out_points, (size_t)new_cap * 2u * sizeof(float));
            if (!n) {
                free(*out_points);
                *out_points = NULL;
                *out_count = 0;
                return 0;
            }
            *out_points = n;
            cap = new_cap;
        }
        (*out_points)[(*out_count) * 2] = x;
        (*out_points)[(*out_count) * 2 + 1] = y;
        (*out_count)++;
    }
    return *out_count > 0;
}

static int svg_flatten_cubic(svg_segments *segs, float x0, float y0, float x1, float y1, float x2,
                             float y2, float x3, float y3, float tol, int depth) {
    float ux = 3.0f * x1 - 2.0f * x0 - x3;
    float uy = 3.0f * y1 - 2.0f * y0 - y3;
    float vx = 3.0f * x2 - 2.0f * x3 - x0;
    float vy = 3.0f * y2 - 2.0f * y3 - y0;
    float d = fmaxf(ux * ux + uy * uy, vx * vx + vy * vy);
    if (d <= tol * tol || depth > 10) {
        return cupidimage_svg_segments_add(segs, x0, y0, x3, y3, 1, 1);
    }
    float x01 = (x0 + x1) * 0.5f;
    float y01 = (y0 + y1) * 0.5f;
    float x12 = (x1 + x2) * 0.5f;
    float y12 = (y1 + y2) * 0.5f;
    float x23 = (x2 + x3) * 0.5f;
    float y23 = (y2 + y3) * 0.5f;
    float x012 = (x01 + x12) * 0.5f;
    float y012 = (y01 + y12) * 0.5f;
    float x123 = (x12 + x23) * 0.5f;
    float y123 = (y12 + y23) * 0.5f;
    float x0123 = (x012 + x123) * 0.5f;
    float y0123 = (y012 + y123) * 0.5f;
    if (!svg_flatten_cubic(segs, x0, y0, x01, y01, x012, y012, x0123, y0123, tol, depth + 1)) {
        return 0;
    }
    return svg_flatten_cubic(segs, x0123, y0123, x123, y123, x23, y23, x3, y3, tol, depth + 1);
}

static int svg_flatten_quad(svg_segments *segs, float x0, float y0, float x1, float y1, float x2,
                            float y2, float tol, int depth) {
    float ux = x0 - 2.0f * x1 + x2;
    float uy = y0 - 2.0f * y1 + y2;
    float d = ux * ux + uy * uy;
    if (d <= tol * tol || depth > 10) {
        return cupidimage_svg_segments_add(segs, x0, y0, x2, y2, 1, 1);
    }
    float x01 = (x0 + x1) * 0.5f;
    float y01 = (y0 + y1) * 0.5f;
    float x12 = (x1 + x2) * 0.5f;
    float y12 = (y1 + y2) * 0.5f;
    float x012 = (x01 + x12) * 0.5f;
    float y012 = (y01 + y12) * 0.5f;
    if (!svg_flatten_quad(segs, x0, y0, x01, y01, x012, y012, tol, depth + 1)) {
        return 0;
    }
    return svg_flatten_quad(segs, x012, y012, x12, y12, x2, y2, tol, depth + 1);
}

int cupidimage_svg_parse_path(const char *d, svg_segments *segs) {
    if (!d) {
        return 0;
    }
    const char *p = d;
    char cmd = 0;
    float cx = 0.0f, cy = 0.0f;
    float sx = 0.0f, sy = 0.0f;
    float last_cx = 0.0f, last_cy = 0.0f;
    float last_qx = 0.0f, last_qy = 0.0f;
    int have_cubic = 0;
    int have_quad = 0;
    int subpath_open = 0;
    float tol = 0.25f;

    while (*p) {
        cupidimage_svg_skip_separators(&p);
        if (!*p) {
            break;
        }
        if (isalpha((unsigned char)*p)) {
            cmd = *p++;
        }
        if (!cmd) {
            break;
        }
        int relative = islower((unsigned char)cmd);
        char ucmd = (char)toupper((unsigned char)cmd);
        switch (ucmd) {
            case 'M': {
                float x = 0.0f, y = 0.0f;
                if (!cupidimage_svg_parse_number(&p, &x) || !cupidimage_svg_parse_number(&p, &y)) {
                    return 1;
                }
                if (relative) {
                    x += cx;
                    y += cy;
                }
                if (subpath_open) {
                    cupidimage_svg_segments_add(segs, cx, cy, sx, sy, 0, 1);
                }
                cx = x;
                cy = y;
                sx = x;
                sy = y;
                subpath_open = 1;
                have_cubic = 0;
                have_quad = 0;
                while (cupidimage_svg_parse_number(&p, &x) && cupidimage_svg_parse_number(&p, &y)) {
                    if (relative) {
                        x += cx;
                        y += cy;
                    }
                    cupidimage_svg_segments_add(segs, cx, cy, x, y, 1, 1);
                    cx = x;
                    cy = y;
                }
                break;
            }
            case 'L': {
                float x = 0.0f, y = 0.0f;
                while (cupidimage_svg_parse_number(&p, &x) && cupidimage_svg_parse_number(&p, &y)) {
                    if (relative) {
                        x += cx;
                        y += cy;
                    }
                    cupidimage_svg_segments_add(segs, cx, cy, x, y, 1, 1);
                    cx = x;
                    cy = y;
                    have_cubic = 0;
                    have_quad = 0;
                }
                break;
            }
            case 'H': {
                float x = 0.0f;
                while (cupidimage_svg_parse_number(&p, &x)) {
                    if (relative) {
                        x += cx;
                    }
                    cupidimage_svg_segments_add(segs, cx, cy, x, cy, 1, 1);
                    cx = x;
                    have_cubic = 0;
                    have_quad = 0;
                }
                break;
            }
            case 'V': {
                float y = 0.0f;
                while (cupidimage_svg_parse_number(&p, &y)) {
                    if (relative) {
                        y += cy;
                    }
                    cupidimage_svg_segments_add(segs, cx, cy, cx, y, 1, 1);
                    cy = y;
                    have_cubic = 0;
                    have_quad = 0;
                }
                break;
            }
            case 'C': {
                float x1, y1, x2, y2, x3, y3;
                while (
                    cupidimage_svg_parse_number(&p, &x1) && cupidimage_svg_parse_number(&p, &y1) &&
                    cupidimage_svg_parse_number(&p, &x2) && cupidimage_svg_parse_number(&p, &y2) &&
                    cupidimage_svg_parse_number(&p, &x3) && cupidimage_svg_parse_number(&p, &y3)) {
                    if (relative) {
                        x1 += cx;
                        y1 += cy;
                        x2 += cx;
                        y2 += cy;
                        x3 += cx;
                        y3 += cy;
                    }
                    if (!svg_flatten_cubic(segs, cx, cy, x1, y1, x2, y2, x3, y3, tol, 0)) {
                        return 0;
                    }
                    cx = x3;
                    cy = y3;
                    last_cx = x2;
                    last_cy = y2;
                    have_cubic = 1;
                    have_quad = 0;
                }
                break;
            }
            case 'S': {
                float x2, y2, x3, y3;
                while (
                    cupidimage_svg_parse_number(&p, &x2) && cupidimage_svg_parse_number(&p, &y2) &&
                    cupidimage_svg_parse_number(&p, &x3) && cupidimage_svg_parse_number(&p, &y3)) {
                    float x1 = cx;
                    float y1 = cy;
                    if (have_cubic) {
                        x1 = 2.0f * cx - last_cx;
                        y1 = 2.0f * cy - last_cy;
                    }
                    if (relative) {
                        x2 += cx;
                        y2 += cy;
                        x3 += cx;
                        y3 += cy;
                    }
                    if (!svg_flatten_cubic(segs, cx, cy, x1, y1, x2, y2, x3, y3, tol, 0)) {
                        return 0;
                    }
                    cx = x3;
                    cy = y3;
                    last_cx = x2;
                    last_cy = y2;
                    have_cubic = 1;
                    have_quad = 0;
                }
                break;
            }
            case 'Q': {
                float x1, y1, x2, y2;
                while (
                    cupidimage_svg_parse_number(&p, &x1) && cupidimage_svg_parse_number(&p, &y1) &&
                    cupidimage_svg_parse_number(&p, &x2) && cupidimage_svg_parse_number(&p, &y2)) {
                    if (relative) {
                        x1 += cx;
                        y1 += cy;
                        x2 += cx;
                        y2 += cy;
                    }
                    if (!svg_flatten_quad(segs, cx, cy, x1, y1, x2, y2, tol, 0)) {
                        return 0;
                    }
                    cx = x2;
                    cy = y2;
                    last_qx = x1;
                    last_qy = y1;
                    have_quad = 1;
                    have_cubic = 0;
                }
                break;
            }
            case 'T': {
                float x2, y2;
                while (cupidimage_svg_parse_number(&p, &x2) &&
                       cupidimage_svg_parse_number(&p, &y2)) {
                    float x1 = cx;
                    float y1 = cy;
                    if (have_quad) {
                        x1 = 2.0f * cx - last_qx;
                        y1 = 2.0f * cy - last_qy;
                    }
                    if (relative) {
                        x2 += cx;
                        y2 += cy;
                    }
                    if (!svg_flatten_quad(segs, cx, cy, x1, y1, x2, y2, tol, 0)) {
                        return 0;
                    }
                    cx = x2;
                    cy = y2;
                    last_qx = x1;
                    last_qy = y1;
                    have_quad = 1;
                    have_cubic = 0;
                }
                break;
            }
            case 'A': {
                float rx, ry, xrot, large_arc, sweep, x, y;
                while (cupidimage_svg_parse_number(&p, &rx) &&
                       cupidimage_svg_parse_number(&p, &ry) &&
                       cupidimage_svg_parse_number(&p, &xrot) &&
                       cupidimage_svg_parse_number(&p, &large_arc) &&
                       cupidimage_svg_parse_number(&p, &sweep) &&
                       cupidimage_svg_parse_number(&p, &x) && cupidimage_svg_parse_number(&p, &y)) {
                    if (relative) {
                        x += cx;
                        y += cy;
                    }
                    svg_segments_add_arc_rot(segs, cx, cy, x, y, rx, ry, xrot,
                                             (int)lroundf(large_arc) != 0,
                                             (int)lroundf(sweep) != 0);
                    cx = x;
                    cy = y;
                    have_cubic = 0;
                    have_quad = 0;
                }
                break;
            }
            case 'Z': {
                if (subpath_open) {
                    cupidimage_svg_segments_add(segs, cx, cy, sx, sy, 1, 1);
                    cx = sx;
                    cy = sy;
                    subpath_open = 0;
                }
                have_cubic = 0;
                have_quad = 0;
                break;
            }
            default:
                return 1;
        }
    }
    if (subpath_open) {
        cupidimage_svg_segments_add(segs, cx, cy, sx, sy, 0, 1);
    }
    return 1;
}

int cupidimage_svg_parse_length_attr(const svg_tag *tag, const char *name, float base, float dpi,
                                     float *out) {
    const char *val = cupidimage_svg_get_attr(tag, name);
    if (!val) {
        return 0;
    }
    int ok = 0;
    float v = cupidimage_svg_parse_length(val, base, dpi, &ok);
    if (ok) {
        *out = v;
        return 1;
    }
    return 0;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
