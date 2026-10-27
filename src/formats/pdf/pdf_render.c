#include "cupidimage.h"
#include "core/internal.h"

#include <ctype.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdf_internal.h"

#include "pdf_render_internal.h"

int cupidimage_pdf_draw_form(pdf_render_state *st, int object, const pdf_object_view *form);
static int pdf_render_stream(pdf_render_state *st, const unsigned char *stream, size_t stream_len);

static void pdf_clip_retain(pdf_clip_mask *clip) {
    if (clip) clip->references++;
}

static void pdf_clip_release(pdf_render_state *st, pdf_clip_mask *clip) {
    if (clip && --clip->references == 0) {
        st->clip_budget->bytes_used -= clip->bytes;
        free(clip);
    }
}

static void pdf_render_state_release(pdf_render_state *st) {
    pdf_clip_release(st, st->gs.clip);
    pdf_clip_release(st, st->text_clip);
    if (st->text_winding) {
        st->clip_budget->bytes_used-=st->text_winding_bytes;
        free(st->text_winding);
    }
    for (int i = 0; i < st->gs_stack_len; i++) pdf_clip_release(st, st->gs_stack[i].clip);
}

unsigned char cupidimage_pdf_clamp_u8(double v) {
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    return (unsigned char)(v * 255.0 + 0.5);
}

static int pdf_parse_number_token(const unsigned char *data, size_t size, size_t *pos, double *out) {
    size_t p = *pos;
    int saw_digit = 0;
    int saw_dot = 0;

    if (p < size && (data[p] == '+' || data[p] == '-')) {
        p++;
    }
    while (p < size) {
        if (data[p] >= '0' && data[p] <= '9') {
            saw_digit = 1;
            p++;
            continue;
        }
        if (data[p] == '.' && !saw_dot) {
            saw_dot = 1;
            p++;
            continue;
        }
        break;
    }

    if (!saw_digit) {
        return 0;
    }

    char tmp[64];
    size_t n = p - *pos;
    if (n >= sizeof(tmp)) {
        n = sizeof(tmp) - 1;
    }
    memcpy(tmp, &data[*pos], n);
    tmp[n] = '\0';
    *out = strtod(tmp, NULL);
    *pos = p;
    return 1;
}

static int pdf_parse_name_token(const unsigned char *data, size_t size, size_t *pos,
                                char **out_str, size_t *out_len) {
    if (*pos >= size || data[*pos] != '/') {
        return 0;
    }
    size_t start = *pos + 1;
    size_t p = start;
    while (p < size && !cupidimage_pdf_is_ws(data[p]) && !cupidimage_pdf_is_delim(data[p])) {
        p++;
    }

    size_t len = p - start;
    char *s = (char *)malloc(len + 1);
    if (!s) {
        return 0;
    }
    memcpy(s, &data[start], len);
    s[len] = '\0';

    *out_str = s;
    *out_len = len;
    *pos = p;
    return 1;
}

static int pdf_parse_literal_string(const unsigned char *data, size_t size, size_t *pos,
                                    char **out_str, size_t *out_len) {
    if (*pos >= size || data[*pos] != '(') {
        return 0;
    }

    size_t p = *pos + 1;
    int depth = 1;
    size_t cap = 64;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        return 0;
    }

    while (p < size) {
        unsigned char c = data[p++];

        if (c == '\\') {
            if (p >= size) break;
            unsigned char e = data[p++];
            unsigned char outc = e;
            if (e == 'n') outc = '\n';
            else if (e == 'r') outc = '\r';
            else if (e == 't') outc = '\t';
            else if (e == 'b') outc = '\b';
            else if (e == 'f') outc = '\f';
            else if (e == '\\' || e == '(' || e == ')') outc = e;
            else if (e == '\n' || e == '\r') {
                if (e == '\r' && p < size && data[p] == '\n') p++;
                continue;
            } else if (e >= '0' && e <= '7') {
                int v = e - '0';
                for (int i = 0; i < 2 && p < size && data[p] >= '0' && data[p] <= '7'; i++) {
                    v = (v << 3) + (data[p] - '0');
                    p++;
                }
                outc = (unsigned char)v;
            }

            if (len + 1 >= cap) {
                size_t ncap = cap * 2;
                char *n = (char *)realloc(buf, ncap);
                if (!n) {
                    free(buf);
                    return 0;
                }
                buf = n;
                cap = ncap;
            }
            buf[len++] = (char)outc;
            continue;
        }

        if (c == '(') {
            depth++;
            if (len + 1 >= cap) {
                size_t ncap = cap * 2;
                char *n = (char *)realloc(buf, ncap);
                if (!n) {
                    free(buf);
                    return 0;
                }
                buf = n;
                cap = ncap;
            }
            buf[len++] = (char)c;
            continue;
        }

        if (c == ')') {
            depth--;
            if (depth == 0) {
                *pos = p;
                buf[len] = '\0';
                *out_str = buf;
                *out_len = len;
                return 1;
            }
            if (len + 1 >= cap) {
                size_t ncap = cap * 2;
                char *n = (char *)realloc(buf, ncap);
                if (!n) {
                    free(buf);
                    return 0;
                }
                buf = n;
                cap = ncap;
            }
            buf[len++] = (char)c;
            continue;
        }

        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *n = (char *)realloc(buf, ncap);
            if (!n) {
                free(buf);
                return 0;
            }
            buf = n;
            cap = ncap;
        }
        buf[len++] = (char)c;
    }

    free(buf);
    return 0;
}

static int pdf_parse_hex_string(const unsigned char *data, size_t size, size_t *pos,
                                char **out_str, size_t *out_len) {
    if (*pos >= size || data[*pos] != '<') {
        return 0;
    }
    if (*pos + 1 < size && data[*pos + 1] == '<') {
        return 0;
    }

    size_t p = *pos + 1;
    size_t cap = 64;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        return 0;
    }

    int have_hi = 0;
    int hi = 0;
    while (p < size) {
        unsigned char c = data[p++];
        if (c == '>') {
            break;
        }
        if (isspace(c)) {
            continue;
        }

        int v = -1;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        if (v < 0) {
            free(buf);
            return 0;
        }

        if (!have_hi) {
            hi = v;
            have_hi = 1;
        } else {
            unsigned char outc = (unsigned char)((hi << 4) | v);
            if (len + 1 >= cap) {
                size_t ncap = cap * 2;
                char *n = (char *)realloc(buf, ncap);
                if (!n) {
                    free(buf);
                    return 0;
                }
                buf = n;
                cap = ncap;
            }
            buf[len++] = (char)outc;
            have_hi = 0;
        }
    }

    if (have_hi) {
        unsigned char outc = (unsigned char)(hi << 4);
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *n = (char *)realloc(buf, ncap);
            if (!n) {
                free(buf);
                return 0;
            }
            buf = n;
            cap = ncap;
        }
        buf[len++] = (char)outc;
    }

    buf[len] = '\0';
    *out_str = buf;
    *out_len = len;
    *pos = p;
    return 1;
}

/* Content property dictionaries are operands, not hex strings or operators.
 * Scan nested dictionaries/arrays without executing their contents. */
static int pdf_parse_dictionary_token(const unsigned char *data, size_t size, size_t *pos,
                                      char **out, size_t *out_len) {
    size_t start = *pos, p = start;
    unsigned char stack[64];
    unsigned depth = 0;
    if (p > size || size - p < 2 || data[p] != '<' || data[p + 1] != '<') return 0;
    stack[depth++] = '>'; p += 2;
    while (p < size && depth) {
        if (p - start > 1024u * 1024u) return 0;
        unsigned char c = data[p++];
        if (c == '%') {
            while (p < size && data[p] != '\r' && data[p] != '\n') p++;
        } else if (c == '(') {
            unsigned nesting = 1;
            while (p < size && nesting) {
                c = data[p++];
                if (c == '\\') { if (p >= size) return 0; p++; }
                else if (c == '(') { if (++nesting > 64) return 0; }
                else if (c == ')') nesting--;
            }
            if (nesting) return 0;
        } else if (c == '<') {
            if (p < size && data[p] == '<') {
                if (depth == sizeof(stack)) return 0;
                stack[depth++] = '>'; p++;
            } else {
                int closed = 0;
                while (p < size) {
                    c = data[p++];
                    if (c == '>') { closed = 1; break; }
                    if (!cupidimage_pdf_is_ws(c) && !((c >= '0' && c <= '9') ||
                        (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
                }
                if (!closed) return 0;
            }
        } else if (c == '[') {
            if (depth == sizeof(stack)) return 0;
            stack[depth++] = ']';
        } else if (c == ']') {
            if (stack[depth - 1] != ']') return 0;
            depth--;
        } else if (c == '>') {
            if (stack[depth - 1] != '>' || p >= size || data[p++] != '>') return 0;
            depth--;
        } else if (c == ')') return 0;
    }
    if (depth || p - start > 1024u * 1024u) return 0;
    size_t length = p - start;
    char *copy = (char *)malloc(length + 1);
    if (!copy) return 0;
    memcpy(copy, data + start, length); copy[length] = '\0';
    *out = copy; *out_len = length; *pos = p;
    return 1;
}

static int pdf_parse_array_token(const unsigned char *data, size_t size, size_t *pos,
                                 char **out_str, size_t *out_len) {
    if (*pos >= size || data[*pos] != '[') {
        return 0;
    }

    size_t start = *pos + 1;
    size_t p = start;
    int depth = 1;

    while (p < size) {
        unsigned char c = data[p];
        if (c == '%') {
            while (p < size && data[p] != '\n' && data[p] != '\r') p++;
            continue;
        }
        if (c == '(') {
            size_t tmp = p;
            char *dummy = NULL;
            size_t dlen = 0;
            if (!pdf_parse_literal_string(data, size, &tmp, &dummy, &dlen)) {
                return 0;
            }
            free(dummy);
            p = tmp;
            continue;
        }
        if (c == '[') {
            depth++;
            p++;
            continue;
        }
        if (c == ']') {
            depth--;
            if (depth == 0) {
                size_t len = p - start;
                char *s = (char *)malloc(len + 1);
                if (!s) {
                    return 0;
                }
                memcpy(s, &data[start], len);
                s[len] = '\0';
                *out_str = s;
                *out_len = len;
                *pos = p + 1;
                return 1;
            }
            p++;
            continue;
        }
        p++;
    }

    return 0;
}

void cupidimage_pdf_composite_pixel(pdf_render_state *st, int x, int y, uint8_t r, uint8_t g, uint8_t b, double alpha) {
    cupidimage_image *img = st->img;
    if (!img || !img->rgba || alpha <= 0) {
        return;
    }
    if (x < 0 || y < 0 || x >= (int)img->width || y >= (int)img->height) {
        return;
    }
    size_t pixel = (size_t)y * img->width + (size_t)x;
    if (st->gs.clip && !(st->gs.clip->bits[pixel / 8] & (1u << (pixel % 8)))) return;
    double px = st->page_x0_pts + (x + 0.5) * (72.0 / 96.0);
    double py = st->page_top_pts - (y + 0.5) * (72.0 / 96.0);
    for (int i = 0; i < st->form_depth; i++) {
        const pdf_form_clip *clip = &st->form_clips[i];
        const double *m = clip->inverse;
        double u = m[0] * px + m[2] * py + m[4];
        double v = m[1] * px + m[3] * py + m[5];
        if (u < clip->box[0] || u >= clip->box[2] || v < clip->box[1] || v >= clip->box[3]) return;
    }
    size_t idx = pixel * 4u;
    if (st->gs.blend_mode == 0 && alpha == 1) {
        img->rgba[idx] = r; img->rgba[idx+1] = g; img->rgba[idx+2] = b; img->rgba[idx+3] = 255;
        return;
    }
    uint8_t source[3] = {r,g,b};
    if (st->gs.blend_mode == 0) {
        for (int i = 0; i < 3; i++) img->rgba[idx+i] = (uint8_t)floor(alpha*source[i] + (1-alpha)*img->rgba[idx+i] + .5);
        img->rgba[idx+3] = 255;
        return;
    }
    double blended[3];
    cupidimage_pdf_blend_rgb(st->gs.blend_mode, source, img->rgba+idx, blended);
    for (int i = 0; i < 3; i++) img->rgba[idx+i] = cupidimage_pdf_clamp_u8(alpha*blended[i] + (1-alpha)*img->rgba[idx+i]/255.0);
    img->rgba[idx + 3] = 255;
}

void cupidimage_pdf_set_pixel(pdf_render_state *st, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    cupidimage_pdf_composite_pixel(st, x, y, r, g, b, st->gs.fill_alpha);
}

static void pdf_matrix_identity(float m[6]) {
    m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f;
    m[3] = 1.0f; m[4] = 0.0f; m[5] = 0.0f;
}

static void pdf_matrix_concat(float dst[6], const float left[6]) {
    float a = dst[0] * left[0] + dst[2] * left[1];
    float b = dst[1] * left[0] + dst[3] * left[1];
    float c = dst[0] * left[2] + dst[2] * left[3];
    float d = dst[1] * left[2] + dst[3] * left[3];
    float e = dst[0] * left[4] + dst[2] * left[5] + dst[4];
    float f = dst[1] * left[4] + dst[3] * left[5] + dst[5];
    dst[0] = a;
    dst[1] = b;
    dst[2] = c;
    dst[3] = d;
    dst[4] = e;
    dst[5] = f;
}

void cupidimage_pdf_user_to_pixel(const pdf_render_state *st, float ux, float uy, float *px, float *py) {
    float tx = st->gs.ctm[0] * ux + st->gs.ctm[2] * uy + st->gs.ctm[4];
    float ty = st->gs.ctm[1] * ux + st->gs.ctm[3] * uy + st->gs.ctm[5];
    float s = 96.0f / 72.0f;
    *px = (tx - st->page_x0_pts) * s;
    *py = (st->page_top_pts - ty) * s;
}

static void pdf_path_clear(pdf_path *p) {
    if (!p) return;
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

static void pdf_path_add_seg(pdf_path *p, float x0, float y0, float x1, float y1) {
    if (!p) return;
    if (p->seg_count >= (int)(sizeof(p->segs) / sizeof(p->segs[0]))) {
        p->overflow = 1;
        return;
    }
    p->segs[p->seg_count].x0 = x0;
    p->segs[p->seg_count].y0 = y0;
    p->segs[p->seg_count].x1 = x1;
    p->segs[p->seg_count].y1 = y1;
    p->segs[p->seg_count].starts_subpath = p->seg_count == p->subpath_first_seg;
    p->seg_count++;
}

static void pdf_path_move_to(pdf_path *p, float x, float y) {
    if (!p) return;
    p->subpath_first_seg = p->seg_count;
    p->current_x = x;
    p->current_y = y;
    p->subpath_start_x = x;
    p->subpath_start_y = y;
    p->has_current = 1;
    p->has_subpath = 1;
}

static void pdf_path_line_to(pdf_path *p, float x, float y) {
    if (!p) return;
    if (!p->has_current) {
        pdf_path_move_to(p, x, y);
        return;
    }
    pdf_path_add_seg(p, p->current_x, p->current_y, x, y);
    p->current_x = x;
    p->current_y = y;
}

static void pdf_path_close(pdf_path *p) {
    if (!p || !p->has_current || !p->has_subpath) return;
    if (fabsf(p->current_x - p->subpath_start_x) > 1e-4f ||
        fabsf(p->current_y - p->subpath_start_y) > 1e-4f) {
        pdf_path_add_seg(p, p->current_x, p->current_y, p->subpath_start_x, p->subpath_start_y);
    }
    p->current_x = p->subpath_start_x;
    p->current_y = p->subpath_start_y;
}

/* Paths retain device coordinates when cm or q/Q changes the graphics state. */
static void pdf_path_move_user(pdf_render_state *st, float x, float y) {
    float px, py;
    cupidimage_pdf_user_to_pixel(st, x, y, &px, &py);
    pdf_path_move_to(&st->path, px, py);
}

static void pdf_path_line_user(pdf_render_state *st, float x, float y) {
    float px, py;
    cupidimage_pdf_user_to_pixel(st, x, y, &px, &py);
    pdf_path_line_to(&st->path, px, py);
}

static float pdf_point_to_segment_dist_sq(float px, float py, float x0, float y0, float x1, float y1) {
    float vx = x1 - x0;
    float vy = y1 - y0;
    float wx = px - x0;
    float wy = py - y0;
    float c1 = vx * wx + vy * wy;
    if (c1 <= 0.0f) {
        float dx = px - x0;
        float dy = py - y0;
        return dx * dx + dy * dy;
    }
    float c2 = vx * vx + vy * vy;
    if (c2 <= 1e-8f) {
        float dx = px - x0;
        float dy = py - y0;
        return dx * dx + dy * dy;
    }
    if (c1 >= c2) {
        float dx = px - x1;
        float dy = py - y1;
        return dx * dx + dy * dy;
    }
    float t = c1 / c2;
    float qx = x0 + t * vx;
    float qy = y0 + t * vy;
    float dx = px - qx;
    float dy = py - qy;
    return dx * dx + dy * dy;
}

static void pdf_flatten_curve(pdf_render_state *st, float x0, float y0,
                               float x1, float y1, float x2, float y2,
                               float x3, float y3, unsigned depth) {
    if (st->path.seg_count >= 4096) { st->path.overflow = 1; return; }
    /* Subdivide until both control points are within a quarter pixel of the chord. */
    if (depth >= 12 ||
        (pdf_point_to_segment_dist_sq(x1, y1, x0, y0, x3, y3) <= 0.0625f &&
         pdf_point_to_segment_dist_sq(x2, y2, x0, y0, x3, y3) <= 0.0625f)) {
        pdf_path_line_to(&st->path, x3, y3);
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

static void pdf_path_curve_to(pdf_render_state *st, float x1, float y1,
                               float x2, float y2, float x3, float y3, int first_is_current) {
    if (!st->path.has_current) return;
    if (first_is_current) {
        x1 = st->path.current_x; y1 = st->path.current_y;
    } else {
        cupidimage_pdf_user_to_pixel(st, x1, y1, &x1, &y1);
    }
    cupidimage_pdf_user_to_pixel(st, x2, y2, &x2, &y2);
    cupidimage_pdf_user_to_pixel(st, x3, y3, &x3, &y3);
    if (!isfinite(x1) || !isfinite(y1) || !isfinite(x2) || !isfinite(y2) ||
        !isfinite(x3) || !isfinite(y3)) { st->path.overflow = 1; return; }
    pdf_flatten_curve(st, st->path.current_x, st->path.current_y,
                       x1, y1, x2, y2, x3, y3, 0);
}

void cupidimage_pdf_stroke_segments(pdf_render_state *st, const pdf_path *path, unsigned char *target) {
    if (!st || !st->img || path->seg_count <= 0) return;

    unsigned char *coverage = target;
    if (!coverage && (st->gs.stroke_alpha < 1 || st->gs.blend_mode)) {
        size_t pixels = (size_t)st->img->width * st->img->height;
        coverage = (unsigned char *)calloc((pixels+7)/8, 1);
        if (!coverage) { st->image_failed=1; cupidimage_set_err(st->error, st->error_capacity, "pdf: stroke coverage allocation failed"); return; }
    }
    float px_per_pt = 96.0f / 72.0f;
    float half_w = st->gs.line_width_pts * px_per_pt * 0.5f;
    if (half_w < 0.5f) half_w = 0.5f;
    float half_w_sq = half_w * half_w;

    for (int i = 0; i < path->seg_count; i++) {
        float x0 = path->segs[i].x0, y0 = path->segs[i].y0;
        float x1 = path->segs[i].x1, y1 = path->segs[i].y1;

        if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1)) continue;
        float w = (float)st->img->width - 1, h = (float)st->img->height - 1;
        int min_x = (int)fminf(w, fmaxf(0, floorf(fminf(x0, x1) - half_w - 1)));
        int min_y = (int)fminf(h, fmaxf(0, floorf(fminf(y0, y1) - half_w - 1)));
        int max_x = (int)fminf(w, fmaxf(0, ceilf(fmaxf(x0, x1) + half_w + 1)));
        int max_y = (int)fminf(h, fmaxf(0, ceilf(fmaxf(y0, y1) + half_w + 1)));

        for (int y = min_y; y <= max_y; y++) {
            float py = (float)y + 0.5f;
            for (int x = min_x; x <= max_x; x++) {
                float px = (float)x + 0.5f;
                float d2 = pdf_point_to_segment_dist_sq(px, py, x0, y0, x1, y1);
                if (d2 <= half_w_sq) {
                    if (coverage) {
                        size_t pixel = (size_t)y*st->img->width+x;
                        unsigned char bit = (unsigned char)(1u << (pixel%8));
                        if (coverage[pixel/8] & bit) continue;
                        coverage[pixel/8] |= bit;
                    }
                    if (target) continue;
                    cupidimage_pdf_composite_pixel(st, x, y, st->gs.stroke_r, st->gs.stroke_g, st->gs.stroke_b, st->gs.stroke_alpha);
                }
            }
        }
    }
    if (!target) free(coverage);
}

static int pdf_scanline_intersects(float x, float y, const pdf_seg *seg) {
    float y0 = seg->y0;
    float y1 = seg->y1;
    if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
        /* Skip horizontal edges (approximately) to avoid double-counting. */
        if (fabsf(y0 - y1) <= 1e-6f) {
            return 0;
        }
        float t = (y - y0) / (y1 - y0);
        float ix = seg->x0 + t * (seg->x1 - seg->x0);
        return ix > x ? (y1 > y0 ? 1 : -1) : 0;
    }
    return 0;
}

static int pdf_rasterize_path(pdf_render_state *st, int even_odd, unsigned char *mask) {
    if (st->path.seg_count <= 0) return 1;

    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    pdf_seg xformed[8192];
    int n = 0;
    float start_x = 0, start_y = 0;

    for (int i = 0; i < st->path.seg_count; i++) {
        pdf_seg edge = st->path.segs[i];
        if (!isfinite(edge.x0) || !isfinite(edge.y0) || !isfinite(edge.x1) || !isfinite(edge.y1)) return 0;
        if (st->path.segs[i].starts_subpath) { start_x = edge.x0; start_y = edge.y0; }
        xformed[n++] = edge;
        min_x = fminf(min_x, fminf(edge.x0, edge.x1));
        max_x = fmaxf(max_x, fmaxf(edge.x0, edge.x1));
        min_y = fminf(min_y, fminf(edge.y0, edge.y1));
        max_y = fmaxf(max_y, fmaxf(edge.y0, edge.y1));
        if (i + 1 == st->path.seg_count || st->path.segs[i + 1].starts_subpath) {
            /* Fill closes each subpath without adding a closing stroke. */
            pdf_seg closing = {edge.x1, edge.y1, start_x, start_y, 0};
            xformed[n++] = closing;
        }
    }

    int x0 = (int)fminf((float)st->img->width, fmaxf(0, floorf(min_x)));
    int y0 = (int)fminf((float)st->img->height, fmaxf(0, floorf(min_y)));
    int x1 = (int)fminf((float)st->img->width, fmaxf(0, ceilf(max_x)));
    int y1 = (int)fminf((float)st->img->height, fmaxf(0, ceilf(max_y)));

    if (mask) {
        size_t area = (size_t)(x1 - x0) * (size_t)(y1 - y0);
        if (area > st->clip_budget->tests_left / (size_t)n) return 0;
        st->clip_budget->tests_left -= area * (size_t)n;
    }

    for (int y = y0; y < y1; y++) {
        float py = (float)y + 0.5f;
        for (int x = x0; x < x1; x++) {
            float px = (float)x + 0.5f;
            int crossings = 0;
            for (int i = 0; i < n; i++) {
                crossings += pdf_scanline_intersects(px, py, &xformed[i]);
            }
            if (even_odd ? crossings % 2 != 0 : crossings != 0) {
                if (mask) {
                    size_t pixel = (size_t)y * st->img->width + (size_t)x;
                    mask[pixel / 8] |= (unsigned char)(1u << (pixel % 8));
                } else {
                    cupidimage_pdf_set_pixel(st, x, y, st->gs.fill_r, st->gs.fill_g, st->gs.fill_b);
                }
            }
        }
    }
    return 1;
}

static void pdf_fill_path(pdf_render_state *st, int even_odd) {
    pdf_rasterize_path(st, even_odd, NULL);
}

/* Fill-and-stroke is one PDF object. Stroke-covered pixels use the original
 * backdrop, not a fill that has already been composited onto it. */
static void pdf_fill_and_stroke(pdf_render_state *st, int even_odd) {
    if (st->gs.fill_alpha == 1 && st->gs.stroke_alpha == 1 && !st->gs.blend_mode) {
        pdf_fill_path(st, even_odd); cupidimage_pdf_stroke_segments(st, &st->path, NULL); return;
    }
    size_t pixels = (size_t)st->img->width * st->img->height, bytes = (pixels+7)/8;
    unsigned char *masks = (unsigned char *)calloc(bytes, 2);
    if (!masks) { st->image_failed=1; cupidimage_set_err(st->error, st->error_capacity, "pdf: path coverage allocation failed"); return; }
    unsigned char *stroke = masks+bytes;
    if (!pdf_rasterize_path(st, even_odd, masks)) { free(masks); st->image_failed=1; cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid fill coverage"); return; }
    cupidimage_pdf_stroke_segments(st, &st->path, stroke);
    for (size_t pixel = 0; pixel < pixels; pixel++) {
        unsigned bit = 1u << (pixel%8);
        int x=(int)(pixel%st->img->width), y=(int)(pixel/st->img->width);
        if (stroke[pixel/8] & bit) cupidimage_pdf_composite_pixel(st, x, y, st->gs.stroke_r, st->gs.stroke_g, st->gs.stroke_b, st->gs.stroke_alpha);
        else if (masks[pixel/8] & bit) cupidimage_pdf_set_pixel(st, x, y, st->gs.fill_r, st->gs.fill_g, st->gs.fill_b);
    }
    free(masks);
}

static int pdf_apply_clip(pdf_render_state *st) {
    size_t bytes = ((size_t)st->img->width * st->img->height + 7) / 8;
    if (st->path.overflow || !st->clip_budget->calls_left ||
        bytes > 64u * 1024u * 1024u - st->clip_budget->bytes_used) return 0;
    st->clip_budget->calls_left--;
    pdf_clip_mask *clip = (pdf_clip_mask *)calloc(1, sizeof(*clip) + bytes);
    if (!clip) return 0;
    clip->references = 1;
    clip->bytes = bytes;
    if (!pdf_rasterize_path(st, st->pending_clip == 2, clip->bits)) {
        free(clip);
        return 0;
    }
    if (st->gs.clip) {
        for (size_t i = 0; i < bytes; i++) clip->bits[i] &= st->gs.clip->bits[i];
    }
    st->clip_budget->bytes_used += bytes;
    pdf_clip_release(st, st->gs.clip);
    st->gs.clip = clip;
    return 1;
}

int cupidimage_pdf_text_clip_prepare(pdf_render_state *st) {
    if (st->text_clip) return 1;
    size_t pixels=(size_t)st->img->width*st->img->height,bytes=(pixels+7)/8;
    size_t available=64u*1024u*1024u-st->clip_budget->bytes_used;
    if (!st->clip_budget->calls_left || bytes>available || pixels>(available-bytes)/sizeof(int32_t)) return 0;
    pdf_clip_mask *clip=calloc(1,sizeof(*clip)+bytes);
    if (!clip) return 0;
    int32_t *winding=calloc(pixels,sizeof(*winding));
    if (!winding) {free(clip);return 0;}
    clip->references=1; clip->bytes=bytes;
    st->text_winding=winding;st->text_winding_bytes=pixels*sizeof(*winding);
    st->clip_budget->bytes_used+=bytes+st->text_winding_bytes;
    st->clip_budget->calls_left--;
    st->text_clip=clip;
    return 1;
}

void cupidimage_pdf_text_end(pdf_render_state *st) {
    if (st->text_clip) {
        if (st->gs.clip) for (size_t i=0;i<st->text_clip->bytes;i++)
            st->text_clip->bits[i]&=st->gs.clip->bits[i];
        pdf_clip_release(st,st->gs.clip);
        st->gs.clip=st->text_clip;
        st->text_clip=NULL;
        st->clip_budget->bytes_used-=st->text_winding_bytes;
        free(st->text_winding);st->text_winding=NULL;st->text_winding_bytes=0;
    }
    st->in_text=0;
}

static void pdf_path_end(pdf_render_state *st) {
    if (st->pending_clip && !pdf_apply_clip(st)) {
        cupidimage_set_err(st->error, st->error_capacity, "pdf: clipping path exceeds limits or is invalid");
        st->image_failed = 1;
    }
    st->pending_clip = 0;
    pdf_path_clear(&st->path);
}

static void pdf_render_tj_array(pdf_render_state *st, const char *arr, size_t len) {
    size_t pos = 0;
    const unsigned char *data = (const unsigned char *)arr;

    while (pos < len) {
        cupidimage_pdf_skip_ws_and_comments(data, len, &pos);
        if (pos >= len) {
            break;
        }

        if (data[pos] == '(') {
            char *txt = NULL;
            size_t txt_len = 0;
            if (pdf_parse_literal_string(data, len, &pos, &txt, &txt_len)) {
                cupidimage_pdf_render_text_string(st, txt, txt_len);
                free(txt);
                continue;
            }
            break;
        }

        if (data[pos] == '<' && !(pos + 1 < len && data[pos + 1] == '<')) {
            char *txt = NULL;
            size_t txt_len = 0;
            if (pdf_parse_hex_string(data, len, &pos, &txt, &txt_len)) {
                cupidimage_pdf_render_text_string(st, txt, txt_len);
                free(txt);
                continue;
            }
            break;
        }

        if (data[pos] == '+' || data[pos] == '-' || data[pos] == '.' ||
            (data[pos] >= '0' && data[pos] <= '9')) {
            double num = 0.0;
            if (pdf_parse_number_token(data, len, &pos, &num)) {
                cupidimage_pdf_text_advance(st, -num / 1000.0 * st->gs.font_size_pts * st->gs.horizontal_scale);
                continue;
            }
        }

        pos++;
    }
}

typedef enum {
    PDF_OPERAND_NUM,
    PDF_OPERAND_NAME,
    PDF_OPERAND_STRING,
    PDF_OPERAND_ARRAY,
    PDF_OPERAND_DICTIONARY
} pdf_operand_type;

typedef struct {
    pdf_operand_type type;
    double num;
    char *str;
    size_t len;
} pdf_operand;

static void pdf_clear_operands(pdf_operand *ops, int *count) {
    if (!ops || !count) {
        return;
    }
    for (int i = 0; i < *count; i++) {
        free(ops[i].str);
        ops[i].str = NULL;
        ops[i].len = 0;
        ops[i].num = 0.0;
    }
    *count = 0;
}

static int pdf_expect_num(const pdf_operand *op) {
    return op && op->type == PDF_OPERAND_NUM;
}



static int pdf_set_device_color(pdf_render_state *st, int stroke, int space,
                                 const pdf_operand *ops, int count) {
    if ((space != 1 && space != 3 && space != 4) || count != space) return 0;
    for (int i = 0; i < count; i++) if (!pdf_expect_num(&ops[i])) return 0;
    uint8_t r, g, b;
    if (space == 1) {
        r = g = b = cupidimage_pdf_clamp_u8(ops[0].num);
    } else if (space == 3) {
        r = cupidimage_pdf_clamp_u8(ops[0].num);
        g = cupidimage_pdf_clamp_u8(ops[1].num);
        b = cupidimage_pdf_clamp_u8(ops[2].num);
    } else {
        double c = fmin(1, fmax(0, ops[0].num));
        double m = fmin(1, fmax(0, ops[1].num));
        double y = fmin(1, fmax(0, ops[2].num));
        double k = fmin(1, fmax(0, ops[3].num));
        r = cupidimage_pdf_clamp_u8(1 - fmin(1, c + k));
        g = cupidimage_pdf_clamp_u8(1 - fmin(1, m + k));
        b = cupidimage_pdf_clamp_u8(1 - fmin(1, y + k));
    }
    if (stroke) {
        st->gs.stroke_r = r; st->gs.stroke_g = g; st->gs.stroke_b = b;
        st->gs.stroke_space.components = space;
        st->gs.stroke_space.indexed = 0;
    } else {
        st->gs.fill_r = r; st->gs.fill_g = g; st->gs.fill_b = b;
        st->gs.fill_space.components = space;
        st->gs.fill_space.indexed = 0;
    }
    return 1;
}

static void pdf_execute_operator(pdf_render_state *st, const char *op,
                                 pdf_operand *ops, int *count) {
    int n = *count;
    if (st->glyph_active && !st->glyph_seen_metrics && strcmp(op,"d0") && strcmp(op,"d1")) {
        st->image_failed=1;
        cupidimage_set_err(st->error,st->error_capacity,"pdf: Type 3 glyph must begin with d0 or d1");
        pdf_clear_operands(ops,count);return;
    }
    if (!strcmp(op,"d0") || !strcmp(op,"d1")) {
        int box=op[1]=='1',valid=st->glyph_active && !st->glyph_seen_metrics && n==(box?6:2);
        for(int i=0;valid && i<n;i++) valid=pdf_expect_num(&ops[i]) && isfinite(ops[i].num) && fabs(ops[i].num)<=1e30;
        valid=valid && ops[1].num==0 && fabs(ops[0].num-st->glyph_expected_width)<=1e-6;
        if(box && valid)valid=ops[2].num<=ops[4].num && ops[3].num<=ops[5].num;
        if(valid) {st->glyph_seen_metrics=1;st->glyph_uncolored|=box;}
        else {st->image_failed=1;cupidimage_set_err(st->error,st->error_capacity,"pdf: invalid Type 3 glyph metrics");}
        pdf_clear_operands(ops,count);return;
    }
    if(st->glyph_uncolored && (!strcmp(op,"g") || !strcmp(op,"G") || !strcmp(op,"rg") || !strcmp(op,"RG") ||
        !strcmp(op,"k") || !strcmp(op,"K") || !strcmp(op,"cs") || !strcmp(op,"CS") ||
        !strcmp(op,"sc") || !strcmp(op,"SC") || !strcmp(op,"scn") || !strcmp(op,"SCN"))) {
        pdf_clear_operands(ops,count);return;
    }

    if (strcmp(op, "sh") == 0) {
        if (st->glyph_uncolored || n != 1 || ops[0].type != PDF_OPERAND_NAME || !cupidimage_pdf_draw_shading(st, ops[0].str)) {
            st->image_failed = 1;
            cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid or unsupported shading");
        }
    } else if (strcmp(op, "gs") == 0) {
        if (n != 1 || ops[0].type != PDF_OPERAND_NAME || !cupidimage_pdf_apply_extgstate(st, ops[0].str)) {
            st->image_failed = 1;
            cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid or unsupported graphics state");
        }
    } else if (strcmp(op, "Do") == 0) {
        if (n != 1 || ops[0].type != PDF_OPERAND_NAME || !cupidimage_pdf_draw_image(st, ops[0].str)) {
            st->image_failed = 1;
            if (n != 1 || ops[0].type != PDF_OPERAND_NAME)
                cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid image draw operands");
        }
    } else if (strcmp(op, "BT") == 0) {
        cupidimage_pdf_text_begin(st);
    } else if (strcmp(op, "ET") == 0) {
        cupidimage_pdf_text_end(st);
    } else if (strcmp(op, "Tf") == 0) {
        if (n == 2 && ops[0].type == PDF_OPERAND_NAME && pdf_expect_num(&ops[1]) &&
            isfinite(ops[1].num) && fabs(ops[1].num) <= 1e30) {
            st->gs.current_font = st->font_table ? cupidimage_pdf_font_table_find(st->font_table, ops[0].str) : NULL;
            st->gs.font_size_pts = ops[1].num;
        } else st->image_failed = 1;
    } else if (strcmp(op, "Td") == 0 || strcmp(op, "TD") == 0) {
        if (n == 2 && pdf_expect_num(&ops[0]) && pdf_expect_num(&ops[1])) {
            if (op[1] == 'D') st->gs.leading_pts = -ops[1].num;
            cupidimage_pdf_text_move(st, ops[0].num, ops[1].num);
        } else st->image_failed = 1;
    } else if (strcmp(op, "Tm") == 0) {
        int valid = n == 6;
        for (int i=0; valid && i<6; i++)
            valid = pdf_expect_num(&ops[i]) && isfinite(ops[i].num) && fabs(ops[i].num)<=1e30;
        if (!valid) st->image_failed = 1;
        else for (int i=0;i<6;i++) st->text_matrix[i]=st->text_line_matrix[i]=ops[i].num;
    } else if (strcmp(op, "T*") == 0) {
        cupidimage_pdf_text_move(st, 0, -st->gs.leading_pts);
    } else if (strcmp(op, "TL") == 0 || strcmp(op, "Tc") == 0 ||
               strcmp(op, "Tw") == 0 || strcmp(op, "Tz") == 0 || strcmp(op, "Ts") == 0) {
        if (n != 1 || !pdf_expect_num(&ops[0]) || !isfinite(ops[0].num) || fabs(ops[0].num)>1e30)
            st->image_failed = 1;
        else if (op[1]=='L') st->gs.leading_pts=ops[0].num;
        else if (op[1]=='c') st->gs.char_space=ops[0].num;
        else if (op[1]=='w') st->gs.word_space=ops[0].num;
        else if (op[1]=='z') st->gs.horizontal_scale=ops[0].num/100.0;
        else st->gs.text_rise=ops[0].num;
    } else if (strcmp(op, "Tr") == 0) {
        if (n != 1 || !pdf_expect_num(&ops[0]) || (!isfinite(ops[0].num) || ops[0].num<0 || ops[0].num>7 || floor(ops[0].num)!=ops[0].num))
            st->image_failed = 1;
        else st->gs.text_mode=(int)ops[0].num;
    } else if (strcmp(op, "cs") == 0 || strcmp(op, "CS") == 0) {
        pdf_image_colors colors = {0};
        int valid = n == 1 && ops[0].type == PDF_OPERAND_NAME &&
                    cupidimage_pdf_color_space(st, ops[0].str, 0, &colors);
        if (!valid) {
            cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid or unsupported color space");
            st->image_failed = 1;
        } else {
            int stroke = op[0] == 'C';
            pdf_operand initial[4] = {{0}};
            for (int i = 0; i < colors.components; i++) {
                initial[i].type = PDF_OPERAND_NUM;
                initial[i].num = colors.indexed ? colors.palette[i] / 255.0 :
                                 colors.components == 4 && i == 3 ? 1 : 0;
            }
            pdf_set_device_color(st, stroke, colors.components, initial, colors.components);
            if (stroke) st->gs.stroke_space = colors;
            else st->gs.fill_space = colors;
        }
    } else if (strcmp(op, "g") == 0 || strcmp(op, "G") == 0 ||
               strcmp(op, "rg") == 0 || strcmp(op, "RG") == 0 ||
               strcmp(op, "k") == 0 || strcmp(op, "K") == 0 ||
               strcmp(op, "sc") == 0 || strcmp(op, "SC") == 0 ||
               strcmp(op, "scn") == 0 || strcmp(op, "SCN") == 0) {
        int stroke = op[0] >= 'A' && op[0] <= 'Z';
        pdf_image_colors colors = stroke ? st->gs.stroke_space : st->gs.fill_space;
        int direct = op[0] != 's' && op[0] != 'S';
        if (direct) {
            colors.indexed = 0;
            colors.components = op[0] == 'g' || op[0] == 'G' ? 1 :
                                op[0] == 'r' || op[0] == 'R' ? 3 : 4;
        }
        pdf_operand converted[4] = {{0}};
        const pdf_operand *values = ops;
        int count = n, valid = 1;
        if (colors.indexed) {
            valid = n == 1 && pdf_expect_num(ops);
            if (valid) {
                unsigned index = (unsigned)floor(fmin(colors.high, fmax(0, ops[0].num)) + .5);
                for (int i = 0; i < colors.components; i++) {
                    converted[i].type = PDF_OPERAND_NUM;
                    converted[i].num = colors.palette[index * colors.components + i] / 255.0;
                }
                values = converted; count = colors.components;
            }
        }
        if (!valid || !pdf_set_device_color(st, stroke, colors.components, values, count)) {
            cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid color operands");
            st->image_failed = 1;
        } else if (stroke) st->gs.stroke_space = colors;
        else st->gs.fill_space = colors;
    } else if (strcmp(op, "w") == 0) {
        if (n >= 1 && pdf_expect_num(&ops[n - 1])) {
            float w = (float)ops[n - 1].num;
            if (w > 0.0f && w < 1000.0f) {
                st->gs.line_width_pts = w;
            }
        }
    } else if (strcmp(op, "cm") == 0) {
        if (n >= 6 &&
            pdf_expect_num(&ops[n - 6]) && pdf_expect_num(&ops[n - 5]) &&
            pdf_expect_num(&ops[n - 4]) && pdf_expect_num(&ops[n - 3]) &&
            pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            float m[6];
            m[0] = (float)ops[n - 6].num;
            m[1] = (float)ops[n - 5].num;
            m[2] = (float)ops[n - 4].num;
            m[3] = (float)ops[n - 3].num;
            m[4] = (float)ops[n - 2].num;
            m[5] = (float)ops[n - 1].num;
            pdf_matrix_concat(st->gs.ctm, m);
        }
    } else if (strcmp(op, "q") == 0) {
        if (st->gs_stack_len < (int)(sizeof(st->gs_stack) / sizeof(st->gs_stack[0]))) {
            pdf_clip_retain(st->gs.clip);
            st->gs_stack[st->gs_stack_len++] = st->gs;
        } else {
            cupidimage_set_err(st->error, st->error_capacity, "pdf: graphics state stack limit exceeded");
            st->image_failed = 1;
        }
    } else if (strcmp(op, "Q") == 0) {
        if (st->gs_stack_len > 0) {
            pdf_clip_release(st, st->gs.clip);
            st->gs = st->gs_stack[--st->gs_stack_len];
        }
    } else if (strcmp(op, "m") == 0) {
        if (n >= 2 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            pdf_path_move_user(st, (float)ops[n - 2].num, (float)ops[n - 1].num);
        }
    } else if (strcmp(op, "l") == 0) {
        if (n >= 2 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            pdf_path_line_user(st, (float)ops[n - 2].num, (float)ops[n - 1].num);
        }
    } else if (strcmp(op, "c") == 0) {
        if (n >= 6 && pdf_expect_num(&ops[n - 6]) && pdf_expect_num(&ops[n - 5]) &&
            pdf_expect_num(&ops[n - 4]) && pdf_expect_num(&ops[n - 3]) &&
            pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            pdf_path_curve_to(st, (float)ops[n - 6].num, (float)ops[n - 5].num,
                              (float)ops[n - 4].num, (float)ops[n - 3].num,
                              (float)ops[n - 2].num, (float)ops[n - 1].num, 0);
        }
    } else if (strcmp(op, "v") == 0 || strcmp(op, "y") == 0) {
        if (n >= 4 && pdf_expect_num(&ops[n - 4]) && pdf_expect_num(&ops[n - 3]) &&
            pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            float x = (float)ops[n - 2].num, y = (float)ops[n - 1].num;
            float cx = (float)ops[n - 4].num, cy = (float)ops[n - 3].num;
            if (op[0] == 'v') {
                pdf_path_curve_to(st, 0, 0, cx, cy, x, y, 1);
            } else {
                pdf_path_curve_to(st, cx, cy, x, y, x, y, 0);
            }
        }
    } else if (strcmp(op, "h") == 0) {
        pdf_path_close(&st->path);
    } else if (strcmp(op, "re") == 0) {
        if (n >= 4 &&
            pdf_expect_num(&ops[n - 4]) && pdf_expect_num(&ops[n - 3]) &&
            pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            float x = (float)ops[n - 4].num;
            float y = (float)ops[n - 3].num;
            float w = (float)ops[n - 2].num;
            float h = (float)ops[n - 1].num;
            pdf_path_move_user(st, x, y);
            pdf_path_line_user(st, x + w, y);
            pdf_path_line_user(st, x + w, y + h);
            pdf_path_line_user(st, x, y + h);
            pdf_path_close(&st->path);
        }
    } else if (strcmp(op, "n") == 0) {
        pdf_path_end(st);
    } else if (strcmp(op, "S") == 0) {
        cupidimage_pdf_stroke_segments(st, &st->path, NULL);
        pdf_path_end(st);
    } else if (strcmp(op, "s") == 0) {
        pdf_path_close(&st->path);
        cupidimage_pdf_stroke_segments(st, &st->path, NULL);
        pdf_path_end(st);
    } else if (strcmp(op, "f") == 0 || strcmp(op, "F") == 0 || strcmp(op, "f*") == 0) {
        pdf_fill_path(st, strcmp(op, "f*") == 0);
        pdf_path_end(st);
    } else if (strcmp(op, "B") == 0 || strcmp(op, "B*") == 0) {
        pdf_fill_and_stroke(st, strcmp(op, "B*") == 0);
        pdf_path_end(st);
    } else if (strcmp(op, "b") == 0 || strcmp(op, "b*") == 0) {
        pdf_path_close(&st->path);
        pdf_fill_and_stroke(st, strcmp(op, "b*") == 0);
        pdf_path_end(st);
    } else if (strcmp(op, "W") == 0 || strcmp(op, "W*") == 0) {
        st->pending_clip = op[1] == '*' ? 2 : 1;
    } else if (strcmp(op, "Tj") == 0) {
        if (n >= 1 && ops[n - 1].type == PDF_OPERAND_STRING) {
            cupidimage_pdf_render_text_string(st, ops[n - 1].str, ops[n - 1].len);
        }
    } else if (strcmp(op, "TJ") == 0) {
        if (n >= 1 && ops[n - 1].type == PDF_OPERAND_ARRAY) {
            pdf_render_tj_array(st, ops[n - 1].str, ops[n - 1].len);
        }
    } else if (strcmp(op, "'") == 0) {
        if (n == 1 && ops[0].type == PDF_OPERAND_STRING) {
            cupidimage_pdf_text_move(st, 0, -st->gs.leading_pts);
            cupidimage_pdf_render_text_string(st, ops[0].str, ops[0].len);
        } else st->image_failed=1;
    } else if (strcmp(op, "\"") == 0) {
        if (n == 3 && pdf_expect_num(&ops[0]) && pdf_expect_num(&ops[1]) &&
            isfinite(ops[0].num) && isfinite(ops[1].num) &&
            fabs(ops[0].num)<=1e30 && fabs(ops[1].num)<=1e30 && ops[2].type==PDF_OPERAND_STRING) {
            st->gs.word_space=ops[0].num;
            st->gs.char_space=ops[1].num;
            cupidimage_pdf_text_move(st, 0, -st->gs.leading_pts);
            cupidimage_pdf_render_text_string(st, ops[2].str, ops[2].len);
        } else st->image_failed=1;
    }

    if (st->image_failed && st->error && st->error_capacity && !st->error[0])
        cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid or unsupported content operator");
    pdf_clear_operands(ops, count);
}

static int pdf_render_stream(pdf_render_state *st, const unsigned char *stream, size_t stream_len) {
    char *err = st->error;
    size_t errcap = st->error_capacity;
    pdf_operand ops[64];
    int op_count = 0;
    memset(ops, 0, sizeof(ops));

    size_t pos = 0;
    while (pos < stream_len) {
        cupidimage_pdf_skip_ws_and_comments(stream, stream_len, &pos);
        if (pos >= stream_len) {
            break;
        }

        unsigned char c = stream[pos];

        if (c == '(') {
            char *txt = NULL;
            size_t txt_len = 0;
            if (!pdf_parse_literal_string(stream, stream_len, &pos, &txt, &txt_len)) {
                cupidimage_set_err(err, errcap, "pdf: malformed content string");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_STRING;
                ops[op_count].str = txt;
                ops[op_count].len = txt_len;
                op_count++;
            } else {
                free(txt);
            }
            continue;
        }

        if (c == '<' && pos + 1 < stream_len && stream[pos + 1] == '<') {
            char *dictionary = NULL;
            size_t length = 0;
            if (!pdf_parse_dictionary_token(stream, stream_len, &pos, &dictionary, &length)) {
                cupidimage_set_err(err, errcap, "pdf: malformed content dictionary");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_DICTIONARY;
                ops[op_count].str = dictionary;
                ops[op_count].len = length;
                op_count++;
            } else free(dictionary);
            continue;
        }

        if (c == '<' && !(pos + 1 < stream_len && stream[pos + 1] == '<')) {
            char *txt = NULL;
            size_t txt_len = 0;
            if (!pdf_parse_hex_string(stream, stream_len, &pos, &txt, &txt_len)) {
                cupidimage_set_err(err, errcap, "pdf: malformed hex string");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_STRING;
                ops[op_count].str = txt;
                ops[op_count].len = txt_len;
                op_count++;
            } else {
                free(txt);
            }
            continue;
        }

        if (c == '[') {
            char *arr = NULL;
            size_t arr_len = 0;
            if (!pdf_parse_array_token(stream, stream_len, &pos, &arr, &arr_len)) {
                cupidimage_set_err(err, errcap, "pdf: malformed array token");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_ARRAY;
                ops[op_count].str = arr;
                ops[op_count].len = arr_len;
                op_count++;
            } else {
                free(arr);
            }
            continue;
        }

        if (c == '/') {
            char *name = NULL;
            size_t name_len = 0;
            if (!pdf_parse_name_token(stream, stream_len, &pos, &name, &name_len)) {
                cupidimage_set_err(err, errcap, "pdf: malformed name token");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_NAME;
                ops[op_count].str = name;
                ops[op_count].len = name_len;
                op_count++;
            } else {
                free(name);
            }
            continue;
        }

        if (c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9')) {
            double num = 0.0;
            if (pdf_parse_number_token(stream, stream_len, &pos, &num)) {
                if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                    ops[op_count].type = PDF_OPERAND_NUM;
                    ops[op_count].num = num;
                    ops[op_count].str = NULL;
                    ops[op_count].len = 0;
                    op_count++;
                }
                continue;
            }
        }

        size_t start = pos;
        while (pos < stream_len && !cupidimage_pdf_is_ws(stream[pos]) && !cupidimage_pdf_is_delim(stream[pos])) {
            pos++;
        }
        if (pos > start) {
            size_t tok_len = pos - start;
            char opbuf[16];
            if (tok_len >= sizeof(opbuf)) {
                tok_len = sizeof(opbuf) - 1;
            }
            memcpy(opbuf, &stream[start], tok_len);
            opbuf[tok_len] = '\0';
            if (!strcmp(opbuf, "BI")) {
                int ok = op_count == 0 && (!st->glyph_active || st->glyph_seen_metrics) && cupidimage_pdf_draw_inline_image(st, stream, stream_len, &pos);
                pdf_clear_operands(ops, &op_count);
                if (!ok) {
                    cupidimage_set_err(err, errcap, "pdf: invalid or unsupported inline image");
                    return 0;
                }
                continue;
            }
            if (!strcmp(opbuf, "ID") || !strcmp(opbuf, "EI")) {
                pdf_clear_operands(ops, &op_count);
                cupidimage_set_err(err, errcap, "pdf: unmatched inline image delimiter");
                return 0;
            }
            pdf_execute_operator(st, opbuf, ops, &op_count);
            if (st->image_failed) return 0;
            continue;
        }

        pos++;
    }

    pdf_clear_operands(ops, &op_count);
    return 1;
}

static void pdf_double_concat(double m[6],const double n[6]) {
    double r[6]={m[0]*n[0]+m[2]*n[1],m[1]*n[0]+m[3]*n[1],
                 m[0]*n[2]+m[2]*n[3],m[1]*n[2]+m[3]*n[3],
                 m[0]*n[4]+m[2]*n[5]+m[4],m[1]*n[4]+m[3]*n[5]+m[5]};
    memcpy(m,r,sizeof(r));
}

int cupidimage_pdf_draw_type3_glyph(pdf_render_state *st,const pdf_font_map *font,unsigned code) {
    const pdf_type3_font *type=font->type3;
    int object=type->glyphs[code],ok=0;
    if(!object || st->gs.text_mode==3 || st->gs.font_size_pts==0 || st->gs.horizontal_scale==0)return 1;
    if(st->glyph_depth==16 || !st->form_budget->glyph_calls_left)return 0;
    st->form_budget->glyph_calls_left--;
    pdf_object_view program={0};unsigned char *decoded=NULL;
    pdf_render_state *child=NULL;
    pdf_font_table fonts;cupidimage_pdf_font_table_init(&fonts);
    const unsigned char *stream;size_t length,pos;
    if(!cupidimage_pdf_get_object_content(st->document,st->document_size,object,&program) ||
       !cupidimage_pdf_object_stream(st->document,st->document_size,&program,&stream,&length) ||
       !cupidimage_pdf_decode_stream(&program,stream,length,&decoded,&length) ||
       length>st->form_budget->bytes_left)goto done;
    st->form_budget->bytes_left-=length;
    child=malloc(sizeof(*child));if(!child)goto done;
    *child=*st;pdf_clip_retain(child->gs.clip);
    child->gs_stack_len=0;child->pending_clip=0;child->image_failed=0;
    child->text_clip=NULL;child->text_winding=NULL;child->text_winding_bytes=0;
    child->in_text=0;child->glyph_active=1;child->glyph_seen_metrics=0;child->glyph_uncolored=st->glyph_uncolored;
    child->glyph_expected_width=type->widths[code];
    child->glyph_depth++;
    pdf_path_clear(&child->path);
    double m[6];for(int i=0;i<6;i++)m[i]=st->gs.ctm[i];
    double scale[6]={st->gs.font_size_pts*st->gs.horizontal_scale,0,0,st->gs.font_size_pts,0,st->gs.text_rise};
    pdf_double_concat(m,st->text_matrix);pdf_double_concat(m,scale);pdf_double_concat(m,type->matrix);
    for(int i=0;i<6;i++) {if(!isfinite(m[i]) || fabs(m[i])>1e30)goto done;child->gs.ctm[i]=(float)m[i];}
    if(cupidimage_pdf_page_value(&type->dictionary,"Resources",&pos)) {
        child->resource_override=&type->dictionary;
        if(!cupidimage_pdf_collect_resource_fonts(st->document,st->document_size,&type->dictionary,&fonts,st->error,st->error_capacity))goto done;
        child->font_table=&fonts;
    }
    ok=pdf_render_stream(child,decoded,length) && child->glyph_seen_metrics;
done:
    if(child)pdf_render_state_release(child);
    free(child);free(decoded);cupidimage_pdf_object_view_release(&program);
    cupidimage_pdf_font_table_free(&fonts);
    if(!ok && st->error && st->error_capacity && !st->error[0])cupidimage_set_err(st->error,st->error_capacity,"pdf: invalid, recursive, or unsupported Type 3 glyph");
    return ok;
}

int cupidimage_pdf_draw_form(pdf_render_state *st, int object, const pdf_object_view *form) {
    size_t pos, length;
    int type = 1, ok = 0;
    const unsigned char *stream;
    unsigned char *inflated = NULL;
    pdf_render_state *child = NULL;
    pdf_font_table fonts;
    cupidimage_pdf_font_table_init(&fonts);
    if (st->form_depth >= 16 || !st->form_budget->calls_left) goto done;
    st->form_budget->calls_left--;
    for (int i = 0; i < st->form_depth; i++) {
        if (st->form_objects[i] == object) goto done;
    }
    if ((cupidimage_pdf_page_value(form, "FormType", &pos) &&
         (!cupidimage_pdf_image_integer(st, form, "FormType", &type) || type != 1)) ||
        cupidimage_pdf_page_value(form, "Group", &pos) || cupidimage_pdf_page_value(form, "Ref", &pos)) goto done;
    child = (pdf_render_state *)malloc(sizeof(*child));
    if (!child) goto done;
    *child = *st;
    pdf_clip_retain(child->gs.clip);
    child->gs_stack_len = 0;
    child->text_clip = NULL;
    child->text_winding = NULL;
    child->text_winding_bytes = 0;
    child->pending_clip = 0;
    child->image_failed = 0;
    child->glyph_active = 0;
    pdf_path_clear(&child->path);
    pdf_form_clip *clip = &child->form_clips[st->form_depth];
    if (!cupidimage_pdf_page_value(form, "BBox", &pos) ||
        !cupidimage_pdf_page_rectangle(st->document, st->document_size, form, pos, clip->box)) goto done;
    if (cupidimage_pdf_page_value(form, "Matrix", &pos)) {
        pdf_object_view indirect = {0};
        const pdf_object_view *matrix = form;
        int matrix_obj;
        if (pos < form->len && form->data[pos] != '[') {
            if (!cupidimage_pdf_page_reference(form, pos, &matrix_obj) ||
                !cupidimage_pdf_get_object_content(st->document, st->document_size, matrix_obj, &indirect)) goto done;
            matrix = &indirect;
            pos = 0;
            cupidimage_pdf_skip_ws_and_comments(matrix->data, matrix->len, &pos);
        }
        float m[6];
        int valid = pos < matrix->len && matrix->data[pos++] == '[';
        for (int i = 0; valid && i < 6; i++) {
            double number;
            cupidimage_pdf_skip_ws_and_comments(matrix->data, matrix->len, &pos);
            valid = cupidimage_pdf_parse_pdf_number(matrix->data, matrix->len, &pos, &number) && fabs(number) <= 1e30;
            if (valid) m[i] = (float)number;
        }
        cupidimage_pdf_skip_ws_and_comments(matrix->data, matrix->len, &pos);
        valid = valid && pos < matrix->len && matrix->data[pos] == ']';
        cupidimage_pdf_object_view_release(&indirect);
        if (!valid) goto done;
        pdf_matrix_concat(child->gs.ctm, m);
    }
    const float *m = child->gs.ctm;
    for (int i = 0; i < 6; i++) if (!isfinite(m[i])) goto done;
    double det = (double)m[0] * m[3] - (double)m[1] * m[2];
    if (det == 0) { ok = 1; goto done; }
    double *inv = clip->inverse;
    inv[0] = m[3] / det; inv[1] = -m[1] / det;
    inv[2] = -m[2] / det; inv[3] = m[0] / det;
    inv[4] = -(inv[0] * m[4] + inv[2] * m[5]);
    inv[5] = -(inv[1] * m[4] + inv[3] * m[5]);
    child->form_objects[child->form_depth++] = object;
    if (cupidimage_pdf_page_value(form, "Resources", &pos)) {
        pdf_object_view resources = {0};
        int valid = cupidimage_pdf_image_dictionary(st, form, "Resources", &resources);
        cupidimage_pdf_object_view_release(&resources);
        if (!valid) goto done;
        child->page_object = object;
        child->resource_override = form;
        if (!cupidimage_pdf_collect_page_fonts(st->document, st->document_size, object, &fonts, st->error, st->error_capacity)) {
            cupidimage_pdf_font_table_free(&fonts);
            cupidimage_pdf_font_table_init(&fonts);
        }
        child->font_table = &fonts;
    }
    if (!cupidimage_pdf_image_stream(st, form, &stream, &length) ||
        !cupidimage_pdf_decode_stream(form, stream, length, &inflated, &length)) goto done;
    stream = inflated;
    if (length > st->form_budget->bytes_left) goto done;
    st->form_budget->bytes_left -= length;
    ok = pdf_render_stream(child, stream, length);
done:
    cupidimage_pdf_font_table_free(&fonts);
    free(inflated);
    if (child) pdf_render_state_release(child);
    free(child);
    if (!ok) cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid, recursive, or unsupported Form XObject");
    return ok;
}

int cupidimage_pdf_render_content_stream(const unsigned char *stream, size_t stream_len,
                                     const unsigned char *document, size_t document_size, int page_object,
                                     const pdf_font_table *font_table,
                                     cupidimage_image *img, const pdf_page_geometry *geometry,
                                     char *err, size_t errcap) {
    pdf_render_state st = {0};
    pdf_clip_budget clip_budget = {0, 256u * 1024u * 1024u, 4096};
    st.clip_budget = &clip_budget;
    pdf_form_budget form_budget = {64u * 1024u * 1024u, 4096, 128u * 1024u * 1024u, 65536};
    st.form_budget = &form_budget;
    st.img = img;
    st.document = document;
    st.document_size = document_size;
    st.page_object = page_object;
    st.image_failed = 0;
    st.error = err;
    st.error_capacity = errcap;
    st.page_x0_pts = (float)geometry->x0;
    st.page_top_pts = (float)geometry->y1;
    cupidimage_pdf_text_begin(&st);
    st.gs.font_size_pts = 12.0;
    st.gs.horizontal_scale = 1.0;
    pdf_matrix_identity(st.gs.ctm);
    st.gs.fill_r = 0;
    st.gs.fill_g = 0;
    st.gs.fill_b = 0;
    st.gs.stroke_r = 0;
    st.gs.stroke_g = 0;
    st.gs.stroke_b = 0;
    st.gs.line_width_pts = 1.0f;
    st.gs.fill_alpha = st.gs.stroke_alpha = 1;
    st.gs.fill_space.components = st.gs.stroke_space.components = 1;
    st.gs_stack_len = 0;
    pdf_path_clear(&st.path);
    st.in_text = 0;
    st.font_table = font_table;
    st.gs.current_font = NULL;

    int ok = pdf_render_stream(&st, stream, stream_len);
    pdf_render_state_release(&st);
    return ok;
}
