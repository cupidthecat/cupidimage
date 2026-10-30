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

static void svg_text_path_clear(svg_text_path_state *tp);
static int svg_text_path_point(const svg_text_path_state *tp, float dist, float *out_x,
                               float *out_y, float *out_angle);
static float svg_textpath_measure_range(const char *start, const char *end, int preserve,
                                        const svg_style *style);
static const uint8_t *svg_get_glyph(unsigned char c);
static int svg_utf8_decode_one(const char *s, uint32_t *cp, int *len);
static const uint8_t *svg_get_glyph_codepoint(uint32_t cp);
static float svg_text_advance(const svg_style *style, uint32_t cp);
static float svg_text_baseline_offset(const svg_style *style);
static float svg_measure_line(const svg_style *style, const char *text, size_t len);
static void svg_draw_glyph(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                           const svg_bbox *bbox, const uint8_t *glyph, float x, float y,
                           float scale_x, float scale_y);
static void svg_render_text_run(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                                const char *text, float *pen_x, float *pen_y, svg_bbox *bbox_out);
static void svg_render_text_run_path(svg_render_ctx *ctx, const svg_style *style,
                                     const svg_matrix *m, const char *text, svg_text_path_state *tp,
                                     svg_bbox *bbox_out);

static const uint8_t svg_font8x8_basic[128][8] = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                                  {0x7E, 0x81, 0xA5, 0x81, 0xBD, 0x99, 0x81, 0x7E},
                                                  {0x7E, 0xFF, 0xDB, 0xFF, 0xC3, 0xE7, 0xFF, 0x7E},
                                                  {0x6C, 0xFE, 0xFE, 0xFE, 0x7C, 0x38, 0x10, 0x00},
                                                  {0x10, 0x38, 0x7C, 0xFE, 0x7C, 0x38, 0x10, 0x00},
                                                  {0x38, 0x7C, 0x38, 0xFE, 0xFE, 0xD6, 0x10, 0x38},
                                                  {0x10, 0x38, 0x7C, 0xFE, 0xFE, 0x7C, 0x10, 0x38},
                                                  {0x00, 0x00, 0x18, 0x3C, 0x3C, 0x18, 0x00, 0x00},
                                                  {0xFF, 0xFF, 0xE7, 0xC3, 0xC3, 0xE7, 0xFF, 0xFF},
                                                  {0x00, 0x3C, 0x66, 0x42, 0x42, 0x66, 0x3C, 0x00},
                                                  {0xFF, 0xC3, 0x99, 0xBD, 0xBD, 0x99, 0xC3, 0xFF},
                                                  {0x0F, 0x07, 0x0F, 0x7D, 0xCC, 0xCC, 0xCC, 0x78},
                                                  {0x3C, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x7E, 0x18},
                                                  {0x3F, 0x33, 0x3F, 0x30, 0x30, 0x70, 0xF0, 0xE0},
                                                  {0x7F, 0x63, 0x7F, 0x63, 0x63, 0x67, 0xE6, 0xC0},
                                                  {0x99, 0x5A, 0x3C, 0xE7, 0xE7, 0x3C, 0x5A, 0x99},
                                                  {0x80, 0xE0, 0xF8, 0xFE, 0xF8, 0xE0, 0x80, 0x00},
                                                  {0x02, 0x0E, 0x3E, 0xFE, 0x3E, 0x0E, 0x02, 0x00},
                                                  {0x18, 0x3C, 0x7E, 0x18, 0x18, 0x7E, 0x3C, 0x18},
                                                  {0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x66, 0x00},
                                                  {0x7F, 0xDB, 0xDB, 0x7B, 0x1B, 0x1B, 0x1B, 0x00},
                                                  {0x3E, 0x63, 0x38, 0x6C, 0x6C, 0x38, 0xCC, 0x7F},
                                                  {0x00, 0x00, 0x00, 0x00, 0x7E, 0x7E, 0x7E, 0x00},
                                                  {0x18, 0x3C, 0x7E, 0x18, 0x7E, 0x3C, 0x18, 0xFF},
                                                  {0x18, 0x3C, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x00},
                                                  {0x18, 0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18, 0x00},
                                                  {0x00, 0x18, 0x0C, 0xFE, 0x0C, 0x18, 0x00, 0x00},
                                                  {0x00, 0x30, 0x60, 0xFE, 0x60, 0x30, 0x00, 0x00},
                                                  {0x00, 0x00, 0xC0, 0xC0, 0xC0, 0xFE, 0x00, 0x00},
                                                  {0x00, 0x24, 0x66, 0xFF, 0x66, 0x24, 0x00, 0x00},
                                                  {0x00, 0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x00, 0x00},
                                                  {0x00, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00, 0x00},
                                                  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                                  {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
                                                  {0x36, 0x36, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00},
                                                  {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
                                                  {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},
                                                  {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},
                                                  {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},
                                                  {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
                                                  {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},
                                                  {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},
                                                  {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
                                                  {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},
                                                  {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},
                                                  {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},
                                                  {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},
                                                  {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},
                                                  {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},
                                                  {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},
                                                  {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},
                                                  {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},
                                                  {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},
                                                  {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},
                                                  {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},
                                                  {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},
                                                  {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},
                                                  {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},
                                                  {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},
                                                  {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},
                                                  {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},
                                                  {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},
                                                  {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},
                                                  {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},
                                                  {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},
                                                  {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},
                                                  {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},
                                                  {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},
                                                  {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},
                                                  {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},
                                                  {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},
                                                  {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},
                                                  {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},
                                                  {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
                                                  {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},
                                                  {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},
                                                  {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},
                                                  {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},
                                                  {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},
                                                  {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},
                                                  {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},
                                                  {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},
                                                  {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},
                                                  {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},
                                                  {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
                                                  {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},
                                                  {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
                                                  {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
                                                  {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},
                                                  {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},
                                                  {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},
                                                  {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},
                                                  {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},
                                                  {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},
                                                  {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},
                                                  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
                                                  {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
                                                  {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},
                                                  {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},
                                                  {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},
                                                  {0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00},
                                                  {0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00},
                                                  {0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00},
                                                  {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},
                                                  {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},
                                                  {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
                                                  {0x18, 0x00, 0x1C, 0x18, 0x18, 0x18, 0x1B, 0x0E},
                                                  {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},
                                                  {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
                                                  {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
                                                  {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},
                                                  {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},
                                                  {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},
                                                  {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},
                                                  {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},
                                                  {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},
                                                  {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},
                                                  {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},
                                                  {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
                                                  {0x00, 0x00, 0x63, 0x63, 0x6B, 0x7F, 0x36, 0x00},
                                                  {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},
                                                  {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},
                                                  {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},
                                                  {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},
                                                  {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},
                                                  {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},
                                                  {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                                  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

static void svg_text_path_clear(svg_text_path_state *tp) {
    if (!tp) {
        return;
    }
    free(tp->seg_lengths);
    tp->seg_lengths = NULL;
    tp->seg_count = 0;
    tp->total_len = 0.0f;
    tp->pos = 0.0f;
    tp->active = 0;
    cupidimage_svg_segments_free(&tp->segs);
}

static int svg_text_path_point(const svg_text_path_state *tp, float dist, float *out_x,
                               float *out_y, float *out_angle) {
    if (!tp || !out_x || !out_y || !out_angle || tp->seg_count == 0 || tp->total_len <= 0.0f) {
        return 0;
    }
    if (dist < 0.0f || dist > tp->total_len) {
        return 0;
    }
    float acc = 0.0f;
    for (size_t i = 0; i < tp->seg_count; i++) {
        float len = tp->seg_lengths[i];
        if (len <= 1e-6f) {
            acc += len;
            continue;
        }
        if (dist <= acc + len) {
            float t = (dist - acc) / len;
            float x0 = tp->segs.stroke[i].x0;
            float y0 = tp->segs.stroke[i].y0;
            float x1 = tp->segs.stroke[i].x1;
            float y1 = tp->segs.stroke[i].y1;
            float dx = x1 - x0;
            float dy = y1 - y0;
            *out_x = x0 + dx * t;
            *out_y = y0 + dy * t;
            *out_angle = atan2f(dy, dx);
            return 1;
        }
        acc += len;
    }
    return 0;
}

static float svg_textpath_measure_range(const char *start, const char *end, int preserve,
                                        const svg_style *style) {
    if (!start || !end || !style || end <= start) {
        return 0.0f;
    }
    size_t cap = (size_t)(end - start) + 1u;
    char *raw = (char *)malloc(cap);
    if (!raw) {
        return 0.0f;
    }
    size_t w = 0;
    const char *p = start;
    while (p < end && *p) {
        if (*p == '<') {
            const char *gt = strchr(p, '>');
            if (!gt || gt >= end) {
                break;
            }
            p = gt + 1;
            continue;
        }
        raw[w++] = *p++;
    }
    raw[w] = '\0';
    float width = 0.0f;
    char *norm = cupidimage_svg_text_normalize(raw, preserve);
    if (norm && *norm) {
        width = svg_measure_line(style, norm, strlen(norm));
    }
    free(norm);
    free(raw);
    return width;
}

static const uint8_t *svg_get_glyph(unsigned char c) {
    if (c < 128) {
        return svg_font8x8_basic[c];
    }
    return svg_font8x8_basic['?'];
}

static int svg_utf8_decode_one(const char *s, uint32_t *cp, int *len) {
    if (!s || !*s || !cp || !len) {
        return 0;
    }
    unsigned char c0 = (unsigned char)s[0];
    if (c0 < 0x80) {
        *cp = c0;
        *len = 1;
        return 1;
    }
    if ((c0 & 0xE0u) == 0xC0u) {
        unsigned char c1 = (unsigned char)s[1];
        if ((c1 & 0xC0u) != 0x80u)
            return 0;
        *cp = ((uint32_t)(c0 & 0x1Fu) << 6) | (uint32_t)(c1 & 0x3Fu);
        *len = 2;
        return 1;
    }
    if ((c0 & 0xF0u) == 0xE0u) {
        unsigned char c1 = (unsigned char)s[1];
        unsigned char c2 = (unsigned char)s[2];
        if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u)
            return 0;
        *cp =
            ((uint32_t)(c0 & 0x0Fu) << 12) | ((uint32_t)(c1 & 0x3Fu) << 6) | (uint32_t)(c2 & 0x3Fu);
        *len = 3;
        return 1;
    }
    if ((c0 & 0xF8u) == 0xF0u) {
        unsigned char c1 = (unsigned char)s[1];
        unsigned char c2 = (unsigned char)s[2];
        unsigned char c3 = (unsigned char)s[3];
        if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u || (c3 & 0xC0u) != 0x80u)
            return 0;
        *cp = ((uint32_t)(c0 & 0x07u) << 18) | ((uint32_t)(c1 & 0x3Fu) << 12) |
              ((uint32_t)(c2 & 0x3Fu) << 6) | (uint32_t)(c3 & 0x3Fu);
        *len = 4;
        return 1;
    }
    return 0;
}

static const uint8_t svg_glyph_bullet[8] = {0x00, 0x00, 0x18, 0x3C, 0x3C, 0x18, 0x00, 0x00};

static const uint8_t *svg_get_glyph_codepoint(uint32_t cp) {
    if (cp < 128u) {
        return svg_get_glyph((unsigned char)cp);
    }
    if (cp == 0x2022u || cp == 0x00B7u) {
        return svg_glyph_bullet;
    }
    return svg_font8x8_basic['?'];
}

static float svg_text_advance(const svg_style *style, uint32_t cp) {
    float base_w = style->font_size * 0.6f;
    float adv = base_w + style->letter_spacing;
    if (cp == (uint32_t)' ') {
        adv += style->word_spacing;
    }
    return adv;
}

static float svg_text_baseline_offset(const svg_style *style) {
    if (!style) {
        return 0.0f;
    }
    if (style->dominant_baseline == 1) {
        return style->font_size * 0.5f;
    }
    if (style->dominant_baseline == 2) {
        return style->font_size;
    }
    return 0.0f;
}

static float svg_measure_line(const svg_style *style, const char *text, size_t len) {
    if (!style || !text) {
        return 0.0f;
    }
    float width = 0.0f;
    size_t i = 0;
    while (i < len) {
        uint32_t cp = 0;
        int n = 0;
        if (!svg_utf8_decode_one(text + i, &cp, &n) || n <= 0 || i + (size_t)n > len) {
            i++;
            continue;
        }
        i += (size_t)n;
        if (cp == (uint32_t)'\n') {
            break;
        }
        if (cp < 32u) {
            continue;
        }
        width += svg_text_advance(style, cp);
    }
    return width;
}

static void svg_draw_glyph(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                           const svg_bbox *bbox, const uint8_t *glyph, float x, float y,
                           float scale_x, float scale_y) {
    if (!ctx || !style || !m || !glyph || !bbox) {
        return;
    }
    if (style->fill_type == SVG_PAINT_NONE) {
        return;
    }
    svg_matrix gm;
    svg_matrix_identity(&gm);
    svg_matrix_scale(&gm, scale_x, scale_y);
    if (style->italic) {
        svg_matrix_skewx(&gm, -12.0f);
    }
    svg_matrix_translate(&gm, x, y - style->font_size);
    svg_matrix total;
    svg_matrix_multiply(m, &gm, &total);
    float width = 8.0f * scale_x;
    float height = 8.0f * scale_y;
    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    cupidimage_svg_transform_bounds(&total, 0.0f, 0.0f, width, height, &dev_minx, &dev_miny,
                                    &dev_maxx, &dev_maxy);
    if (dev_maxx < 0.0f || dev_maxy < 0.0f || dev_minx > (float)ctx->width ||
        dev_miny > (float)ctx->height) {
        return;
    }
    svg_matrix inv;
    if (!svg_matrix_invert(&total, &inv)) {
        return;
    }
    uint32_t ss = ctx->ss;
    int sx0 = cupidimage_svg_raster_bound(dev_minx * (float)ss, 0);
    int sy0 = cupidimage_svg_raster_bound(dev_miny * (float)ss, 0);
    int sx1 = cupidimage_svg_raster_bound(dev_maxx * (float)ss, 1);
    int sy1 = cupidimage_svg_raster_bound(dev_maxy * (float)ss, 1);
    if (sx0 < 0)
        sx0 = 0;
    if (sy0 < 0)
        sy0 = 0;
    int max_sx = (int)(ctx->width * ss) - 1;
    int max_sy = (int)(ctx->height * ss) - 1;
    if (sx1 > max_sx)
        sx1 = max_sx;
    if (sy1 > max_sy)
        sy1 = max_sy;
    int fill_is_color = style->fill_type == SVG_PAINT_COLOR;
    uint8_t fill_pr = 0, fill_pg = 0, fill_pb = 0, fill_pa = 0;
    if (fill_is_color) {
        cupidimage_svg_prepare_const_paint(style, 0, &fill_pr, &fill_pg, &fill_pb, &fill_pa);
    }
    for (int sy = sy0; sy <= sy1; sy++) {
        float dy = ((float)sy + 0.5f) / (float)ss;
        for (int sx = sx0; sx <= sx1; sx++) {
            float dx = ((float)sx + 0.5f) / (float)ss;
            float gx = dx;
            float gy = dy;
            svg_matrix_transform_point(&inv, &gx, &gy);
            if (gx < 0.0f || gy < 0.0f || gx >= 8.0f || gy >= 8.0f) {
                continue;
            }
            int ix = (int)floorf(gx);
            int iy = (int)floorf(gy);
            if (ix < 0 || ix >= 8 || iy < 0 || iy >= 8) {
                continue;
            }
            if ((glyph[iy] & (1u << ix)) == 0) {
                continue;
            }
            uint8_t *dst =
                ctx->hi_rgba + ((size_t)sy * (size_t)(ctx->width * ss) + (size_t)sx) * 4u;
            uint8_t mask_alpha =
                cupidimage_svg_sample_mask_alpha((svg_style *)style, ctx, bbox, sx, sy);
            if (mask_alpha == 0) {
                continue;
            }
            if (fill_is_color) {
                if (fill_pa > 0) {
                    uint8_t pr = fill_pr;
                    uint8_t pg = fill_pg;
                    uint8_t pb = fill_pb;
                    uint8_t pa = fill_pa;
                    if (mask_alpha < 255) {
                        pr = (uint8_t)((pr * mask_alpha) / 255u);
                        pg = (uint8_t)((pg * mask_alpha) / 255u);
                        pb = (uint8_t)((pb * mask_alpha) / 255u);
                        pa = (uint8_t)((pa * mask_alpha) / 255u);
                    }
                    if (pa > 0) {
                        cupidimage_svg_blend_premul(dst, pr, pg, pb, pa);
                    }
                }
            } else {
                float ux = gx;
                float uy = gy;
                svg_matrix_transform_point(&gm, &ux, &uy);
                uint8_t pr, pg, pb, pa;
                if (cupidimage_svg_sample_paint(style, ctx, 0, bbox, ux, uy, &pr, &pg, &pb, &pa)) {
                    if (mask_alpha < 255) {
                        pr = (uint8_t)((pr * mask_alpha) / 255u);
                        pg = (uint8_t)((pg * mask_alpha) / 255u);
                        pb = (uint8_t)((pb * mask_alpha) / 255u);
                        pa = (uint8_t)((pa * mask_alpha) / 255u);
                    }
                    if (pa > 0) {
                        cupidimage_svg_blend_premul(dst, pr, pg, pb, pa);
                    }
                }
            }
        }
    }
}

static void svg_render_text_run(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                                const char *text, float *pen_x, float *pen_y, svg_bbox *bbox_out) {
    if (!ctx || !style || !m || !text || !pen_x || !pen_y) {
        return;
    }
    float line_height = style->font_size * 1.2f;
    float scale_x = style->font_size * 0.6f / 8.0f;
    float scale_y = style->font_size / 8.0f;
    float baseline_off = svg_text_baseline_offset(style);
    const char *p = text;
    while (*p) {
        const char *line_start = p;
        while (*p && *p != '\n') {
            p++;
        }
        size_t line_len = (size_t)(p - line_start);
        float line_width = svg_measure_line(style, line_start, line_len);
        float line_x = *pen_x;
        if (style->text_anchor == 1) {
            line_x -= line_width * 0.5f;
        } else if (style->text_anchor == 2) {
            line_x -= line_width;
        }
        float line_y = *pen_y + baseline_off;
        if (bbox_out) {
            cupidimage_svg_bbox_add_rect(bbox_out, line_x, line_y - style->font_size, line_width,
                                         style->font_size);
        }
        svg_bbox bbox;
        bbox.x = line_x;
        bbox.y = line_y - style->font_size;
        bbox.w = line_width;
        bbox.h = style->font_size;
        float x = line_x;
        size_t i = 0;
        while (i < line_len) {
            uint32_t cp = 0;
            int n = 0;
            if (!svg_utf8_decode_one(line_start + i, &cp, &n) || n <= 0 ||
                i + (size_t)n > line_len) {
                i++;
                continue;
            }
            i += (size_t)n;
            if (cp < 32u) {
                continue;
            }
            const uint8_t *glyph = svg_get_glyph_codepoint(cp);
            svg_draw_glyph(ctx, style, m, &bbox, glyph, x, line_y, scale_x, scale_y);
            if (style->bold) {
                svg_draw_glyph(ctx, style, m, &bbox, glyph, x + scale_x * 0.8f, line_y, scale_x,
                               scale_y);
            }
            x += svg_text_advance(style, cp);
        }
        *pen_x = x;
        if (*p == '\n') {
            *pen_y += line_height;
            *pen_x = line_x;
            p++;
        }
    }
}

static void svg_render_text_run_path(svg_render_ctx *ctx, const svg_style *style,
                                     const svg_matrix *m, const char *text, svg_text_path_state *tp,
                                     svg_bbox *bbox_out) {
    if (!ctx || !style || !m || !text || !tp || !tp->active) {
        return;
    }
    if (bbox_out) {
        svg_bbox temp = tp->bbox;
        cupidimage_svg_bbox_expand(&temp, style->font_size * 0.5f);
        cupidimage_svg_bbox_add_rect(bbox_out, temp.x, temp.y, temp.w, temp.h);
    }
    float scale_x = style->font_size * 0.6f / 8.0f;
    float scale_y = style->font_size / 8.0f;
    float baseline_off = svg_text_baseline_offset(style);
    const char *p = text;
    while (*p) {
        uint32_t cp = 0;
        int n = 0;
        if (!svg_utf8_decode_one(p, &cp, &n) || n <= 0) {
            p++;
            continue;
        }
        p += n;
        if (cp == (uint32_t)'\n') {
            break;
        }
        if (cp < 32u) {
            continue;
        }
        float adv = svg_text_advance(style, cp);
        float sample_pos = tp->pos + adv * 0.5f;
        float gx = 0.0f;
        float gy = 0.0f;
        float ang = 0.0f;
        if (!svg_text_path_point(tp, sample_pos, &gx, &gy, &ang)) {
            return;
        }
        svg_matrix local;
        svg_matrix_identity(&local);
        svg_matrix_rotate(&local, ang * (180.0f / (float)M_PI));
        svg_matrix_translate(&local, gx, gy);
        svg_matrix glyph_m;
        svg_matrix_multiply(m, &local, &glyph_m);
        const uint8_t *glyph = svg_get_glyph_codepoint(cp);
        svg_draw_glyph(ctx, style, &glyph_m, &tp->bbox, glyph, 0.0f,
                       style->font_size + baseline_off, scale_x, scale_y);
        if (style->bold) {
            svg_draw_glyph(ctx, style, &glyph_m, &tp->bbox, glyph, scale_x * 0.8f,
                           style->font_size + baseline_off, scale_x, scale_y);
        }
        tp->pos += adv;
    }
}

void cupidimage_svg_render_text_element(svg_render_ctx *ctx, const svg_style *style,
                                        const svg_matrix *m, const svg_tag *text_tag, char **p,
                                        const svg_defs *defs, const svg_css *css,
                                        const svg_elem_info *stack_info, int stack_depth,
                                        float base_len, float dpi, svg_bbox *out_bbox) {
    if (!ctx || !style || !m || !text_tag || !p || !defs || !css || !stack_info) {
        return;
    }
    if (out_bbox) {
        cupidimage_svg_bbox_init(out_bbox);
    }
    float pen_x = 0.0f;
    float pen_y = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    if (!cupidimage_svg_parse_length_attr(text_tag, "x", ctx->vb_w, dpi, &pen_x)) {
        pen_x = 0.0f;
    }
    if (!cupidimage_svg_parse_length_attr(text_tag, "y", ctx->vb_h, dpi, &pen_y)) {
        pen_y = 0.0f;
    }
    cupidimage_svg_parse_length_attr(text_tag, "dx", ctx->vb_w, dpi, &dx);
    cupidimage_svg_parse_length_attr(text_tag, "dy", ctx->vb_h, dpi, &dy);
    pen_x += dx;
    pen_y += dy;
    int preserve = cupidimage_svg_xml_space_preserve(text_tag, 0);

    svg_text_state state_stack[SVG_MAX_DEPTH];
    int tdepth = 0;
    svg_style cur_style = *style;
    svg_matrix cur_m = *m;
    svg_text_path_state path_stack[SVG_MAX_DEPTH];
    int path_depth = 0;
    svg_text_path_state *cur_path = NULL;

    while (**p) {
        char *lt = strchr(*p, '<');
        if (!lt) {
            lt = *p + strlen(*p);
        }
        if (lt > *p) {
            char *seg = svg_strndup(*p, (size_t)(lt - *p));
            if (seg) {
                char *norm = cupidimage_svg_text_normalize(seg, preserve);
                if (norm && *norm) {
                    if (cur_path && cur_path->active) {
                        svg_render_text_run_path(ctx, &cur_style, &cur_m, norm, cur_path, out_bbox);
                    } else {
                        svg_render_text_run(ctx, &cur_style, &cur_m, norm, &pen_x, &pen_y,
                                            out_bbox);
                    }
                }
                free(norm);
                free(seg);
            }
        }
        if (!*lt) {
            *p = lt;
            break;
        }
        if (strncmp(lt, "<![CDATA[", 9) == 0) {
            char *cend = strstr(lt + 9, "]]>");
            if (!cend) {
                *p = lt + strlen(lt);
                break;
            }
            if (cend > lt + 9) {
                char *seg = svg_strndup(lt + 9, (size_t)(cend - (lt + 9)));
                if (seg) {
                    char *norm = cupidimage_svg_text_normalize(seg, preserve);
                    if (norm && *norm) {
                        if (cur_path && cur_path->active) {
                            svg_render_text_run_path(ctx, &cur_style, &cur_m, norm, cur_path,
                                                     out_bbox);
                        } else {
                            svg_render_text_run(ctx, &cur_style, &cur_m, norm, &pen_x, &pen_y,
                                                out_bbox);
                        }
                    }
                    free(norm);
                    free(seg);
                }
            }
            *p = cend + 3;
            continue;
        }
        char *tmp = lt;
        svg_tag inner;
        if (!cupidimage_svg_next_tag(&tmp, &inner)) {
            *p = tmp;
            break;
        }
        const char *lname = svg_local_name(inner.name);
        if (inner.is_end && strcmp(lname, "text") == 0) {
            *p = tmp;
            break;
        }
        if (!inner.is_end && strcmp(lname, "tspan") == 0) {
            if (tdepth < SVG_MAX_DEPTH) {
                state_stack[tdepth].style = cur_style;
                state_stack[tdepth].transform = cur_m;
                state_stack[tdepth].preserve = preserve;
                tdepth++;
            }
            svg_style next_style = cur_style;
            cupidimage_svg_apply_presentation_attrs(&next_style, &inner, base_len, dpi, defs);
            svg_elem_info tspan_info;
            cupidimage_svg_elem_info_from_tag(&tspan_info, lname, &inner);
            svg_elem_info tmp_stack[SVG_MAX_DEPTH];
            int tmp_depth = stack_depth < SVG_MAX_DEPTH ? stack_depth : SVG_MAX_DEPTH;
            for (int i = 0; i < tmp_depth; i++) {
                tmp_stack[i] = stack_info[i];
            }
            if (tmp_depth < SVG_MAX_DEPTH) {
                tmp_stack[tmp_depth++] = tspan_info;
            }
            cupidimage_svg_css_apply(&next_style, css, defs, tmp_stack, tmp_depth, NULL, 0,
                                     base_len, dpi);
            cupidimage_svg_apply_inline_style_attr(&next_style, &inner, base_len, dpi, defs);
            preserve = cupidimage_svg_xml_space_preserve(&inner, preserve);
            svg_matrix nm = cur_m;
            const char *tr = cupidimage_svg_get_attr(&inner, "transform");
            if (tr) {
                svg_matrix t;
                if (cupidimage_svg_parse_transform(tr, &t)) {
                    svg_matrix tmpm;
                    svg_matrix_multiply(&nm, &t, &tmpm);
                    nm = tmpm;
                }
            }
            cur_style = next_style;
            cur_m = nm;
            float v = 0.0f;
            if (cupidimage_svg_parse_length_attr(&inner, "x", ctx->vb_w, dpi, &v)) {
                pen_x = v;
            }
            if (cupidimage_svg_parse_length_attr(&inner, "y", ctx->vb_h, dpi, &v)) {
                pen_y = v;
            }
            if (cupidimage_svg_parse_length_attr(&inner, "dx", ctx->vb_w, dpi, &v)) {
                pen_x += v;
            }
            if (cupidimage_svg_parse_length_attr(&inner, "dy", ctx->vb_h, dpi, &v)) {
                pen_y += v;
            }
        } else if (!inner.is_end && strcmp(lname, "textpath") == 0) {
            if (tdepth < SVG_MAX_DEPTH) {
                state_stack[tdepth].style = cur_style;
                state_stack[tdepth].transform = cur_m;
                state_stack[tdepth].preserve = preserve;
                tdepth++;
            }
            svg_style next_style = cur_style;
            cupidimage_svg_apply_presentation_attrs(&next_style, &inner, base_len, dpi, defs);
            svg_elem_info tpath_info;
            cupidimage_svg_elem_info_from_tag(&tpath_info, lname, &inner);
            svg_elem_info tmp_stack[SVG_MAX_DEPTH];
            int tmp_depth = stack_depth < SVG_MAX_DEPTH ? stack_depth : SVG_MAX_DEPTH;
            for (int i = 0; i < tmp_depth; i++) {
                tmp_stack[i] = stack_info[i];
            }
            if (tmp_depth < SVG_MAX_DEPTH) {
                tmp_stack[tmp_depth++] = tpath_info;
            }
            cupidimage_svg_css_apply(&next_style, css, defs, tmp_stack, tmp_depth, NULL, 0,
                                     base_len, dpi);
            cupidimage_svg_apply_inline_style_attr(&next_style, &inner, base_len, dpi, defs);
            preserve = cupidimage_svg_xml_space_preserve(&inner, preserve);
            cur_style = next_style;

            if (path_depth < SVG_MAX_DEPTH) {
                svg_text_path_state *tp = &path_stack[path_depth++];
                memset(tp, 0, sizeof(*tp));
                tp->active = 0;
                cupidimage_svg_segments_init(&tp->segs);
                const char *href = cupidimage_svg_get_attr(&inner, "href");
                if (!href) {
                    href = cupidimage_svg_get_attr(&inner, "xlink:href");
                }
                char idbuf[96];
                idbuf[0] = '\0';
                cupidimage_svg_parse_href_id(href, idbuf, sizeof(idbuf));
                const svg_path_def *pd =
                    (idbuf[0] && defs) ? cupidimage_svg_defs_find_path(defs, idbuf) : NULL;
                if (pd && pd->d) {
                    if (cupidimage_svg_parse_path(pd->d, &tp->segs) && tp->segs.stroke_count > 0) {
                        tp->seg_count = tp->segs.stroke_count;
                        tp->seg_lengths = (float *)malloc(tp->seg_count * sizeof(float));
                        if (tp->seg_lengths) {
                            float total = 0.0f;
                            for (size_t i = 0; i < tp->seg_count; i++) {
                                float dx = tp->segs.stroke[i].x1 - tp->segs.stroke[i].x0;
                                float dy = tp->segs.stroke[i].y1 - tp->segs.stroke[i].y0;
                                float len = sqrtf(dx * dx + dy * dy);
                                tp->seg_lengths[i] = len;
                                total += len;
                            }
                            tp->total_len = total;
                            tp->bbox.x = tp->segs.minx;
                            tp->bbox.y = tp->segs.miny;
                            tp->bbox.w = tp->segs.maxx - tp->segs.minx;
                            tp->bbox.h = tp->segs.maxy - tp->segs.miny;
                            if (tp->total_len > 0.0f) {
                                tp->active = 1;
                            }
                        }
                    }
                }
                float offset = 0.0f;
                int offset_is_percent = 0;
                const char *start_offset = cupidimage_svg_get_attr(&inner, "startOffset");
                if (start_offset && tp->active) {
                    const char *sp = start_offset;
                    while (isspace((unsigned char)*sp)) {
                        sp++;
                    }
                    char *endp = NULL;
                    double v = strtod(sp, &endp);
                    if (endp != sp) {
                        while (isspace((unsigned char)*endp)) {
                            endp++;
                        }
                        if (*endp == '%') {
                            offset_is_percent = 1;
                            offset = (float)(v / 100.0);
                        } else {
                            int ok = 0;
                            offset = cupidimage_svg_parse_length(start_offset, base_len, dpi, &ok);
                            if (!ok) {
                                offset = 0.0f;
                            }
                        }
                    }
                }
                if (tp->active) {
                    if (offset_is_percent) {
                        tp->pos = offset * tp->total_len;
                    } else {
                        tp->pos = offset;
                    }
                    if (cur_style.text_anchor != 0) {
                        char *after = NULL;
                        char *close = cupidimage_svg_find_close_tag(tmp, "textpath", &after);
                        if (close) {
                            float text_len =
                                svg_textpath_measure_range(tmp, close, preserve, &cur_style);
                            if (cur_style.text_anchor == 1) {
                                tp->pos -= text_len * 0.5f;
                            } else if (cur_style.text_anchor == 2) {
                                tp->pos -= text_len;
                            }
                        }
                    }
                }
                if (inner.is_self_closing) {
                    svg_text_path_clear(tp);
                    path_depth--;
                    cur_path = path_depth > 0 ? &path_stack[path_depth - 1] : NULL;
                    if (tdepth > 0) {
                        tdepth--;
                        cur_style = state_stack[tdepth].style;
                        cur_m = state_stack[tdepth].transform;
                        preserve = state_stack[tdepth].preserve;
                    }
                } else {
                    cur_path = tp;
                }
            } else if (inner.is_self_closing) {
                if (tdepth > 0) {
                    tdepth--;
                    cur_style = state_stack[tdepth].style;
                    cur_m = state_stack[tdepth].transform;
                    preserve = state_stack[tdepth].preserve;
                }
            }
        } else if (inner.is_end && strcmp(lname, "textpath") == 0) {
            if (path_depth > 0) {
                svg_text_path_clear(&path_stack[path_depth - 1]);
                path_depth--;
            }
            cur_path = path_depth > 0 ? &path_stack[path_depth - 1] : NULL;
            if (tdepth > 0) {
                tdepth--;
                cur_style = state_stack[tdepth].style;
                cur_m = state_stack[tdepth].transform;
                preserve = state_stack[tdepth].preserve;
            }
        } else if (inner.is_end && strcmp(lname, "tspan") == 0) {
            if (tdepth > 0) {
                tdepth--;
                cur_style = state_stack[tdepth].style;
                cur_m = state_stack[tdepth].transform;
                preserve = state_stack[tdepth].preserve;
            }
        }
        *p = tmp;
    }
    while (path_depth > 0)
        svg_text_path_clear(&path_stack[--path_depth]);
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
