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

void cupidimage_svg_draw_samples_rect(svg_render_ctx *ctx, const svg_style *style,
                                      const svg_matrix *m, float x, float y, float w, float h,
                                      float rx, float ry) {
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }
    float half = style->stroke_width * 0.5f;
    float minx = x - half;
    float miny = y - half;
    float maxx = x + w + half;
    float maxy = y + h + half;
    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    cupidimage_svg_transform_bounds(m, minx, miny, maxx, maxy, &dev_minx, &dev_miny, &dev_maxx,
                                    &dev_maxy);
    if (dev_maxx < 0.0f || dev_maxy < 0.0f || dev_minx > (float)ctx->width ||
        dev_miny > (float)ctx->height) {
        return;
    }
    svg_matrix inv;
    if (!svg_matrix_invert(m, &inv)) {
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
    float inner_rx = rx > half ? rx - half : 0.0f;
    float inner_ry = ry > half ? ry - half : 0.0f;

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

    svg_bbox bbox = {x, y, w, h};
    int has_fill = style->fill_type != SVG_PAINT_NONE;
    int has_stroke = style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f;
    int need_segment_stroke =
        has_stroke && (style->stroke_dashcount > 0 || style->stroke_linecap != SVG_LINECAP_BUTT ||
                       style->stroke_linejoin != SVG_LINEJOIN_MITER);
    int has_stroke_analytic = has_stroke && !need_segment_stroke;
    int fill_is_color = style->fill_type == SVG_PAINT_COLOR;
    int stroke_is_color = style->stroke_type == SVG_PAINT_COLOR;
    uint8_t fill_pr = 0, fill_pg = 0, fill_pb = 0, fill_pa = 0;
    uint8_t stroke_pr = 0, stroke_pg = 0, stroke_pb = 0, stroke_pa = 0;
    if (fill_is_color) {
        cupidimage_svg_prepare_const_paint(style, 0, &fill_pr, &fill_pg, &fill_pb, &fill_pa);
    }
    if (stroke_is_color) {
        cupidimage_svg_prepare_const_paint(style, 1, &stroke_pr, &stroke_pg, &stroke_pb,
                                           &stroke_pa);
    }

    float inner_x = x + half;
    float inner_y = y + half;
    float inner_w = w - style->stroke_width;
    float inner_h = h - style->stroke_width;

    for (int sy = sy0; sy <= sy1; sy++) {
        float dy = ((float)sy + 0.5f) / (float)ss;
        for (int sx = sx0; sx <= sx1; sx++) {
            float dx = ((float)sx + 0.5f) / (float)ss;
            float lx = dx;
            float ly = dy;
            svg_matrix_transform_point(&inv, &lx, &ly);
            int inside = 0;
            if (rx <= 0.0f || ry <= 0.0f) {
                if (lx >= x && lx <= x + w && ly >= y && ly <= y + h) {
                    inside = 1;
                }
            } else {
                float inner_left = x + rx;
                float inner_right = x + w - rx;
                float inner_top = y + ry;
                float inner_bottom = y + h - ry;
                if ((lx >= inner_left && lx <= inner_right && ly >= y && ly <= y + h) ||
                    (lx >= x && lx <= x + w && ly >= inner_top && ly <= inner_bottom)) {
                    inside = 1;
                } else {
                    float cx = lx < inner_left ? inner_left : inner_right;
                    float cy = ly < inner_top ? inner_top : inner_bottom;
                    float dx0 = lx - cx;
                    float dy0 = ly - cy;
                    float nx = dx0 / rx;
                    float ny = dy0 / ry;
                    if (nx * nx + ny * ny <= 1.0f) {
                        inside = 1;
                    }
                }
            }
            int in_stroke = 0;
            if (has_stroke_analytic) {
                if (inner_w <= 0.0f || inner_h <= 0.0f) {
                    in_stroke = inside;
                } else {
                    int inside_inner = 0;
                    if (inner_rx <= 0.0f || inner_ry <= 0.0f) {
                        if (lx >= inner_x && lx <= inner_x + inner_w && ly >= inner_y &&
                            ly <= inner_y + inner_h) {
                            inside_inner = 1;
                        }
                    } else {
                        float inner_left = inner_x + inner_rx;
                        float inner_right = inner_x + inner_w - inner_rx;
                        float inner_top = inner_y + inner_ry;
                        float inner_bottom = inner_y + inner_h - inner_ry;
                        if ((lx >= inner_left && lx <= inner_right && ly >= inner_y &&
                             ly <= inner_y + inner_h) ||
                            (lx >= inner_x && lx <= inner_x + inner_w && ly >= inner_top &&
                             ly <= inner_bottom)) {
                            inside_inner = 1;
                        } else {
                            float cx = lx < inner_left ? inner_left : inner_right;
                            float cy = ly < inner_top ? inner_top : inner_bottom;
                            float dx0 = lx - cx;
                            float dy0 = ly - cy;
                            float nx = dx0 / inner_rx;
                            float ny = dy0 / inner_ry;
                            if (nx * nx + ny * ny <= 1.0f) {
                                inside_inner = 1;
                            }
                        }
                    }
                    if (inside && !inside_inner) {
                        in_stroke = 1;
                    }
                }
            }
            uint8_t *dst =
                ctx->hi_rgba + ((size_t)sy * (size_t)(ctx->width * ss) + (size_t)sx) * 4u;
            uint8_t mask_alpha = 255;
            if ((has_fill && inside) || (has_stroke_analytic && in_stroke)) {
                mask_alpha =
                    cupidimage_svg_sample_mask_alpha((svg_style *)style, ctx, &bbox, sx, sy);
            }
            if (has_fill && inside && mask_alpha > 0) {
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
                    uint8_t pr, pg, pb, pa;
                    if (cupidimage_svg_sample_paint(style, ctx, 0, &bbox, lx, ly, &pr, &pg, &pb,
                                                    &pa)) {
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
            if (has_stroke_analytic && in_stroke && mask_alpha > 0) {
                if (stroke_is_color) {
                    if (stroke_pa > 0) {
                        uint8_t pr = stroke_pr;
                        uint8_t pg = stroke_pg;
                        uint8_t pb = stroke_pb;
                        uint8_t pa = stroke_pa;
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
                    uint8_t pr, pg, pb, pa;
                    if (cupidimage_svg_sample_paint(style, ctx, 1, &bbox, lx, ly, &pr, &pg, &pb,
                                                    &pa)) {
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
    if (need_segment_stroke) {
        svg_segments segs;
        cupidimage_svg_segments_init(&segs);
        cupidimage_svg_segments_add_rect(&segs, x, y, w, h, rx, ry);
        svg_style stroke_style = *style;
        stroke_style.fill_type = SVG_PAINT_NONE;
        stroke_style.fill_pattern = NULL;
        cupidimage_svg_draw_samples_segments(ctx, &stroke_style, m, &segs);
        cupidimage_svg_segments_free(&segs);
    }
}

void cupidimage_svg_draw_samples_ellipse(svg_render_ctx *ctx, const svg_style *style,
                                         const svg_matrix *m, float cx, float cy, float rx,
                                         float ry) {
    if (rx <= 0.0f || ry <= 0.0f) {
        return;
    }
    float half = style->stroke_width * 0.5f;
    float minx = cx - rx - half;
    float miny = cy - ry - half;
    float maxx = cx + rx + half;
    float maxy = cy + ry + half;
    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    cupidimage_svg_transform_bounds(m, minx, miny, maxx, maxy, &dev_minx, &dev_miny, &dev_maxx,
                                    &dev_maxy);
    if (dev_maxx < 0.0f || dev_maxy < 0.0f || dev_minx > (float)ctx->width ||
        dev_miny > (float)ctx->height) {
        return;
    }
    svg_matrix inv;
    if (!svg_matrix_invert(m, &inv)) {
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

    svg_bbox bbox = {cx - rx, cy - ry, rx * 2.0f, ry * 2.0f};
    int has_fill = style->fill_type != SVG_PAINT_NONE;
    int has_stroke = style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f;
    int need_segment_stroke =
        has_stroke && (style->stroke_dashcount > 0 || style->stroke_linecap != SVG_LINECAP_BUTT ||
                       style->stroke_linejoin != SVG_LINEJOIN_MITER);
    int has_stroke_analytic = has_stroke && !need_segment_stroke;
    int fill_is_color = style->fill_type == SVG_PAINT_COLOR;
    int stroke_is_color = style->stroke_type == SVG_PAINT_COLOR;
    uint8_t fill_pr = 0, fill_pg = 0, fill_pb = 0, fill_pa = 0;
    uint8_t stroke_pr = 0, stroke_pg = 0, stroke_pb = 0, stroke_pa = 0;
    if (fill_is_color) {
        cupidimage_svg_prepare_const_paint(style, 0, &fill_pr, &fill_pg, &fill_pb, &fill_pa);
    }
    if (stroke_is_color) {
        cupidimage_svg_prepare_const_paint(style, 1, &stroke_pr, &stroke_pg, &stroke_pb,
                                           &stroke_pa);
    }

    float rx_scale = 1.0f / rx;
    float ry_scale = 1.0f / ry;
    float max_r = fmaxf(rx, ry);
    float half_norm = max_r > 0.0f ? half / max_r : 0.0f;

    for (int sy = sy0; sy <= sy1; sy++) {
        float dy = ((float)sy + 0.5f) / (float)ss;
        for (int sx = sx0; sx <= sx1; sx++) {
            float dx = ((float)sx + 0.5f) / (float)ss;
            float lx = dx;
            float ly = dy;
            svg_matrix_transform_point(&inv, &lx, &ly);
            float ux = (lx - cx) * rx_scale;
            float uy = (ly - cy) * ry_scale;
            float t = ux * ux + uy * uy;
            int inside = t <= 1.0f;
            int in_stroke = 0;
            if (has_stroke_analytic) {
                float dist = fabsf(sqrtf(t) - 1.0f);
                if (dist <= half_norm) {
                    in_stroke = 1;
                }
            }
            uint8_t *dst =
                ctx->hi_rgba + ((size_t)sy * (size_t)(ctx->width * ss) + (size_t)sx) * 4u;
            uint8_t mask_alpha = 255;
            if ((has_fill && inside) || (has_stroke_analytic && in_stroke)) {
                mask_alpha =
                    cupidimage_svg_sample_mask_alpha((svg_style *)style, ctx, &bbox, sx, sy);
            }
            if (has_fill && inside && mask_alpha > 0) {
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
                    uint8_t pr, pg, pb, pa;
                    if (cupidimage_svg_sample_paint(style, ctx, 0, &bbox, lx, ly, &pr, &pg, &pb,
                                                    &pa)) {
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
            if (has_stroke_analytic && in_stroke && mask_alpha > 0) {
                if (stroke_is_color) {
                    if (stroke_pa > 0) {
                        uint8_t pr = stroke_pr;
                        uint8_t pg = stroke_pg;
                        uint8_t pb = stroke_pb;
                        uint8_t pa = stroke_pa;
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
                    uint8_t pr, pg, pb, pa;
                    if (cupidimage_svg_sample_paint(style, ctx, 1, &bbox, lx, ly, &pr, &pg, &pb,
                                                    &pa)) {
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
    if (need_segment_stroke) {
        svg_segments segs;
        cupidimage_svg_segments_init(&segs);
        cupidimage_svg_segments_add_ellipse(&segs, cx, cy, rx, ry);
        svg_style stroke_style = *style;
        stroke_style.fill_type = SVG_PAINT_NONE;
        stroke_style.fill_pattern = NULL;
        cupidimage_svg_draw_samples_segments(ctx, &stroke_style, m, &segs);
        cupidimage_svg_segments_free(&segs);
    }
}

void cupidimage_svg_draw_samples_line(svg_render_ctx *ctx, const svg_style *style,
                                      const svg_matrix *m, float x0, float y0, float x1, float y1) {
    if (style->stroke_type == SVG_PAINT_NONE || style->stroke_width <= 0.0f) {
        return;
    }
    svg_segments segs;
    cupidimage_svg_segments_init(&segs);
    cupidimage_svg_segments_add(&segs, x0, y0, x1, y1, 1, 0);
    cupidimage_svg_draw_samples_segments(ctx, style, m, &segs);
    cupidimage_svg_segments_free(&segs);
}

void cupidimage_svg_draw_samples_image(svg_render_ctx *ctx, const svg_style *style,
                                       const svg_matrix *m, const cupidimage_image *img, float x,
                                       float y, float w, float h, int preserve_none, float align_x,
                                       float align_y) {
    if (!ctx || !style || !m || !img || !img->rgba || img->width == 0 || img->height == 0) {
        return;
    }
    if (style->opacity <= 0.0f || w <= 0.0f || h <= 0.0f) {
        return;
    }

    float draw_w = w;
    float draw_h = h;
    float draw_x = x;
    float draw_y = y;
    if (!preserve_none) {
        float sx = draw_w / (float)img->width;
        float sy = draw_h / (float)img->height;
        float s = fminf(sx, sy);
        if (s <= 0.0f) {
            return;
        }
        float used_w = (float)img->width * s;
        float used_h = (float)img->height * s;
        draw_x += (draw_w - used_w) * align_x;
        draw_y += (draw_h - used_h) * align_y;
        draw_w = used_w;
        draw_h = used_h;
    }

    svg_matrix image_m;
    svg_matrix_identity(&image_m);
    svg_matrix_scale(&image_m, draw_w / (float)img->width, draw_h / (float)img->height);
    svg_matrix_translate(&image_m, draw_x, draw_y);
    svg_matrix full_m;
    svg_matrix_multiply(m, &image_m, &full_m);

    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    cupidimage_svg_transform_bounds(&full_m, 0.0f, 0.0f, (float)img->width, (float)img->height,
                                    &dev_minx, &dev_miny, &dev_maxx, &dev_maxy);
    if (dev_maxx < 0.0f || dev_maxy < 0.0f || dev_minx > (float)ctx->width ||
        dev_miny > (float)ctx->height) {
        return;
    }

    svg_matrix inv;
    if (!svg_matrix_invert(&full_m, &inv)) {
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

    svg_bbox bbox = {draw_x, draw_y, draw_w, draw_h};
    float opacity = cupidimage_svg_clamp01(style->opacity);
    for (int sy = sy0; sy <= sy1; sy++) {
        float dy = ((float)sy + 0.5f) / (float)ss;
        for (int sx = sx0; sx <= sx1; sx++) {
            float dx = ((float)sx + 0.5f) / (float)ss;
            float ix = dx;
            float iy = dy;
            svg_matrix_transform_point(&inv, &ix, &iy);
            if (ix < 0.0f || iy < 0.0f || ix >= (float)img->width || iy >= (float)img->height) {
                continue;
            }
            int px = (int)floorf(ix);
            int py = (int)floorf(iy);
            if (px < 0 || py < 0 || px >= (int)img->width || py >= (int)img->height) {
                continue;
            }
            size_t sidx = ((size_t)py * (size_t)img->width + (size_t)px) * 4u;
            uint8_t sa = (uint8_t)lroundf((img->rgba[sidx + 3] / 255.0f) * opacity * 255.0f);
            if (sa == 0) {
                continue;
            }
            uint8_t pr = (uint8_t)((img->rgba[sidx + 0] * sa) / 255u);
            uint8_t pg = (uint8_t)((img->rgba[sidx + 1] * sa) / 255u);
            uint8_t pb = (uint8_t)((img->rgba[sidx + 2] * sa) / 255u);
            uint8_t mask_alpha =
                cupidimage_svg_sample_mask_alpha((svg_style *)style, ctx, &bbox, sx, sy);
            if (mask_alpha < 255) {
                pr = (uint8_t)((pr * mask_alpha) / 255u);
                pg = (uint8_t)((pg * mask_alpha) / 255u);
                pb = (uint8_t)((pb * mask_alpha) / 255u);
                sa = (uint8_t)((sa * mask_alpha) / 255u);
            }
            if (sa == 0) {
                continue;
            }
            uint8_t *dst =
                ctx->hi_rgba + ((size_t)sy * (size_t)(ctx->width * ss) + (size_t)sx) * 4u;
            cupidimage_svg_blend_premul(dst, pr, pg, pb, sa);
        }
    }
}

void cupidimage_svg_draw_samples_segments(svg_render_ctx *ctx, const svg_style *style,
                                          const svg_matrix *m, const svg_segments *segs) {
    if (segs->stroke_count == 0 && segs->fill_count == 0) {
        return;
    }
    float half = style->stroke_width * 0.5f;
    float minx = segs->minx - half;
    float miny = segs->miny - half;
    float maxx = segs->maxx + half;
    float maxy = segs->maxy + half;
    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    cupidimage_svg_transform_bounds(m, minx, miny, maxx, maxy, &dev_minx, &dev_miny, &dev_maxx,
                                    &dev_maxy);
    if (dev_maxx < 0.0f || dev_maxy < 0.0f || dev_minx > (float)ctx->width ||
        dev_miny > (float)ctx->height) {
        return;
    }
    svg_matrix inv;
    if (!svg_matrix_invert(m, &inv)) {
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

    svg_bbox bbox;
    bbox.x = segs->minx;
    bbox.y = segs->miny;
    bbox.w = segs->maxx - segs->minx;
    bbox.h = segs->maxy - segs->miny;
    int has_fill = style->fill_type != SVG_PAINT_NONE && segs->fill_count > 0;
    int has_stroke = style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f &&
                     segs->stroke_count > 0;
    svg_segments dash_segs;
    int using_dash = 0;
    const svg_segments *stroke_segs = segs;
    if (has_stroke && style->stroke_dashcount > 0) {
        if (cupidimage_svg_segments_build_dashed(segs, style, &dash_segs)) {
            stroke_segs = &dash_segs;
            using_dash = 1;
        } else {
            has_stroke = 0;
        }
    }
    if (has_stroke && stroke_segs->stroke_count == 0) {
        has_stroke = 0;
    }
    int fill_is_color = style->fill_type == SVG_PAINT_COLOR;
    int stroke_is_color = style->stroke_type == SVG_PAINT_COLOR;
    uint8_t fill_pr = 0, fill_pg = 0, fill_pb = 0, fill_pa = 0;
    uint8_t stroke_pr = 0, stroke_pg = 0, stroke_pb = 0, stroke_pa = 0;
    if (fill_is_color) {
        cupidimage_svg_prepare_const_paint(style, 0, &fill_pr, &fill_pg, &fill_pb, &fill_pa);
    }
    if (stroke_is_color) {
        cupidimage_svg_prepare_const_paint(style, 1, &stroke_pr, &stroke_pg, &stroke_pb,
                                           &stroke_pa);
    }

    uint8_t *start_open = NULL;
    uint8_t *end_open = NULL;
    if (has_stroke) {
        start_open = (uint8_t *)malloc(stroke_segs->stroke_count);
        end_open = (uint8_t *)malloc(stroke_segs->stroke_count);
        if (start_open && end_open) {
            cupidimage_svg_segments_endpoint_flags(stroke_segs, start_open, end_open);
        } else {
            free(start_open);
            free(end_open);
            start_open = NULL;
            end_open = NULL;
        }
    }
    svg_join_tri *join_tris = NULL;
    int join_count = 0;
    if (has_stroke && style->stroke_linejoin != SVG_LINEJOIN_ROUND) {
        cupidimage_svg_build_join_tris(stroke_segs, style, start_open, end_open, &join_tris,
                                       &join_count);
    }

    for (int sy = sy0; sy <= sy1; sy++) {
        float dy = ((float)sy + 0.5f) / (float)ss;
        for (int sx = sx0; sx <= sx1; sx++) {
            float dx = ((float)sx + 0.5f) / (float)ss;
            float lx = dx;
            float ly = dy;
            svg_matrix_transform_point(&inv, &lx, &ly);
            int inside = 0;
            int in_stroke = 0;
            if (has_fill) {
                inside = cupidimage_svg_point_in_segments(segs, style->fill_rule_evenodd, lx, ly);
            }
            if (has_stroke) {
                if (start_open && end_open) {
                    in_stroke = cupidimage_svg_point_on_segments_cap(stroke_segs, style, lx, ly,
                                                                     half, start_open, end_open,
                                                                     join_tris, join_count);
                } else {
                    in_stroke = cupidimage_svg_point_on_segments(stroke_segs, lx, ly, half);
                }
            }
            uint8_t *dst =
                ctx->hi_rgba + ((size_t)sy * (size_t)(ctx->width * ss) + (size_t)sx) * 4u;
            uint8_t mask_alpha = 255;
            if ((has_fill && inside) || (has_stroke && in_stroke)) {
                mask_alpha =
                    cupidimage_svg_sample_mask_alpha((svg_style *)style, ctx, &bbox, sx, sy);
            }
            if (has_fill && inside && mask_alpha > 0) {
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
                    uint8_t pr, pg, pb, pa;
                    if (cupidimage_svg_sample_paint(style, ctx, 0, &bbox, lx, ly, &pr, &pg, &pb,
                                                    &pa)) {
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
            if (has_stroke && in_stroke && mask_alpha > 0) {
                if (stroke_is_color) {
                    if (stroke_pa > 0) {
                        uint8_t pr = stroke_pr;
                        uint8_t pg = stroke_pg;
                        uint8_t pb = stroke_pb;
                        uint8_t pa = stroke_pa;
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
                    uint8_t pr, pg, pb, pa;
                    if (cupidimage_svg_sample_paint(style, ctx, 1, &bbox, lx, ly, &pr, &pg, &pb,
                                                    &pa)) {
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
    free(start_open);
    free(end_open);
    free(join_tris);
    if (using_dash) {
        cupidimage_svg_segments_free(&dash_segs);
    }
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
