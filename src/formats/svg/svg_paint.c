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

static int svg_clip_prepare(svg_clip_path *clip, const svg_render_ctx *ctx, const svg_bbox *bbox);
static int svg_mask_prepare(svg_mask *mask, const svg_render_ctx *ctx, const svg_bbox *bbox);
static int svg_pattern_prepare(svg_pattern *p, const svg_render_ctx *ctx, const svg_bbox *bbox);
static int svg_pattern_sample(svg_pattern *p, const svg_render_ctx *ctx, const svg_bbox *bbox,
                              float x, float y, uint8_t *pr, uint8_t *pg, uint8_t *pb, uint8_t *pa);
static uint32_t svg_gradient_sample(const svg_gradient *g, const svg_render_ctx *ctx,
                                    const svg_bbox *bbox, float x, float y);
static int svg_decode_entity(const char *s, size_t *consumed);

static int svg_clip_prepare(svg_clip_path *clip, const svg_render_ctx *ctx, const svg_bbox *bbox) {
    if (!clip || !ctx || !bbox || !clip->content || !*clip->content) {
        return 0;
    }
    if (clip->rendering) {
        return 0;
    }
    uint32_t out_w = ctx->width * ctx->ss;
    uint32_t out_h = ctx->height * ctx->ss;
    int needs_bbox = (clip->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX);
    if (clip->rendered && clip->rgba && clip->mask_w == out_w && clip->mask_h == out_h) {
        if (!needs_bbox) {
            return 1;
        }
        if (clip->has_cached_bbox && fabsf(clip->cached_bbox_x - bbox->x) < 1e-4f &&
            fabsf(clip->cached_bbox_y - bbox->y) < 1e-4f &&
            fabsf(clip->cached_bbox_w - bbox->w) < 1e-4f &&
            fabsf(clip->cached_bbox_h - bbox->h) < 1e-4f) {
            return 1;
        }
    }
    free(clip->rgba);
    clip->rgba = NULL;
    clip->rendered = 0;
    clip->rendering = 1;

    char header[256];
    int header_len =
        snprintf(header, sizeof(header),
                 "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%u\" height=\"%u\" "
                 "viewBox=\"%.6g %.6g %.6g %.6g\">",
                 out_w, out_h, ctx->vb_x, ctx->vb_y, ctx->vb_w, ctx->vb_h);
    if (header_len < 0 || header_len >= (int)sizeof(header)) {
        clip->rendering = 0;
        return 0;
    }
    char g1[192];
    int g1_len = 0;
    if (clip->has_transform) {
        char mat[96];
        if (cupidimage_svg_matrix_to_string(&clip->transform, mat, sizeof(mat))) {
            g1_len = snprintf(g1, sizeof(g1), "<g transform=\"%s\">", mat);
            if (g1_len < 0 || g1_len >= (int)sizeof(g1)) {
                clip->rendering = 0;
                return 0;
            }
        }
    }
    char g2[192];
    int g2_len = 0;
    if (needs_bbox) {
        float bw = bbox->w > 0.0f ? bbox->w : 1.0f;
        float bh = bbox->h > 0.0f ? bbox->h : 1.0f;
        g2_len = snprintf(g2, sizeof(g2), "<g transform=\"translate(%.6g %.6g) scale(%.6g %.6g)\">",
                          bbox->x, bbox->y, bw, bh);
        if (g2_len < 0 || g2_len >= (int)sizeof(g2)) {
            clip->rendering = 0;
            return 0;
        }
    }
    const char *g2_close = needs_bbox ? "</g>" : "";
    const char *g1_close = (g1_len > 0) ? "</g>" : "";
    const char *footer = "</svg>";
    size_t content_len = strlen(clip->content);
    size_t total = (size_t)header_len + (size_t)g1_len + (size_t)g2_len + content_len +
                   strlen(g2_close) + strlen(g1_close) + strlen(footer) + 1u;
    char *svg = (char *)malloc(total);
    if (!svg) {
        clip->rendering = 0;
        return 0;
    }
    char *dst = svg;
    memcpy(dst, header, (size_t)header_len);
    dst += header_len;
    if (g1_len > 0) {
        memcpy(dst, g1, (size_t)g1_len);
        dst += g1_len;
    }
    if (g2_len > 0) {
        memcpy(dst, g2, (size_t)g2_len);
        dst += g2_len;
    }
    memcpy(dst, clip->content, content_len);
    dst += content_len;
    if (needs_bbox) {
        memcpy(dst, g2_close, strlen(g2_close));
        dst += strlen(g2_close);
    }
    if (g1_len > 0) {
        memcpy(dst, g1_close, strlen(g1_close));
        dst += strlen(g1_close);
    }
    memcpy(dst, footer, strlen(footer));
    dst += strlen(footer);
    *dst = '\0';

    cupidimage_svg_options opts;
    opts.width = out_w;
    opts.height = out_h;
    opts.scale = 1.0f;
    opts.dpi = ctx->dpi;
    opts.animation_time = ctx->anim_time;
    opts.supersampling = 1;
    opts.background_alpha = 0;

    svg_render_ctx clip_ctx;
    memset(&clip_ctx, 0, sizeof(clip_ctx));
    char errbuf[128];
    int ok = cupidimage_svg_render(&clip_ctx, (const unsigned char *)svg, strlen(svg), errbuf,
                                   sizeof(errbuf), &opts);
    free(svg);
    if (!ok) {
        clip->rendering = 0;
        return 0;
    }
    clip->rgba = clip_ctx.hi_rgba;
    clip->mask_w = clip_ctx.width * clip_ctx.ss;
    clip->mask_h = clip_ctx.height * clip_ctx.ss;
    clip->rendered = 1;
    clip->rendering = 0;
    if (needs_bbox) {
        clip->cached_bbox_x = bbox->x;
        clip->cached_bbox_y = bbox->y;
        clip->cached_bbox_w = bbox->w;
        clip->cached_bbox_h = bbox->h;
        clip->has_cached_bbox = 1;
    } else {
        clip->has_cached_bbox = 0;
    }
    return clip->rgba != NULL;
}

static int svg_mask_prepare(svg_mask *mask, const svg_render_ctx *ctx, const svg_bbox *bbox) {
    if (!mask || !ctx || !bbox || !mask->content || !*mask->content) {
        return 0;
    }
    if (mask->rendering) {
        return 0;
    }
    uint32_t out_w = ctx->width * ctx->ss;
    uint32_t out_h = ctx->height * ctx->ss;
    int needs_bbox = (mask->content_units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX);
    if (mask->rendered && mask->rgba && mask->mask_w == out_w && mask->mask_h == out_h) {
        if (!needs_bbox) {
            return 1;
        }
        if (mask->has_cached_bbox && fabsf(mask->cached_bbox_x - bbox->x) < 1e-4f &&
            fabsf(mask->cached_bbox_y - bbox->y) < 1e-4f &&
            fabsf(mask->cached_bbox_w - bbox->w) < 1e-4f &&
            fabsf(mask->cached_bbox_h - bbox->h) < 1e-4f) {
            return 1;
        }
    }
    free(mask->rgba);
    mask->rgba = NULL;
    mask->rendered = 0;
    mask->rendering = 1;

    char header[256];
    int header_len =
        snprintf(header, sizeof(header),
                 "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%u\" height=\"%u\" "
                 "viewBox=\"%.6g %.6g %.6g %.6g\">",
                 out_w, out_h, ctx->vb_x, ctx->vb_y, ctx->vb_w, ctx->vb_h);
    if (header_len < 0 || header_len >= (int)sizeof(header)) {
        mask->rendering = 0;
        return 0;
    }
    char g1[192];
    int g1_len = 0;
    if (mask->has_transform) {
        char mat[96];
        if (cupidimage_svg_matrix_to_string(&mask->transform, mat, sizeof(mat))) {
            g1_len = snprintf(g1, sizeof(g1), "<g transform=\"%s\">", mat);
            if (g1_len < 0 || g1_len >= (int)sizeof(g1)) {
                mask->rendering = 0;
                return 0;
            }
        }
    }
    char g2[192];
    int g2_len = 0;
    if (needs_bbox) {
        float bw = bbox->w > 0.0f ? bbox->w : 1.0f;
        float bh = bbox->h > 0.0f ? bbox->h : 1.0f;
        g2_len = snprintf(g2, sizeof(g2), "<g transform=\"translate(%.6g %.6g) scale(%.6g %.6g)\">",
                          bbox->x, bbox->y, bw, bh);
        if (g2_len < 0 || g2_len >= (int)sizeof(g2)) {
            mask->rendering = 0;
            return 0;
        }
    }
    const char *g2_close = needs_bbox ? "</g>" : "";
    const char *g1_close = (g1_len > 0) ? "</g>" : "";
    const char *footer = "</svg>";
    size_t content_len = strlen(mask->content);
    size_t total = (size_t)header_len + (size_t)g1_len + (size_t)g2_len + content_len +
                   strlen(g2_close) + strlen(g1_close) + strlen(footer) + 1u;
    char *svg = (char *)malloc(total);
    if (!svg) {
        mask->rendering = 0;
        return 0;
    }
    char *dst = svg;
    memcpy(dst, header, (size_t)header_len);
    dst += header_len;
    if (g1_len > 0) {
        memcpy(dst, g1, (size_t)g1_len);
        dst += g1_len;
    }
    if (g2_len > 0) {
        memcpy(dst, g2, (size_t)g2_len);
        dst += g2_len;
    }
    memcpy(dst, mask->content, content_len);
    dst += content_len;
    if (needs_bbox) {
        memcpy(dst, g2_close, strlen(g2_close));
        dst += strlen(g2_close);
    }
    if (g1_len > 0) {
        memcpy(dst, g1_close, strlen(g1_close));
        dst += strlen(g1_close);
    }
    memcpy(dst, footer, strlen(footer));
    dst += strlen(footer);
    *dst = '\0';

    cupidimage_svg_options opts;
    opts.width = out_w;
    opts.height = out_h;
    opts.scale = 1.0f;
    opts.dpi = ctx->dpi;
    opts.animation_time = ctx->anim_time;
    opts.supersampling = 1;
    opts.background_alpha = 0;

    svg_render_ctx mask_ctx;
    memset(&mask_ctx, 0, sizeof(mask_ctx));
    char errbuf[128];
    int ok = cupidimage_svg_render(&mask_ctx, (const unsigned char *)svg, strlen(svg), errbuf,
                                   sizeof(errbuf), &opts);
    free(svg);
    if (!ok) {
        mask->rendering = 0;
        return 0;
    }
    mask->rgba = mask_ctx.hi_rgba;
    mask->mask_w = mask_ctx.width * mask_ctx.ss;
    mask->mask_h = mask_ctx.height * mask_ctx.ss;
    mask->rendered = 1;
    mask->rendering = 0;
    if (needs_bbox) {
        mask->cached_bbox_x = bbox->x;
        mask->cached_bbox_y = bbox->y;
        mask->cached_bbox_w = bbox->w;
        mask->cached_bbox_h = bbox->h;
        mask->has_cached_bbox = 1;
    } else {
        mask->has_cached_bbox = 0;
    }
    return mask->rgba != NULL;
}

uint8_t cupidimage_svg_sample_mask_alpha(svg_style *style, const svg_render_ctx *ctx,
                                         const svg_bbox *bbox, int sx, int sy) {
    if (!style || !ctx || !bbox) {
        return 255;
    }
    uint8_t alpha = 255;
    if (style->clip_path) {
        if (svg_clip_prepare(style->clip_path, ctx, bbox)) {
            if (sx < 0 || sy < 0 || (uint32_t)sx >= style->clip_path->mask_w ||
                (uint32_t)sy >= style->clip_path->mask_h) {
                return 0;
            }
            size_t idx = ((size_t)sy * style->clip_path->mask_w + (size_t)sx) * 4u;
            alpha = style->clip_path->rgba[idx + 3];
        }
    }
    if (style->mask) {
        if (svg_mask_prepare(style->mask, ctx, bbox)) {
            if (sx < 0 || sy < 0 || (uint32_t)sx >= style->mask->mask_w ||
                (uint32_t)sy >= style->mask->mask_h) {
                return 0;
            }
            size_t idx = ((size_t)sy * style->mask->mask_w + (size_t)sx) * 4u;
            uint8_t ma = style->mask->rgba[idx + 3];
            if (style->mask->type == SVG_MASK_TYPE_LUMINANCE) {
                uint8_t mr = style->mask->rgba[idx + 0];
                uint8_t mg = style->mask->rgba[idx + 1];
                uint8_t mb = style->mask->rgba[idx + 2];
                uint8_t lum = (uint8_t)((mr * 54u + mg * 183u + mb * 19u + 128u) >> 8);
                ma = (uint8_t)((lum * ma) / 255u);
            }
            alpha = (uint8_t)((alpha * ma) / 255u);
        }
    }
    return alpha;
}

void cupidimage_svg_composite_buffer(svg_render_ctx *ctx, const uint8_t *src) {
    if (!ctx || !ctx->hi_rgba || !src) {
        return;
    }
    size_t total = (size_t)ctx->width * (size_t)ctx->height * (size_t)ctx->ss * (size_t)ctx->ss;
    for (size_t i = 0; i < total; i++) {
        size_t idx = i * 4u;
        uint8_t sa = src[idx + 3];
        if (sa == 0) {
            continue;
        }
        cupidimage_svg_blend_premul(ctx->hi_rgba + idx, src[idx + 0], src[idx + 1], src[idx + 2],
                                    sa);
    }
}

static int svg_pattern_prepare(svg_pattern *p, const svg_render_ctx *ctx, const svg_bbox *bbox) {
    if (!p || !ctx || !bbox || !p->content || !*p->content) {
        return 0;
    }
    if (p->rendering) {
        return 0;
    }
    float bbox_w = bbox->w > 0.0f ? bbox->w : 1.0f;
    float bbox_h = bbox->h > 0.0f ? bbox->h : 1.0f;
    float base_w = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_w : ctx->vb_w;
    float base_h = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_h : ctx->vb_h;
    float origin_x = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox->x : 0.0f;
    float origin_y = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox->y : 0.0f;
    float tile_x = cupidimage_svg_resolve_grad_coord(&p->x, base_w, origin_x, p->units);
    float tile_y = cupidimage_svg_resolve_grad_coord(&p->y, base_h, origin_y, p->units);
    float tile_w = cupidimage_svg_resolve_grad_coord(&p->width, base_w, 0.0f, p->units);
    float tile_h = cupidimage_svg_resolve_grad_coord(&p->height, base_h, 0.0f, p->units);
    if (tile_w <= 0.0f || tile_h <= 0.0f) {
        return 0;
    }
    uint32_t px_w = (uint32_t)lroundf(tile_w * (float)ctx->ss);
    uint32_t px_h = (uint32_t)lroundf(tile_h * (float)ctx->ss);
    if (px_w < 1)
        px_w = 1;
    if (px_h < 1)
        px_h = 1;
    int needs_bbox = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ||
                     (p->content_units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX);
    if (p->rendered && p->rgba && p->tile_w == px_w && p->tile_h == px_h) {
        if (!needs_bbox) {
            return 1;
        }
        if (p->has_cached_bbox && fabsf(p->cached_bbox_x - bbox->x) < 1e-4f &&
            fabsf(p->cached_bbox_y - bbox->y) < 1e-4f &&
            fabsf(p->cached_bbox_w - bbox->w) < 1e-4f &&
            fabsf(p->cached_bbox_h - bbox->h) < 1e-4f) {
            return 1;
        }
    }

    free(p->rgba);
    p->rgba = NULL;
    p->rendered = 0;
    p->rendering = 1;

    char header[256];
    int header_len =
        snprintf(header, sizeof(header),
                 "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.6g\" height=\"%.6g\" "
                 "viewBox=\"%.6g %.6g %.6g %.6g\">",
                 tile_w, tile_h, tile_x, tile_y, tile_w, tile_h);
    if (header_len < 0 || header_len >= (int)sizeof(header)) {
        p->rendering = 0;
        return 0;
    }
    char gopen[160];
    int gopen_len = 0;
    if (p->content_units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) {
        float cw = bbox->w > 0.0f ? bbox->w : 1.0f;
        float ch = bbox->h > 0.0f ? bbox->h : 1.0f;
        gopen_len = snprintf(gopen, sizeof(gopen),
                             "<g transform=\"translate(%.6g %.6g) scale(%.6g %.6g)\">", bbox->x,
                             bbox->y, cw, ch);
        if (gopen_len < 0 || gopen_len >= (int)sizeof(gopen)) {
            p->rendering = 0;
            return 0;
        }
    }
    const char *gclose = (p->content_units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? "</g>" : "";
    const char *footer = "</svg>";
    size_t content_len = strlen(p->content);
    size_t gclose_len = strlen(gclose);
    size_t footer_len = strlen(footer);
    size_t total =
        (size_t)header_len + (size_t)gopen_len + content_len + gclose_len + footer_len + 1u;
    char *svg = (char *)malloc(total);
    if (!svg) {
        p->rendering = 0;
        return 0;
    }
    char *dst = svg;
    memcpy(dst, header, (size_t)header_len);
    dst += header_len;
    if (gopen_len > 0) {
        memcpy(dst, gopen, (size_t)gopen_len);
        dst += gopen_len;
    }
    if (content_len > 0) {
        memcpy(dst, p->content, content_len);
        dst += content_len;
    }
    if (gclose_len > 0) {
        memcpy(dst, gclose, gclose_len);
        dst += gclose_len;
    }
    memcpy(dst, footer, footer_len);
    dst += footer_len;
    *dst = '\0';

    cupidimage_svg_options opts;
    opts.width = px_w;
    opts.height = px_h;
    opts.scale = 1.0f;
    opts.dpi = ctx->dpi;
    opts.animation_time = ctx->anim_time;
    opts.supersampling = 1;
    opts.background_alpha = 0;

    svg_render_ctx tile_ctx;
    memset(&tile_ctx, 0, sizeof(tile_ctx));
    char errbuf[128];
    int ok = cupidimage_svg_render(&tile_ctx, (const unsigned char *)svg, strlen(svg), errbuf,
                                   sizeof(errbuf), &opts);
    free(svg);
    if (!ok) {
        p->rendering = 0;
        return 0;
    }
    p->rgba = tile_ctx.hi_rgba;
    p->tile_w = tile_ctx.width * tile_ctx.ss;
    p->tile_h = tile_ctx.height * tile_ctx.ss;
    p->rendered = 1;
    p->rendering = 0;
    if (needs_bbox) {
        p->cached_bbox_x = bbox->x;
        p->cached_bbox_y = bbox->y;
        p->cached_bbox_w = bbox->w;
        p->cached_bbox_h = bbox->h;
        p->has_cached_bbox = 1;
    } else {
        p->has_cached_bbox = 0;
    }
    return p->rgba != NULL;
}

static int svg_pattern_sample(svg_pattern *p, const svg_render_ctx *ctx, const svg_bbox *bbox,
                              float x, float y, uint8_t *pr, uint8_t *pg, uint8_t *pb,
                              uint8_t *pa) {
    if (!p || !ctx || !bbox || !pr || !pg || !pb || !pa) {
        return 0;
    }
    if (!svg_pattern_prepare(p, ctx, bbox)) {
        return 0;
    }
    float px = x;
    float py = y;
    if (p->has_transform) {
        svg_matrix_transform_point(&p->inv_transform, &px, &py);
    }
    float bbox_w = bbox->w > 0.0f ? bbox->w : 1.0f;
    float bbox_h = bbox->h > 0.0f ? bbox->h : 1.0f;
    float base_w = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_w : ctx->vb_w;
    float base_h = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_h : ctx->vb_h;
    float origin_x = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox->x : 0.0f;
    float origin_y = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox->y : 0.0f;
    float tile_x = cupidimage_svg_resolve_grad_coord(&p->x, base_w, origin_x, p->units);
    float tile_y = cupidimage_svg_resolve_grad_coord(&p->y, base_h, origin_y, p->units);
    float tile_w = cupidimage_svg_resolve_grad_coord(&p->width, base_w, 0.0f, p->units);
    float tile_h = cupidimage_svg_resolve_grad_coord(&p->height, base_h, 0.0f, p->units);
    if (tile_w <= 0.0f || tile_h <= 0.0f || p->tile_w == 0 || p->tile_h == 0 || !p->rgba) {
        return 0;
    }
    float tx = (px - tile_x) / tile_w;
    float ty = (py - tile_y) / tile_h;
    tx = tx - floorf(tx);
    ty = ty - floorf(ty);
    if (tx < 0.0f)
        tx += 1.0f;
    if (ty < 0.0f)
        ty += 1.0f;
    uint32_t ix = (uint32_t)floorf(tx * (float)p->tile_w);
    uint32_t iy = (uint32_t)floorf(ty * (float)p->tile_h);
    if (ix >= p->tile_w)
        ix = p->tile_w - 1;
    if (iy >= p->tile_h)
        iy = p->tile_h - 1;
    size_t idx = ((size_t)iy * (size_t)p->tile_w + (size_t)ix) * 4u;
    *pr = p->rgba[idx + 0];
    *pg = p->rgba[idx + 1];
    *pb = p->rgba[idx + 2];
    *pa = p->rgba[idx + 3];
    return *pa > 0;
}

static uint32_t svg_gradient_sample(const svg_gradient *g, const svg_render_ctx *ctx,
                                    const svg_bbox *bbox, float x, float y) {
    if (!g || g->stop_count == 0 || !ctx || !bbox) {
        return 0x00000000u;
    }
    float px = x;
    float py = y;
    if (g->has_transform) {
        svg_matrix_transform_point(&g->inv_transform, &px, &py);
    }
    float bbox_w = bbox->w > 0.0f ? bbox->w : 1.0f;
    float bbox_h = bbox->h > 0.0f ? bbox->h : 1.0f;
    float base_w = (g->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_w : ctx->vb_w;
    float base_h = (g->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_h : ctx->vb_h;
    float origin_x = (g->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox->x : 0.0f;
    float origin_y = (g->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox->y : 0.0f;
    if (g->type == SVG_PAINT_LINEAR_GRADIENT) {
        float x1 = cupidimage_svg_resolve_grad_coord(&g->x1, base_w, origin_x, g->units);
        float y1 = cupidimage_svg_resolve_grad_coord(&g->y1, base_h, origin_y, g->units);
        float x2 = cupidimage_svg_resolve_grad_coord(&g->x2, base_w, origin_x, g->units);
        float y2 = cupidimage_svg_resolve_grad_coord(&g->y2, base_h, origin_y, g->units);
        float dx = x2 - x1;
        float dy = y2 - y1;
        float denom = dx * dx + dy * dy;
        float t = 0.0f;
        if (denom > 1e-12f) {
            t = ((px - x1) * dx + (py - y1) * dy) / denom;
        }
        t = cupidimage_svg_apply_spread(t, g->spread);
        return cupidimage_svg_gradient_stop_color(g, t);
    }
    float cx = cupidimage_svg_resolve_grad_coord(&g->cx, base_w, origin_x, g->units);
    float cy = cupidimage_svg_resolve_grad_coord(&g->cy, base_h, origin_y, g->units);
    float r = cupidimage_svg_resolve_grad_coord(&g->r, fmaxf(base_w, base_h), 0.0f, g->units);
    if (r <= 1e-6f) {
        return cupidimage_svg_gradient_stop_color(g, 1.0f);
    }
    float fx = cupidimage_svg_resolve_grad_coord(&g->fx, base_w, origin_x, g->units);
    float fy = cupidimage_svg_resolve_grad_coord(&g->fy, base_h, origin_y, g->units);
    float vx = px - fx;
    float vy = py - fy;
    float t = 0.0f;
    float vlen2 = vx * vx + vy * vy;
    if (vlen2 <= 1e-12f) {
        t = 0.0f;
    } else {
        float dx = fx - cx;
        float dy = fy - cy;
        float a = vlen2;
        float b = 2.0f * (dx * vx + dy * vy);
        float c = dx * dx + dy * dy - r * r;
        float disc = b * b - 4.0f * a * c;
        if (disc >= 0.0f) {
            float sqrt_disc = sqrtf(disc);
            float s1 = (-b + sqrt_disc) / (2.0f * a);
            float s2 = (-b - sqrt_disc) / (2.0f * a);
            float s = fmaxf(s1, s2);
            if (s > 0.0f) {
                t = 1.0f / s;
            } else {
                t = sqrtf(vlen2) / r;
            }
        } else {
            t = sqrtf(vlen2) / r;
        }
    }
    t = cupidimage_svg_apply_spread(t, g->spread);
    return cupidimage_svg_gradient_stop_color(g, t);
}

int cupidimage_svg_prepare_const_paint(const svg_style *style, int is_stroke, uint8_t *pr,
                                       uint8_t *pg, uint8_t *pb, uint8_t *pa) {
    if (!style || !pr || !pg || !pb || !pa) {
        return 0;
    }
    svg_paint_type type = is_stroke ? style->stroke_type : style->fill_type;
    if (type != SVG_PAINT_COLOR) {
        return 0;
    }
    uint32_t color = is_stroke ? style->stroke_color : style->fill_color;
    uint8_t a = (uint8_t)(color & 0xFFu);
    float opacity = style->opacity * (is_stroke ? style->stroke_opacity : style->fill_opacity);
    if (opacity < 0.0f)
        opacity = 0.0f;
    if (opacity > 1.0f)
        opacity = 1.0f;
    float alpha = (a / 255.0f) * opacity;
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;
    uint8_t oa = (uint8_t)lroundf(alpha * 255.0f);
    uint8_t r = (uint8_t)(color >> 24);
    uint8_t g = (uint8_t)(color >> 16);
    uint8_t b = (uint8_t)(color >> 8);
    *pa = oa;
    *pr = (uint8_t)((r * oa) / 255u);
    *pg = (uint8_t)((g * oa) / 255u);
    *pb = (uint8_t)((b * oa) / 255u);
    return oa > 0;
}

int cupidimage_svg_sample_paint(const svg_style *style, const svg_render_ctx *ctx, int is_stroke,
                                const svg_bbox *bbox, float x, float y, uint8_t *pr, uint8_t *pg,
                                uint8_t *pb, uint8_t *pa) {
    if (!style || !ctx || !bbox || !pr || !pg || !pb || !pa) {
        return 0;
    }
    svg_paint_type type = is_stroke ? style->stroke_type : style->fill_type;
    if (type == SVG_PAINT_NONE) {
        return 0;
    }
    if (type == SVG_PAINT_PATTERN) {
        svg_pattern *pat = is_stroke ? style->stroke_pattern : style->fill_pattern;
        uint8_t sr = 0, sg = 0, sb = 0, sa = 0;
        if (!svg_pattern_sample(pat, ctx, bbox, x, y, &sr, &sg, &sb, &sa)) {
            return 0;
        }
        float opacity = style->opacity * (is_stroke ? style->stroke_opacity : style->fill_opacity);
        if (opacity < 0.0f)
            opacity = 0.0f;
        if (opacity > 1.0f)
            opacity = 1.0f;
        uint8_t oa = (uint8_t)lroundf(sa * opacity);
        if (oa == 0) {
            return 0;
        }
        *pa = oa;
        *pr = (uint8_t)lroundf(sr * opacity);
        *pg = (uint8_t)lroundf(sg * opacity);
        *pb = (uint8_t)lroundf(sb * opacity);
        return 1;
    }
    uint32_t color = 0;
    if (type == SVG_PAINT_COLOR) {
        color = is_stroke ? style->stroke_color : style->fill_color;
    } else {
        const svg_gradient *grad = is_stroke ? style->stroke_gradient : style->fill_gradient;
        color = svg_gradient_sample(grad, ctx, bbox, x, y);
    }
    uint8_t a = (uint8_t)(color & 0xFFu);
    if (a == 0) {
        return 0;
    }
    float opacity = style->opacity * (is_stroke ? style->stroke_opacity : style->fill_opacity);
    if (opacity < 0.0f)
        opacity = 0.0f;
    if (opacity > 1.0f)
        opacity = 1.0f;
    float alpha = (a / 255.0f) * opacity;
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;
    uint8_t oa = (uint8_t)lroundf(alpha * 255.0f);
    if (oa == 0) {
        return 0;
    }
    uint8_t r = (uint8_t)(color >> 24);
    uint8_t g = (uint8_t)(color >> 16);
    uint8_t b = (uint8_t)(color >> 8);
    *pa = oa;
    *pr = (uint8_t)((r * oa) / 255u);
    *pg = (uint8_t)((g * oa) / 255u);
    *pb = (uint8_t)((b * oa) / 255u);
    return 1;
}

int cupidimage_svg_xml_space_preserve(const svg_tag *tag, int inherited) {
    const char *space = cupidimage_svg_get_attr_exact(tag, "xml:space");
    if (!space) {
        return inherited;
    }
    if (svg_strcasecmp(space, "preserve") == 0) {
        return 1;
    }
    if (svg_strcasecmp(space, "default") == 0) {
        return 0;
    }
    return inherited;
}

static int svg_decode_entity(const char *s, size_t *consumed) {
    if (!s || s[0] != '&') {
        return -1;
    }
    const char *p = s + 1;
    if (strncmp(p, "lt;", 3) == 0) {
        *consumed = 4;
        return '<';
    }
    if (strncmp(p, "gt;", 3) == 0) {
        *consumed = 4;
        return '>';
    }
    if (strncmp(p, "amp;", 4) == 0) {
        *consumed = 5;
        return '&';
    }
    if (strncmp(p, "quot;", 5) == 0) {
        *consumed = 6;
        return '"';
    }
    if (strncmp(p, "apos;", 5) == 0) {
        *consumed = 6;
        return '\'';
    }
    if (*p == '#') {
        p++;
        int base = 10;
        if (*p == 'x' || *p == 'X') {
            base = 16;
            p++;
        }
        char *end = NULL;
        long v = strtol(p, &end, base);
        if (end && *end == ';') {
            *consumed = (size_t)(end - s + 1);
            if (v < 0 || v > 255) {
                return '?';
            }
            return (int)v;
        }
    }
    return -1;
}

char *cupidimage_svg_text_normalize(const char *text, int preserve) {
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    size_t w = 0;
    int last_space = 0;
    for (size_t i = 0; i < len;) {
        char ch = text[i];
        if (ch == '&') {
            size_t consumed = 0;
            int decoded = svg_decode_entity(text + i, &consumed);
            if (decoded >= 0) {
                ch = (char)decoded;
                i += consumed;
            } else {
                i++;
            }
        } else {
            i++;
        }
        if (ch == '\r') {
            ch = '\n';
        } else if (ch == '\t') {
            ch = ' ';
        }
        if (!preserve) {
            if (isspace((unsigned char)ch)) {
                if (last_space || w == 0) {
                    continue;
                }
                ch = ' ';
                last_space = 1;
            } else {
                last_space = 0;
            }
        }
        out[w++] = ch;
    }
    if (!preserve) {
        while (w > 0 && out[w - 1] == ' ') {
            w--;
        }
    }
    out[w] = '\0';
    return out;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
