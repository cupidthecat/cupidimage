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

static uint8_t *svg_filter_resolve_input(const svg_filter_ref *ref, uint8_t *current,
                                         uint8_t *source, uint8_t *source_alpha,
                                         uint8_t *background, uint8_t *background_alpha,
                                         uint8_t *fill_paint, uint8_t *stroke_paint,
                                         uint8_t **results, int result_count);
static void svg_filter_clip_region(uint8_t *buf, uint32_t w, uint32_t h,
                                   const svg_filter_region *region);
static uint8_t svg_clamp_u8(float v);
static float svg_channel_value(const uint8_t *buf, size_t idx, int channel);
static float svg_alpha_sample_clamped(const uint8_t *buf, uint32_t w, uint32_t h, int x, int y);
static uint32_t svg_noise_hash(uint32_t x, uint32_t y, uint32_t seed);
static float svg_noise2(float x, float y, uint32_t seed);
static void svg_compute_filter_op_region(const svg_filter_op *op, const svg_filter *filter,
                                         const svg_render_ctx *ctx, const svg_bbox *bbox,
                                         const svg_matrix *m, const svg_filter_region *parent,
                                         svg_filter_region *out);

static uint8_t *svg_filter_resolve_input(const svg_filter_ref *ref, uint8_t *current,
                                         uint8_t *source, uint8_t *source_alpha,
                                         uint8_t *background, uint8_t *background_alpha,
                                         uint8_t *fill_paint, uint8_t *stroke_paint,
                                         uint8_t **results, int result_count) {
    if (!ref) {
        return current;
    }
    if (ref->type == SVG_FILTER_IN_SOURCE_GRAPHIC) {
        return source;
    }
    if (ref->type == SVG_FILTER_IN_SOURCE_ALPHA) {
        return source_alpha ? source_alpha : current;
    }
    if (ref->type == SVG_FILTER_IN_BACKGROUND_IMAGE) {
        return background ? background : current;
    }
    if (ref->type == SVG_FILTER_IN_BACKGROUND_ALPHA) {
        return background_alpha ? background_alpha : current;
    }
    if (ref->type == SVG_FILTER_IN_FILL_PAINT) {
        return fill_paint ? fill_paint : current;
    }
    if (ref->type == SVG_FILTER_IN_STROKE_PAINT) {
        return stroke_paint ? stroke_paint : current;
    }
    if (ref->type == SVG_FILTER_IN_RESULT) {
        if (results && ref->index >= 0 && ref->index < result_count && results[ref->index]) {
            return results[ref->index];
        }
        return current;
    }
    return current;
}

static void svg_filter_clip_region(uint8_t *buf, uint32_t w, uint32_t h,
                                   const svg_filter_region *region) {
    if (!buf || !region || !region->valid) {
        return;
    }
    int x0 = region->x0;
    int y0 = region->y0;
    int x1 = region->x1;
    int y1 = region->y1;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 >= (int)w)
        x1 = (int)w - 1;
    if (y1 >= (int)h)
        y1 = (int)h - 1;
    for (uint32_t y = 0; y < h; y++) {
        if ((int)y < y0 || (int)y > y1) {
            memset(buf + (size_t)y * w * 4u, 0, (size_t)w * 4u);
            continue;
        }
        if (x0 > 0) {
            memset(buf + ((size_t)y * w + (size_t)0) * 4u, 0, (size_t)x0 * 4u);
        }
        if (x1 + 1 < (int)w) {
            size_t off = ((size_t)y * w + (size_t)(x1 + 1)) * 4u;
            memset(buf + off, 0, (size_t)(w - (uint32_t)(x1 + 1)) * 4u);
        }
    }
}

static uint8_t svg_clamp_u8(float v) {
    if (v < 0.0f)
        return 0;
    if (v > 255.0f)
        return 255;
    return (uint8_t)lroundf(v);
}

static float svg_channel_value(const uint8_t *buf, size_t idx, int channel) {
    float a = buf[idx + 3] / 255.0f;
    if (channel == SVG_DISPLACE_A) {
        return a;
    }
    if (a <= 0.0f) {
        return 0.0f;
    }
    if (channel == SVG_DISPLACE_G) {
        return (buf[idx + 1] / 255.0f) / a;
    }
    if (channel == SVG_DISPLACE_B) {
        return (buf[idx + 2] / 255.0f) / a;
    }
    return (buf[idx + 0] / 255.0f) / a;
}

static float svg_alpha_sample_clamped(const uint8_t *buf, uint32_t w, uint32_t h, int x, int y) {
    if (!buf || w == 0 || h == 0) {
        return 0.0f;
    }
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x >= (int)w)
        x = (int)w - 1;
    if (y >= (int)h)
        y = (int)h - 1;
    size_t idx = ((size_t)y * w + (size_t)x) * 4u;
    return buf[idx + 3] / 255.0f;
}

static uint32_t svg_noise_hash(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = (x * 374761393u + y * 668265263u) ^ seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float svg_noise2(float x, float y, uint32_t seed) {
    if (!isfinite(x) || !isfinite(y))
        return 0.0f;
    double ix = floor((double)x), iy = floor((double)y);
    double wx = fmod(ix, 4294967296.0), wy = fmod(iy, 4294967296.0);
    if (wx < 0)
        wx += 4294967296.0;
    if (wy < 0)
        wy += 4294967296.0;
    uint32_t x0 = (uint32_t)wx, y0 = (uint32_t)wy;
    uint32_t x1 = x0 + 1u, y1 = y0 + 1u;
    float fx = (float)((double)x - ix);
    float fy = (float)((double)y - iy);
    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fy * fy * (3.0f - 2.0f * fy);
    float v00 = (float)(svg_noise_hash(x0, y0, seed) & 0xFFFF) / 65535.0f;
    float v10 = (float)(svg_noise_hash(x1, y0, seed) & 0xFFFF) / 65535.0f;
    float v01 = (float)(svg_noise_hash(x0, y1, seed) & 0xFFFF) / 65535.0f;
    float v11 = (float)(svg_noise_hash(x1, y1, seed) & 0xFFFF) / 65535.0f;
    float i1 = v00 + (v10 - v00) * u;
    float i2 = v01 + (v11 - v01) * u;
    float val = i1 + (i2 - i1) * v;
    return val * 2.0f - 1.0f;
}

void cupidimage_svg_compute_filter_region(const svg_filter *filter, const svg_render_ctx *ctx,
                                          const svg_bbox *bbox, const svg_matrix *m,
                                          svg_filter_region *out) {
    if (!out) {
        return;
    }
    out->valid = 0;
    if (!filter || !ctx || !bbox || !m) {
        return;
    }
    svg_bbox use_bbox = *bbox;
    if (!cupidimage_svg_bbox_valid(&use_bbox)) {
        use_bbox.x = ctx->vb_x;
        use_bbox.y = ctx->vb_y;
        use_bbox.w = ctx->vb_w;
        use_bbox.h = ctx->vb_h;
    }
    float bbox_w = use_bbox.w > 0.0f ? use_bbox.w : 1.0f;
    float bbox_h = use_bbox.h > 0.0f ? use_bbox.h : 1.0f;
    float base_w = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_w : ctx->vb_w;
    float base_h = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_h : ctx->vb_h;
    float origin_x = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? use_bbox.x : 0.0f;
    float origin_y = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? use_bbox.y : 0.0f;
    float fx = cupidimage_svg_resolve_grad_coord(&filter->x, base_w, origin_x, filter->units);
    float fy = cupidimage_svg_resolve_grad_coord(&filter->y, base_h, origin_y, filter->units);
    float fw = cupidimage_svg_resolve_grad_coord(&filter->width, base_w, 0.0f, filter->units);
    float fh = cupidimage_svg_resolve_grad_coord(&filter->height, base_h, 0.0f, filter->units);
    if (fw <= 0.0f || fh <= 0.0f) {
        return;
    }
    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    cupidimage_svg_transform_bounds(m, fx, fy, fx + fw, fy + fh, &dev_minx, &dev_miny, &dev_maxx,
                                    &dev_maxy);
    uint32_t ss = ctx->ss;
    int sx0 = cupidimage_svg_raster_bound(dev_minx * (float)ss, 0);
    int sy0 = cupidimage_svg_raster_bound(dev_miny * (float)ss, 0);
    int sx1 = cupidimage_svg_raster_bound(dev_maxx * (float)ss, 1);
    int sy1 = cupidimage_svg_raster_bound(dev_maxy * (float)ss, 1);
    int max_sx = (int)(ctx->width * ss) - 1;
    int max_sy = (int)(ctx->height * ss) - 1;
    if (sx0 < 0)
        sx0 = 0;
    if (sy0 < 0)
        sy0 = 0;
    if (sx1 > max_sx)
        sx1 = max_sx;
    if (sy1 > max_sy)
        sy1 = max_sy;
    if (sx0 > sx1 || sy0 > sy1) {
        return;
    }
    out->x0 = sx0;
    out->y0 = sy0;
    out->x1 = sx1;
    out->y1 = sy1;
    out->valid = 1;
}

static void svg_compute_filter_op_region(const svg_filter_op *op, const svg_filter *filter,
                                         const svg_render_ctx *ctx, const svg_bbox *bbox,
                                         const svg_matrix *m, const svg_filter_region *parent,
                                         svg_filter_region *out) {
    if (!out || !parent) {
        return;
    }
    *out = *parent;
    if (!op || !filter || !ctx || !bbox || !m || !op->has_region) {
        return;
    }
    svg_bbox use_bbox = *bbox;
    if (!cupidimage_svg_bbox_valid(&use_bbox)) {
        use_bbox.x = ctx->vb_x;
        use_bbox.y = ctx->vb_y;
        use_bbox.w = ctx->vb_w;
        use_bbox.h = ctx->vb_h;
    }
    float bbox_w = use_bbox.w > 0.0f ? use_bbox.w : 1.0f;
    float bbox_h = use_bbox.h > 0.0f ? use_bbox.h : 1.0f;
    float base_w = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_w : ctx->vb_w;
    float base_h = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_h : ctx->vb_h;
    float origin_x = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? use_bbox.x : 0.0f;
    float origin_y = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? use_bbox.y : 0.0f;
    float fx = cupidimage_svg_resolve_grad_coord(&op->rx, base_w, origin_x, filter->units);
    float fy = cupidimage_svg_resolve_grad_coord(&op->ry, base_h, origin_y, filter->units);
    float fw = cupidimage_svg_resolve_grad_coord(&op->rwidth, base_w, 0.0f, filter->units);
    float fh = cupidimage_svg_resolve_grad_coord(&op->rheight, base_h, 0.0f, filter->units);
    if (fw <= 0.0f || fh <= 0.0f) {
        return;
    }
    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    cupidimage_svg_transform_bounds(m, fx, fy, fx + fw, fy + fh, &dev_minx, &dev_miny, &dev_maxx,
                                    &dev_maxy);
    uint32_t ss = ctx->ss;
    int sx0 = cupidimage_svg_raster_bound(dev_minx * (float)ss, 0);
    int sy0 = cupidimage_svg_raster_bound(dev_miny * (float)ss, 0);
    int sx1 = cupidimage_svg_raster_bound(dev_maxx * (float)ss, 1);
    int sy1 = cupidimage_svg_raster_bound(dev_maxy * (float)ss, 1);
    int max_sx = (int)(ctx->width * ss) - 1;
    int max_sy = (int)(ctx->height * ss) - 1;
    if (sx0 < 0)
        sx0 = 0;
    if (sy0 < 0)
        sy0 = 0;
    if (sx1 > max_sx)
        sx1 = max_sx;
    if (sy1 > max_sy)
        sy1 = max_sy;
    if (sx0 > sx1 || sy0 > sy1) {
        return;
    }
    if (!parent->valid) {
        out->x0 = sx0;
        out->y0 = sy0;
        out->x1 = sx1;
        out->y1 = sy1;
        out->valid = 1;
        return;
    }
    if (sx0 > parent->x0)
        out->x0 = sx0;
    if (sy0 > parent->y0)
        out->y0 = sy0;
    if (sx1 < parent->x1)
        out->x1 = sx1;
    if (sy1 < parent->y1)
        out->y1 = sy1;
    out->valid = (out->x0 <= out->x1 && out->y0 <= out->y1);
}

void cupidimage_svg_apply_filter_ops(const svg_filter *filter, svg_render_ctx *ctx, uint8_t *buf,
                                     float base_len, const svg_filter_region *region,
                                     const svg_bbox *bbox, const svg_matrix *m, uint8_t *background,
                                     uint8_t *background_alpha, uint8_t *fill_paint,
                                     uint8_t *stroke_paint) {
    if (!filter || !ctx || !buf || !filter->content || !*filter->content) {
        return;
    }
    svg_filter_op *ops = NULL;
    int count = 0;
    int result_count = 0;
    if (!cupidimage_svg_parse_filter_ops(filter->content, &ops, &count, &result_count, base_len,
                                         ctx->dpi, filter->units)) {
        cupidimage_svg_filter_ops_cleanup(ops, count);
        free(ops);
        return;
    }
    uint32_t w = ctx->width * ctx->ss;
    uint32_t h = ctx->height * ctx->ss;
    float scale_x = ctx->vb_w > 0.0f ? (float)(ctx->width * ctx->ss) / ctx->vb_w : 1.0f;
    float scale_y = ctx->vb_h > 0.0f ? (float)(ctx->height * ctx->ss) / ctx->vb_h : 1.0f;
    size_t size = (size_t)w * (size_t)h * 4u;
    uint8_t *source = (uint8_t *)malloc(size);
    if (!source) {
        free(ops);
        return;
    }
    memcpy(source, buf, size);
    uint8_t *source_alpha = NULL;
    uint8_t **results = NULL;
    if (result_count > 0) {
        results = (uint8_t **)calloc((size_t)result_count, sizeof(uint8_t *));
    }
    for (int i = 0; i < count; i++) {
        svg_filter_op *op = &ops[i];
        svg_filter_region clip_region;
        if (region) {
            clip_region = *region;
        } else {
            clip_region.valid = 0;
            clip_region.x0 = 0;
            clip_region.y0 = 0;
            clip_region.x1 = (int)w - 1;
            clip_region.y1 = (int)h - 1;
        }
        if (op->has_region) {
            svg_filter_region parent_region = clip_region;
            svg_compute_filter_op_region(op, filter, ctx, bbox, m, &parent_region, &clip_region);
        }
        int need_alpha = (op->in1.type == SVG_FILTER_IN_SOURCE_ALPHA) ||
                         (op->in2.type == SVG_FILTER_IN_SOURCE_ALPHA);
        if (op->type == SVG_FILTER_OP_MERGE) {
            for (int m = 0; m < op->merge_count; m++) {
                if (op->merge_inputs[m].type == SVG_FILTER_IN_SOURCE_ALPHA) {
                    need_alpha = 1;
                    break;
                }
            }
        }
        if (need_alpha && !source_alpha) {
            source_alpha = (uint8_t *)malloc(size);
            if (source_alpha) {
                for (size_t j = 0; j < (size_t)w * (size_t)h; j++) {
                    uint8_t a = source[j * 4u + 3];
                    source_alpha[j * 4u + 0] = a;
                    source_alpha[j * 4u + 1] = a;
                    source_alpha[j * 4u + 2] = a;
                    source_alpha[j * 4u + 3] = a;
                }
            }
        }
        uint8_t *in1 = svg_filter_resolve_input(&op->in1, buf, source, source_alpha, background,
                                                background_alpha, fill_paint, stroke_paint, results,
                                                result_count);
        if (op->type == SVG_FILTER_OP_BLUR) {
            if (in1 != buf) {
                memcpy(buf, in1, size);
            }
            float sx = fabsf(op->a) * scale_x;
            float sy = fabsf(op->b) * scale_y;
            cupidimage_svg_gaussian_blur(buf, w, h, sx, sy);
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_OFFSET) {
            int dx = (int)lroundf(op->a * scale_x);
            int dy = (int)lroundf(op->b * scale_y);
            if (dx != 0 || dy != 0 || in1 != buf) {
                uint8_t *tmp = (uint8_t *)malloc(size);
                if (!tmp) {
                    continue;
                }
                memset(tmp, 0, size);
                for (uint32_t y = 0; y < h; y++) {
                    int sy = (int)y - dy;
                    if (sy < 0 || sy >= (int)h) {
                        continue;
                    }
                    for (uint32_t x = 0; x < w; x++) {
                        int sx = (int)x - dx;
                        if (sx < 0 || sx >= (int)w) {
                            continue;
                        }
                        size_t src = ((size_t)sy * w + (size_t)sx) * 4u;
                        size_t dst = ((size_t)y * w + (size_t)x) * 4u;
                        tmp[dst + 0] = in1[src + 0];
                        tmp[dst + 1] = in1[src + 1];
                        tmp[dst + 2] = in1[src + 2];
                        tmp[dst + 3] = in1[src + 3];
                    }
                }
                memcpy(buf, tmp, size);
                free(tmp);
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_COLOR_MATRIX && op->has_matrix) {
            if (in1 != buf) {
                memcpy(buf, in1, size);
            }
            for (size_t j = 0; j < (size_t)w * (size_t)h; j++) {
                size_t idx = j * 4u;
                float a = buf[idx + 3] / 255.0f;
                float r = 0.0f, g = 0.0f, b = 0.0f;
                if (a > 0.0f) {
                    r = buf[idx + 0] / 255.0f / a;
                    g = buf[idx + 1] / 255.0f / a;
                    b = buf[idx + 2] / 255.0f / a;
                }
                float nr = op->matrix[0] * r + op->matrix[1] * g + op->matrix[2] * b +
                           op->matrix[3] * a + op->matrix[4];
                float ng = op->matrix[5] * r + op->matrix[6] * g + op->matrix[7] * b +
                           op->matrix[8] * a + op->matrix[9];
                float nb = op->matrix[10] * r + op->matrix[11] * g + op->matrix[12] * b +
                           op->matrix[13] * a + op->matrix[14];
                float na = op->matrix[15] * r + op->matrix[16] * g + op->matrix[17] * b +
                           op->matrix[18] * a + op->matrix[19];
                if (nr < 0.0f)
                    nr = 0.0f;
                if (ng < 0.0f)
                    ng = 0.0f;
                if (nb < 0.0f)
                    nb = 0.0f;
                if (na < 0.0f)
                    na = 0.0f;
                if (nr > 1.0f)
                    nr = 1.0f;
                if (ng > 1.0f)
                    ng = 1.0f;
                if (nb > 1.0f)
                    nb = 1.0f;
                if (na > 1.0f)
                    na = 1.0f;
                buf[idx + 3] = (uint8_t)lroundf(na * 255.0f);
                buf[idx + 0] = (uint8_t)lroundf(nr * na * 255.0f);
                buf[idx + 1] = (uint8_t)lroundf(ng * na * 255.0f);
                buf[idx + 2] = (uint8_t)lroundf(nb * na * 255.0f);
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_COMPONENT_TRANSFER) {
            if (in1 != buf) {
                memcpy(buf, in1, size);
            }
            for (size_t j = 0; j < (size_t)w * (size_t)h; j++) {
                size_t idx = j * 4u;
                float a = buf[idx + 3] / 255.0f;
                float r = 0.0f, g = 0.0f, b = 0.0f;
                if (a > 0.0f) {
                    r = buf[idx + 0] / 255.0f / a;
                    g = buf[idx + 1] / 255.0f / a;
                    b = buf[idx + 2] / 255.0f / a;
                }
                float nr = cupidimage_svg_transfer_apply(&op->tr, r);
                float ng = cupidimage_svg_transfer_apply(&op->tg, g);
                float nb = cupidimage_svg_transfer_apply(&op->tb, b);
                float na = cupidimage_svg_transfer_apply(&op->ta, a);
                if (nr < 0.0f)
                    nr = 0.0f;
                if (ng < 0.0f)
                    ng = 0.0f;
                if (nb < 0.0f)
                    nb = 0.0f;
                if (na < 0.0f)
                    na = 0.0f;
                if (nr > 1.0f)
                    nr = 1.0f;
                if (ng > 1.0f)
                    ng = 1.0f;
                if (nb > 1.0f)
                    nb = 1.0f;
                if (na > 1.0f)
                    na = 1.0f;
                buf[idx + 3] = (uint8_t)lroundf(na * 255.0f);
                buf[idx + 0] = (uint8_t)lroundf(nr * na * 255.0f);
                buf[idx + 1] = (uint8_t)lroundf(ng * na * 255.0f);
                buf[idx + 2] = (uint8_t)lroundf(nb * na * 255.0f);
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_CONVOLVE) {
            if (in1 != buf) {
                memcpy(buf, in1, size);
            }
            uint8_t *tmp = (uint8_t *)malloc(size);
            if (!tmp) {
                continue;
            }
            int ox = op->order_x > 0 ? op->order_x : 3;
            int oy = op->order_y > 0 ? op->order_y : 3;
            int tx = op->target_x;
            int ty = op->target_y;
            if (tx < 0)
                tx = 0;
            if (ty < 0)
                ty = 0;
            if (tx >= ox)
                tx = ox - 1;
            if (ty >= oy)
                ty = oy - 1;
            float divisor = (fabsf(op->divisor) < 1e-6f) ? 1.0f : op->divisor;
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    float acc_r = 0.0f;
                    float acc_g = 0.0f;
                    float acc_b = 0.0f;
                    float acc_a = 0.0f;
                    for (int ky = 0; ky < oy; ky++) {
                        int sy = (int)y + (ky - ty);
                        if (op->mode == SVG_EDGE_DUPLICATE) {
                            if (sy < 0)
                                sy = 0;
                            if (sy >= (int)h)
                                sy = (int)h - 1;
                        } else if (op->mode == SVG_EDGE_WRAP) {
                            if (sy < 0)
                                sy = (sy % (int)h + (int)h) % (int)h;
                            if (sy >= (int)h)
                                sy %= (int)h;
                        } else {
                            if (sy < 0 || sy >= (int)h) {
                                continue;
                            }
                        }
                        for (int kx = 0; kx < ox; kx++) {
                            int sx = (int)x + (kx - tx);
                            if (op->mode == SVG_EDGE_DUPLICATE) {
                                if (sx < 0)
                                    sx = 0;
                                if (sx >= (int)w)
                                    sx = (int)w - 1;
                            } else if (op->mode == SVG_EDGE_WRAP) {
                                if (sx < 0)
                                    sx = (sx % (int)w + (int)w) % (int)w;
                                if (sx >= (int)w)
                                    sx %= (int)w;
                            } else {
                                if (sx < 0 || sx >= (int)w) {
                                    continue;
                                }
                            }
                            size_t sidx = ((size_t)sy * w + (size_t)sx) * 4u;
                            float k = op->kernel ? op->kernel[ky * ox + kx] : 0.0f;
                            acc_r += (float)in1[sidx + 0] * k;
                            acc_g += (float)in1[sidx + 1] * k;
                            acc_b += (float)in1[sidx + 2] * k;
                            acc_a += (float)in1[sidx + 3] * k;
                        }
                    }
                    acc_r = acc_r / divisor + op->bias * 255.0f;
                    acc_g = acc_g / divisor + op->bias * 255.0f;
                    acc_b = acc_b / divisor + op->bias * 255.0f;
                    acc_a = acc_a / divisor + op->bias * 255.0f;
                    size_t didx = ((size_t)y * w + (size_t)x) * 4u;
                    if (op->preserve_alpha) {
                        uint8_t oa = in1[didx + 3];
                        tmp[didx + 3] = oa;
                        float a = oa / 255.0f;
                        tmp[didx + 0] = svg_clamp_u8(acc_r * a / 255.0f);
                        tmp[didx + 1] = svg_clamp_u8(acc_g * a / 255.0f);
                        tmp[didx + 2] = svg_clamp_u8(acc_b * a / 255.0f);
                    } else {
                        tmp[didx + 0] = svg_clamp_u8(acc_r);
                        tmp[didx + 1] = svg_clamp_u8(acc_g);
                        tmp[didx + 2] = svg_clamp_u8(acc_b);
                        tmp[didx + 3] = svg_clamp_u8(acc_a);
                    }
                }
            }
            memcpy(buf, tmp, size);
            free(tmp);
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_TURBULENCE) {
            float fx = op->turb_freq_x;
            float fy = op->turb_freq_y;
            if (fx <= 0.0f)
                fx = 0.01f;
            if (fy <= 0.0f)
                fy = 0.01f;
            int octaves = op->turb_octaves;
            if (octaves < 1)
                octaves = 1;
            float max_amp = 0.0f;
            float amp = 1.0f;
            for (int o = 0; o < octaves; o++) {
                max_amp += amp;
                amp *= 0.5f;
            }
            float inv_sx = scale_x > 1e-6f ? 1.0f / scale_x : 1.0f;
            float inv_sy = scale_y > 1e-6f ? 1.0f / scale_y : 1.0f;
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    float ux = (float)x * inv_sx;
                    float uy = (float)y * inv_sy;
                    float sum = 0.0f;
                    float freqx = fx;
                    float freqy = fy;
                    float a = 1.0f;
                    for (int o = 0; o < octaves; o++) {
                        float n = svg_noise2(ux * freqx, uy * freqy,
                                             (uint32_t)op->turb_seed + (uint32_t)o * 17u);
                        if (op->turb_fractal) {
                            sum += n * a;
                        } else {
                            sum += fabsf(n) * a;
                        }
                        a *= 0.5f;
                        freqx *= 2.0f;
                        freqy *= 2.0f;
                    }
                    float val = op->turb_fractal ? (sum / max_amp * 0.5f + 0.5f) : (sum / max_amp);
                    if (val < 0.0f)
                        val = 0.0f;
                    if (val > 1.0f)
                        val = 1.0f;
                    uint8_t c = (uint8_t)lroundf(val * 255.0f);
                    size_t idx = ((size_t)y * w + (size_t)x) * 4u;
                    buf[idx + 0] = c;
                    buf[idx + 1] = c;
                    buf[idx + 2] = c;
                    buf[idx + 3] = 255;
                }
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_DISPLACEMENT) {
            uint8_t *in2 = svg_filter_resolve_input(&op->in2, buf, source, source_alpha, background,
                                                    background_alpha, fill_paint, stroke_paint,
                                                    results, result_count);
            uint8_t *tmp = (uint8_t *)malloc(size);
            if (!tmp) {
                continue;
            }
            float scale_dx = op->a * scale_x;
            float scale_dy = op->a * scale_y;
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    size_t idx = ((size_t)y * w + (size_t)x) * 4u;
                    float cx = svg_channel_value(in2, idx, op->displace_x_channel);
                    float cy = svg_channel_value(in2, idx, op->displace_y_channel);
                    float dx = (cx - 0.5f) * scale_dx;
                    float dy = (cy - 0.5f) * scale_dy;
                    int sx = (int)lroundf((float)x + dx);
                    int sy = (int)lroundf((float)y + dy);
                    if (sx < 0)
                        sx = 0;
                    if (sy < 0)
                        sy = 0;
                    if (sx >= (int)w)
                        sx = (int)w - 1;
                    if (sy >= (int)h)
                        sy = (int)h - 1;
                    size_t sidx = ((size_t)sy * w + (size_t)sx) * 4u;
                    tmp[idx + 0] = in1[sidx + 0];
                    tmp[idx + 1] = in1[sidx + 1];
                    tmp[idx + 2] = in1[sidx + 2];
                    tmp[idx + 3] = in1[sidx + 3];
                }
            }
            memcpy(buf, tmp, size);
            free(tmp);
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_DIFFUSE_LIGHTING ||
                   op->type == SVG_FILTER_OP_SPECULAR_LIGHTING) {
            if (in1 != buf) {
                memcpy(buf, in1, size);
            }
            float lx = op->light_x;
            float ly = op->light_y;
            float lz = op->light_z;
            float llen = sqrtf(lx * lx + ly * ly + lz * lz);
            if (llen <= 1e-6f) {
                lx = 0.0f;
                ly = 0.0f;
                lz = 1.0f;
                llen = 1.0f;
            }
            lx /= llen;
            ly /= llen;
            lz /= llen;
            uint8_t lr = (uint8_t)(op->color >> 24);
            uint8_t lg = (uint8_t)(op->color >> 16);
            uint8_t lb = (uint8_t)(op->color >> 8);
            float lrf = lr / 255.0f;
            float lgf = lg / 255.0f;
            float lbf = lb / 255.0f;
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    size_t idx = ((size_t)y * w + (size_t)x) * 4u;
                    int xi = (int)x;
                    int yi = (int)y;
                    float a00 = svg_alpha_sample_clamped(in1, w, h, xi - 1, yi - 1);
                    float a10 = svg_alpha_sample_clamped(in1, w, h, xi, yi - 1);
                    float a20 = svg_alpha_sample_clamped(in1, w, h, xi + 1, yi - 1);
                    float a01 = svg_alpha_sample_clamped(in1, w, h, xi - 1, yi);
                    float a21 = svg_alpha_sample_clamped(in1, w, h, xi + 1, yi);
                    float a02 = svg_alpha_sample_clamped(in1, w, h, xi - 1, yi + 1);
                    float a12 = svg_alpha_sample_clamped(in1, w, h, xi, yi + 1);
                    float a22 = svg_alpha_sample_clamped(in1, w, h, xi + 1, yi + 1);
                    float sx = (a20 + 2.0f * a21 + a22) - (a00 + 2.0f * a01 + a02);
                    float sy = (a02 + 2.0f * a12 + a22) - (a00 + 2.0f * a10 + a20);
                    float dx = sx * op->surface_scale;
                    float dy = sy * op->surface_scale;
                    float nx = -dx;
                    float ny = -dy;
                    float nz = 1.0f;
                    float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (nlen > 1e-6f) {
                        nx /= nlen;
                        ny /= nlen;
                        nz /= nlen;
                    }
                    float intensity = 0.0f;
                    if (op->type == SVG_FILTER_OP_DIFFUSE_LIGHTING) {
                        float dotnl = nx * lx + ny * ly + nz * lz;
                        if (dotnl > 0.0f) {
                            intensity = dotnl * op->diffuse_constant;
                        }
                    } else {
                        float dotnl = nx * lx + ny * ly + nz * lz;
                        if (dotnl > 0.0f) {
                            float rz = 2.0f * dotnl * nz - lz;
                            float dotrv = rz;
                            if (dotrv > 0.0f) {
                                intensity =
                                    powf(dotrv, op->specular_exponent) * op->specular_constant;
                            }
                        }
                    }
                    float base_a = in1[idx + 3] / 255.0f;
                    float out_a = 0.0f;
                    float out_r = 0.0f;
                    float out_g = 0.0f;
                    float out_b = 0.0f;
                    if (op->type == SVG_FILTER_OP_DIFFUSE_LIGHTING) {
                        if (intensity < 0.0f)
                            intensity = 0.0f;
                        if (intensity > 1.0f)
                            intensity = 1.0f;
                        out_a = base_a;
                        out_r = lrf * intensity;
                        out_g = lgf * intensity;
                        out_b = lbf * intensity;
                    } else {
                        if (intensity < 0.0f)
                            intensity = 0.0f;
                        if (intensity > 1.0f)
                            intensity = 1.0f;
                        out_a = base_a * intensity;
                        out_r = lrf;
                        out_g = lgf;
                        out_b = lbf;
                    }
                    buf[idx + 3] = svg_clamp_u8(out_a * 255.0f);
                    buf[idx + 0] = svg_clamp_u8(out_r * out_a * 255.0f);
                    buf[idx + 1] = svg_clamp_u8(out_g * out_a * 255.0f);
                    buf[idx + 2] = svg_clamp_u8(out_b * out_a * 255.0f);
                }
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_FLOOD) {
            uint8_t a = (uint8_t)(op->color & 0xFFu);
            uint8_t r = (uint8_t)(op->color >> 24);
            uint8_t g = (uint8_t)(op->color >> 16);
            uint8_t b = (uint8_t)(op->color >> 8);
            uint8_t pr = (uint8_t)((r * a) / 255u);
            uint8_t pg = (uint8_t)((g * a) / 255u);
            uint8_t pb = (uint8_t)((b * a) / 255u);
            for (size_t j = 0; j < (size_t)w * (size_t)h; j++) {
                size_t idx = j * 4u;
                buf[idx + 0] = pr;
                buf[idx + 1] = pg;
                buf[idx + 2] = pb;
                buf[idx + 3] = a;
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_BLEND || op->type == SVG_FILTER_OP_COMPOSITE) {
            uint8_t *in2 = svg_filter_resolve_input(&op->in2, buf, source, source_alpha, background,
                                                    background_alpha, fill_paint, stroke_paint,
                                                    results, result_count);
            uint8_t *tmp = (uint8_t *)malloc(size);
            if (!tmp) {
                continue;
            }
            for (size_t j = 0; j < (size_t)w * (size_t)h; j++) {
                size_t idx = j * 4u;
                float a1 = in1[idx + 3] / 255.0f;
                float a2 = in2[idx + 3] / 255.0f;
                float r1 = 0.0f, g1 = 0.0f, b1 = 0.0f;
                float r2 = 0.0f, g2 = 0.0f, b2 = 0.0f;
                if (a1 > 0.0f) {
                    r1 = in1[idx + 0] / 255.0f / a1;
                    g1 = in1[idx + 1] / 255.0f / a1;
                    b1 = in1[idx + 2] / 255.0f / a1;
                }
                if (a2 > 0.0f) {
                    r2 = in2[idx + 0] / 255.0f / a2;
                    g2 = in2[idx + 1] / 255.0f / a2;
                    b2 = in2[idx + 2] / 255.0f / a2;
                }
                float out_a = a1 + a2 - a1 * a2;
                float out_r = 0.0f;
                float out_g = 0.0f;
                float out_b = 0.0f;
                if (op->type == SVG_FILTER_OP_BLEND) {
                    float br = r2;
                    float bg = g2;
                    float bb = b2;
                    if (op->mode == SVG_BLEND_MULTIPLY) {
                        br = r1 * r2;
                        bg = g1 * g2;
                        bb = b1 * b2;
                    } else if (op->mode == SVG_BLEND_SCREEN) {
                        br = 1.0f - (1.0f - r1) * (1.0f - r2);
                        bg = 1.0f - (1.0f - g1) * (1.0f - g2);
                        bb = 1.0f - (1.0f - b1) * (1.0f - b2);
                    } else if (op->mode == SVG_BLEND_DARKEN) {
                        br = fminf(r1, r2);
                        bg = fminf(g1, g2);
                        bb = fminf(b1, b2);
                    } else if (op->mode == SVG_BLEND_LIGHTEN) {
                        br = fmaxf(r1, r2);
                        bg = fmaxf(g1, g2);
                        bb = fmaxf(b1, b2);
                    } else if (op->mode == SVG_BLEND_OVERLAY) {
                        br = (r1 <= 0.5f) ? (2.0f * r1 * r2)
                                          : (1.0f - 2.0f * (1.0f - r1) * (1.0f - r2));
                        bg = (g1 <= 0.5f) ? (2.0f * g1 * g2)
                                          : (1.0f - 2.0f * (1.0f - g1) * (1.0f - g2));
                        bb = (b1 <= 0.5f) ? (2.0f * b1 * b2)
                                          : (1.0f - 2.0f * (1.0f - b1) * (1.0f - b2));
                    } else if (op->mode == SVG_BLEND_HARDLIGHT) {
                        br = (r2 <= 0.5f) ? (2.0f * r1 * r2)
                                          : (1.0f - 2.0f * (1.0f - r1) * (1.0f - r2));
                        bg = (g2 <= 0.5f) ? (2.0f * g1 * g2)
                                          : (1.0f - 2.0f * (1.0f - g1) * (1.0f - g2));
                        bb = (b2 <= 0.5f) ? (2.0f * b1 * b2)
                                          : (1.0f - 2.0f * (1.0f - b1) * (1.0f - b2));
                    } else if (op->mode == SVG_BLEND_SOFTLIGHT) {
                        float gr = (r1 <= 0.25f) ? (((16.0f * r1 - 12.0f) * r1 + 4.0f) * r1)
                                                 : sqrtf(fmaxf(r1, 0.0f));
                        float gg = (g1 <= 0.25f) ? (((16.0f * g1 - 12.0f) * g1 + 4.0f) * g1)
                                                 : sqrtf(fmaxf(g1, 0.0f));
                        float gb = (b1 <= 0.25f) ? (((16.0f * b1 - 12.0f) * b1 + 4.0f) * b1)
                                                 : sqrtf(fmaxf(b1, 0.0f));
                        br = (r2 <= 0.5f) ? (r1 - (1.0f - 2.0f * r2) * r1 * (1.0f - r1))
                                          : (r1 + (2.0f * r2 - 1.0f) * (gr - r1));
                        bg = (g2 <= 0.5f) ? (g1 - (1.0f - 2.0f * g2) * g1 * (1.0f - g1))
                                          : (g1 + (2.0f * g2 - 1.0f) * (gg - g1));
                        bb = (b2 <= 0.5f) ? (b1 - (1.0f - 2.0f * b2) * b1 * (1.0f - b1))
                                          : (b1 + (2.0f * b2 - 1.0f) * (gb - b1));
                    }
                    float c1r = (float)in1[idx + 0] / 255.0f;
                    float c1g = (float)in1[idx + 1] / 255.0f;
                    float c1b = (float)in1[idx + 2] / 255.0f;
                    float c2r = (float)in2[idx + 0] / 255.0f;
                    float c2g = (float)in2[idx + 1] / 255.0f;
                    float c2b = (float)in2[idx + 2] / 255.0f;
                    float pr = c1r * (1.0f - a2) + c2r * (1.0f - a1) + br * a1 * a2;
                    float pg = c1g * (1.0f - a2) + c2g * (1.0f - a1) + bg * a1 * a2;
                    float pb = c1b * (1.0f - a2) + c2b * (1.0f - a1) + bb * a1 * a2;
                    if (out_a > 1e-6f) {
                        out_r = pr / out_a;
                        out_g = pg / out_a;
                        out_b = pb / out_a;
                    } else {
                        out_r = 0.0f;
                        out_g = 0.0f;
                        out_b = 0.0f;
                    }
                } else {
                    float ar = (float)in1[idx + 0] / 255.0f;
                    float ag = (float)in1[idx + 1] / 255.0f;
                    float ab = (float)in1[idx + 2] / 255.0f;
                    float br = (float)in2[idx + 0] / 255.0f;
                    float bg = (float)in2[idx + 1] / 255.0f;
                    float bb = (float)in2[idx + 2] / 255.0f;
                    float out_pr = 0.0f, out_pg = 0.0f, out_pb = 0.0f;
                    float out_pa = 0.0f;
                    if (op->mode == SVG_COMPOSITE_OVER) {
                        out_pr = ar + br * (1.0f - a1);
                        out_pg = ag + bg * (1.0f - a1);
                        out_pb = ab + bb * (1.0f - a1);
                        out_pa = a1 + a2 * (1.0f - a1);
                    } else if (op->mode == SVG_COMPOSITE_IN) {
                        out_pr = ar * a2;
                        out_pg = ag * a2;
                        out_pb = ab * a2;
                        out_pa = a1 * a2;
                    } else if (op->mode == SVG_COMPOSITE_OUT) {
                        out_pr = ar * (1.0f - a2);
                        out_pg = ag * (1.0f - a2);
                        out_pb = ab * (1.0f - a2);
                        out_pa = a1 * (1.0f - a2);
                    } else if (op->mode == SVG_COMPOSITE_ATOP) {
                        out_pr = ar * a2 + br * (1.0f - a1);
                        out_pg = ag * a2 + bg * (1.0f - a1);
                        out_pb = ab * a2 + bb * (1.0f - a1);
                        out_pa = a2;
                    } else if (op->mode == SVG_COMPOSITE_XOR) {
                        out_pr = ar * (1.0f - a2) + br * (1.0f - a1);
                        out_pg = ag * (1.0f - a2) + bg * (1.0f - a1);
                        out_pb = ab * (1.0f - a2) + bb * (1.0f - a1);
                        out_pa = a1 + a2 - 2.0f * a1 * a2;
                    } else if (op->mode == SVG_COMPOSITE_ARITHMETIC) {
                        out_pr = op->a * ar * br + op->b * ar + op->c * br + op->d;
                        out_pg = op->a * ag * bg + op->b * ag + op->c * bg + op->d;
                        out_pb = op->a * ab * bb + op->b * ab + op->c * bb + op->d;
                        out_pa = op->a * a1 * a2 + op->b * a1 + op->c * a2 + op->d;
                    }
                    if (out_pr < 0.0f)
                        out_pr = 0.0f;
                    if (out_pg < 0.0f)
                        out_pg = 0.0f;
                    if (out_pb < 0.0f)
                        out_pb = 0.0f;
                    if (out_pa < 0.0f)
                        out_pa = 0.0f;
                    if (out_pr > 1.0f)
                        out_pr = 1.0f;
                    if (out_pg > 1.0f)
                        out_pg = 1.0f;
                    if (out_pb > 1.0f)
                        out_pb = 1.0f;
                    if (out_pa > 1.0f)
                        out_pa = 1.0f;
                    tmp[idx + 0] = (uint8_t)lroundf(out_pr * 255.0f);
                    tmp[idx + 1] = (uint8_t)lroundf(out_pg * 255.0f);
                    tmp[idx + 2] = (uint8_t)lroundf(out_pb * 255.0f);
                    tmp[idx + 3] = (uint8_t)lroundf(out_pa * 255.0f);
                    continue;
                }
                if (out_a < 0.0f)
                    out_a = 0.0f;
                if (out_a > 1.0f)
                    out_a = 1.0f;
                if (out_r < 0.0f)
                    out_r = 0.0f;
                if (out_g < 0.0f)
                    out_g = 0.0f;
                if (out_b < 0.0f)
                    out_b = 0.0f;
                if (out_r > 1.0f)
                    out_r = 1.0f;
                if (out_g > 1.0f)
                    out_g = 1.0f;
                if (out_b > 1.0f)
                    out_b = 1.0f;
                tmp[idx + 3] = (uint8_t)lroundf(out_a * 255.0f);
                tmp[idx + 0] = (uint8_t)lroundf(out_r * out_a * 255.0f);
                tmp[idx + 1] = (uint8_t)lroundf(out_g * out_a * 255.0f);
                tmp[idx + 2] = (uint8_t)lroundf(out_b * out_a * 255.0f);
            }
            memcpy(buf, tmp, size);
            free(tmp);
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_TILE) {
            if (in1 != buf) {
                memcpy(buf, in1, size);
            }
            if (clip_region.valid) {
                int x0 = clip_region.x0;
                int y0 = clip_region.y0;
                int x1 = clip_region.x1;
                int y1 = clip_region.y1;
                int tw = x1 - x0 + 1;
                int th = y1 - y0 + 1;
                if (tw > 0 && th > 0) {
                    for (int y = y0; y <= y1; y++) {
                        for (int x = x0; x <= x1; x++) {
                            int sx = x0 + ((x - x0) % tw);
                            int sy = y0 + ((y - y0) % th);
                            size_t sidx = ((size_t)sy * w + (size_t)sx) * 4u;
                            size_t didx = ((size_t)y * w + (size_t)x) * 4u;
                            buf[didx + 0] = in1[sidx + 0];
                            buf[didx + 1] = in1[sidx + 1];
                            buf[didx + 2] = in1[sidx + 2];
                            buf[didx + 3] = in1[sidx + 3];
                        }
                    }
                }
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_IMAGE) {
            memset(buf, 0, size);
            if (op->image_href && *op->image_href) {
                cupidimage_image img;
                if (cupidimage_svg_load_href_image(op->image_href, &img)) {
                    float draw_w = op->img_w;
                    float draw_h = op->img_h;
                    if (draw_w <= 0.0f) {
                        draw_w = (float)img.width / (scale_x > 0.0f ? scale_x : 1.0f);
                    }
                    if (draw_h <= 0.0f) {
                        draw_h = (float)img.height / (scale_y > 0.0f ? scale_y : 1.0f);
                    }
                    float draw_x = op->img_x;
                    float draw_y = op->img_y;
                    int px = (int)lroundf(draw_x * scale_x);
                    int py = (int)lroundf(draw_y * scale_y);
                    int pw = (int)lroundf(draw_w * scale_x);
                    int ph = (int)lroundf(draw_h * scale_y);
                    if (pw < 1)
                        pw = 1;
                    if (ph < 1)
                        ph = 1;
                    for (int y = 0; y < ph; y++) {
                        int dy = py + y;
                        if (dy < 0 || dy >= (int)h)
                            continue;
                        int sy = (int)((float)y / (float)ph * (float)img.height);
                        if (sy < 0)
                            sy = 0;
                        if (sy >= (int)img.height)
                            sy = (int)img.height - 1;
                        for (int x = 0; x < pw; x++) {
                            int dx = px + x;
                            if (dx < 0 || dx >= (int)w)
                                continue;
                            int sx = (int)((float)x / (float)pw * (float)img.width);
                            if (sx < 0)
                                sx = 0;
                            if (sx >= (int)img.width)
                                sx = (int)img.width - 1;
                            size_t sidx = ((size_t)sy * img.width + (size_t)sx) * 4u;
                            size_t didx = ((size_t)dy * w + (size_t)dx) * 4u;
                            uint8_t a = img.rgba[sidx + 3];
                            uint8_t pr = (uint8_t)((img.rgba[sidx + 0] * a) / 255u);
                            uint8_t pg = (uint8_t)((img.rgba[sidx + 1] * a) / 255u);
                            uint8_t pb = (uint8_t)((img.rgba[sidx + 2] * a) / 255u);
                            buf[didx + 0] = pr;
                            buf[didx + 1] = pg;
                            buf[didx + 2] = pb;
                            buf[didx + 3] = a;
                        }
                    }
                    cupidimage_free(&img);
                }
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_MORPHOLOGY) {
            int rx = (int)lroundf(fabsf(op->a) * scale_x);
            int ry = (int)lroundf(fabsf(op->b) * scale_y);
            if (rx < 0)
                rx = 0;
            if (ry < 0)
                ry = 0;
            if (rx == 0 && ry == 0) {
                if (in1 != buf) {
                    memcpy(buf, in1, size);
                }
            } else {
                uint8_t *src = (uint8_t *)malloc(size);
                if (!src) {
                    continue;
                }
                memcpy(src, in1, size);
                for (uint32_t y = 0; y < h; y++) {
                    for (uint32_t x = 0; x < w; x++) {
                        uint8_t best_r = (op->mode == SVG_MORPHOLOGY_DILATE) ? 0 : 255;
                        uint8_t best_g = (op->mode == SVG_MORPHOLOGY_DILATE) ? 0 : 255;
                        uint8_t best_b = (op->mode == SVG_MORPHOLOGY_DILATE) ? 0 : 255;
                        uint8_t best_a = (op->mode == SVG_MORPHOLOGY_DILATE) ? 0 : 255;
                        int y0 = (int)y - ry;
                        int y1 = (int)y + ry;
                        int x0 = (int)x - rx;
                        int x1 = (int)x + rx;
                        if (y0 < 0)
                            y0 = 0;
                        if (x0 < 0)
                            x0 = 0;
                        if (y1 >= (int)h)
                            y1 = (int)h - 1;
                        if (x1 >= (int)w)
                            x1 = (int)w - 1;
                        for (int yy = y0; yy <= y1; yy++) {
                            for (int xx = x0; xx <= x1; xx++) {
                                size_t sidx = ((size_t)yy * w + (size_t)xx) * 4u;
                                uint8_t sr = src[sidx + 0];
                                uint8_t sg = src[sidx + 1];
                                uint8_t sb = src[sidx + 2];
                                uint8_t sa = src[sidx + 3];
                                if (op->mode == SVG_MORPHOLOGY_DILATE) {
                                    if (sr > best_r)
                                        best_r = sr;
                                    if (sg > best_g)
                                        best_g = sg;
                                    if (sb > best_b)
                                        best_b = sb;
                                    if (sa > best_a)
                                        best_a = sa;
                                } else {
                                    if (sr < best_r)
                                        best_r = sr;
                                    if (sg < best_g)
                                        best_g = sg;
                                    if (sb < best_b)
                                        best_b = sb;
                                    if (sa < best_a)
                                        best_a = sa;
                                }
                            }
                        }
                        size_t didx = ((size_t)y * w + (size_t)x) * 4u;
                        buf[didx + 0] = best_r;
                        buf[didx + 1] = best_g;
                        buf[didx + 2] = best_b;
                        buf[didx + 3] = best_a;
                    }
                }
                free(src);
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_MERGE) {
            uint8_t *tmp = (uint8_t *)calloc(1u, size);
            if (!tmp) {
                continue;
            }
            int mcount = op->merge_count;
            if (mcount == 0) {
                mcount = 1;
                op->merge_inputs[0] =
                    cupidimage_svg_make_filter_ref(SVG_FILTER_IN_SOURCE_GRAPHIC, -1);
            }
            for (int m = 0; m < mcount; m++) {
                uint8_t *inm = svg_filter_resolve_input(
                    &op->merge_inputs[m], buf, source, source_alpha, background, background_alpha,
                    fill_paint, stroke_paint, results, result_count);
                for (size_t j = 0; j < (size_t)w * (size_t)h; j++) {
                    size_t idx = j * 4u;
                    cupidimage_svg_blend_premul(tmp + idx, inm[idx + 0], inm[idx + 1], inm[idx + 2],
                                                inm[idx + 3]);
                }
            }
            memcpy(buf, tmp, size);
            free(tmp);
            svg_filter_clip_region(buf, w, h, &clip_region);
        }
        if (op->result_id >= 0 && results && op->result_id < result_count) {
            free(results[op->result_id]);
            results[op->result_id] = (uint8_t *)malloc(size);
            if (results[op->result_id]) {
                memcpy(results[op->result_id], buf, size);
            }
        }
    }
    if (results) {
        for (int i = 0; i < result_count; i++) {
            free(results[i]);
        }
        free(results);
    }
    cupidimage_svg_filter_ops_cleanup(ops, count);
    free(source_alpha);
    free(source);
    free(ops);
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
