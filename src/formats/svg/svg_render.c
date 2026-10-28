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

static int svg_render_transformed_snippet(svg_render_ctx *ctx, const char *content,
                                          const svg_matrix *gm, float dpi);
static float svg_segment_angle_deg(const svg_segment *seg);
static float svg_join_angle_deg(const svg_segment *prev, const svg_segment *next);
static void svg_draw_marker_instance(svg_render_ctx *ctx, const svg_marker *mk, const svg_matrix *m,
                                     float px, float py, float angle_deg, float stroke_width,
                                     float dpi);
static void svg_draw_markers_for_segments(svg_render_ctx *ctx, const svg_style *style,
                                          const svg_matrix *m, const svg_segments *segs, float dpi);
static int svg_draw_shape(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                          const svg_tag *tag, const char *local, float dpi, char *child_start,
                          float anim_time, const svg_defs *defs, svg_bbox *out_bbox);

static int svg_render_transformed_snippet(svg_render_ctx *ctx, const char *content,
                                          const svg_matrix *gm, float dpi) {
    if (!ctx || !content || !*content || !gm) {
        return 0;
    }
    float vb_x = ctx->vb_x;
    float vb_y = ctx->vb_y;
    float vb_w = ctx->vb_w > 0.0f ? ctx->vb_w : (float)ctx->width;
    float vb_h = ctx->vb_h > 0.0f ? ctx->vb_h : (float)ctx->height;
    int header_len = snprintf(
        NULL, 0,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.6f\" height=\"%.6f\" "
        "viewBox=\"%.6f %.6f %.6f %.6f\"><g transform=\"matrix(%.9g %.9g %.9g %.9g %.9g %.9g)\">",
        vb_w, vb_h, vb_x, vb_y, vb_w, vb_h, gm->a, gm->b, gm->c, gm->d, gm->e, gm->f);
    if (header_len <= 0) {
        return 0;
    }
    const char *footer = "</g></svg>";
    size_t content_len = strlen(content);
    size_t total = (size_t)header_len + content_len + strlen(footer) + 1u;
    char *svg = (char *)malloc(total);
    if (!svg) {
        return 0;
    }
    char *dst = svg;
    dst += (size_t)snprintf(
        dst, (size_t)header_len + 1u,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.6f\" height=\"%.6f\" "
        "viewBox=\"%.6f %.6f %.6f %.6f\"><g transform=\"matrix(%.9g %.9g %.9g %.9g %.9g %.9g)\">",
        vb_w, vb_h, vb_x, vb_y, vb_w, vb_h, gm->a, gm->b, gm->c, gm->d, gm->e, gm->f);
    memcpy(dst, content, content_len);
    dst += content_len;
    memcpy(dst, footer, strlen(footer));
    dst += strlen(footer);
    *dst = '\0';

    cupidimage_svg_options opts;
    opts.width = ctx->width;
    opts.height = ctx->height;
    opts.scale = 1.0f;
    opts.dpi = dpi;
    opts.animation_time = ctx->anim_time;
    opts.supersampling = (uint8_t)ctx->ss;
    opts.background_alpha = 0;

    svg_render_ctx tmp_ctx;
    memset(&tmp_ctx, 0, sizeof(tmp_ctx));
    char errbuf[128];
    int ok = cupidimage_svg_render(&tmp_ctx, (const unsigned char *)svg, strlen(svg), errbuf,
                                   sizeof(errbuf), &opts);
    free(svg);
    if (!ok || !tmp_ctx.hi_rgba) {
        free(tmp_ctx.hi_rgba);
        return 0;
    }
    cupidimage_svg_composite_buffer(ctx, tmp_ctx.hi_rgba);
    free(tmp_ctx.hi_rgba);
    return 1;
}

static float svg_segment_angle_deg(const svg_segment *seg) {
    if (!seg) {
        return 0.0f;
    }
    return atan2f(seg->y1 - seg->y0, seg->x1 - seg->x0) * (180.0f / (float)M_PI);
}

static float svg_join_angle_deg(const svg_segment *prev, const svg_segment *next) {
    if (!prev || !next) {
        return 0.0f;
    }
    float dx0 = prev->x1 - prev->x0;
    float dy0 = prev->y1 - prev->y0;
    float dx1 = next->x1 - next->x0;
    float dy1 = next->y1 - next->y0;
    float l0 = sqrtf(dx0 * dx0 + dy0 * dy0);
    float l1 = sqrtf(dx1 * dx1 + dy1 * dy1);
    if (l1 <= 1e-6f) {
        return svg_segment_angle_deg(prev);
    }
    if (l0 <= 1e-6f) {
        return svg_segment_angle_deg(next);
    }
    dx0 /= l0;
    dy0 /= l0;
    dx1 /= l1;
    dy1 /= l1;
    float bx = dx0 + dx1;
    float by = dy0 + dy1;
    float bl = sqrtf(bx * bx + by * by);
    if (bl <= 1e-6f) {
        return svg_segment_angle_deg(next);
    }
    return atan2f(by, bx) * (180.0f / (float)M_PI);
}

static void svg_draw_marker_instance(svg_render_ctx *ctx, const svg_marker *mk, const svg_matrix *m,
                                     float px, float py, float angle_deg, float stroke_width,
                                     float dpi) {
    if (!ctx || !mk || !m || !mk->content || !*mk->content) {
        return;
    }
    float mw = mk->marker_w > 0.0f ? mk->marker_w : 3.0f;
    float mh = mk->marker_h > 0.0f ? mk->marker_h : 3.0f;
    float vbw = mk->vb_ok && mk->vb_w > 0.0f ? mk->vb_w : mw;
    float vbh = mk->vb_ok && mk->vb_h > 0.0f ? mk->vb_h : mh;
    if (vbw <= 0.0f || vbh <= 0.0f) {
        return;
    }
    float sx = mw / vbw;
    float sy = mh / vbh;
    float ax = 0.0f;
    float ay = 0.0f;
    if (!mk->preserve_none) {
        float uni = fminf(sx, sy);
        ax = (mw - vbw * uni) * mk->align_x;
        ay = (mh - vbh * uni) * mk->align_y;
        sx = uni;
        sy = uni;
    }
    if (mk->units_stroke_width) {
        float sw = stroke_width > 0.0f ? stroke_width : 1.0f;
        sx *= sw;
        sy *= sw;
    }
    float orient = mk->orient_auto ? angle_deg : mk->orient_angle;

    svg_matrix gm = *m;
    svg_matrix t;
    svg_matrix_identity(&t);
    t.e = px;
    t.f = py;
    svg_matrix_multiply(&gm, &t, &gm);
    svg_matrix r;
    svg_matrix_identity(&r);
    svg_matrix_rotate(&r, orient);
    svg_matrix_multiply(&gm, &r, &gm);
    svg_matrix s;
    svg_matrix_identity(&s);
    s.a = sx;
    s.d = sy;
    svg_matrix_multiply(&gm, &s, &gm);
    if (ax != 0.0f || ay != 0.0f) {
        svg_matrix ta;
        svg_matrix_identity(&ta);
        ta.e = ax;
        ta.f = ay;
        svg_matrix_multiply(&gm, &ta, &gm);
    }
    if (mk->vb_ok) {
        svg_matrix tvb;
        svg_matrix_identity(&tvb);
        tvb.e = -mk->vb_x;
        tvb.f = -mk->vb_y;
        svg_matrix_multiply(&gm, &tvb, &gm);
    }
    svg_matrix tref;
    svg_matrix_identity(&tref);
    tref.e = -mk->ref_x;
    tref.f = -mk->ref_y;
    svg_matrix_multiply(&gm, &tref, &gm);
    svg_render_transformed_snippet(ctx, mk->content, &gm, dpi);
}

static void svg_draw_markers_for_segments(svg_render_ctx *ctx, const svg_style *style,
                                          const svg_matrix *m, const svg_segments *segs,
                                          float dpi) {
    if (!ctx || !style || !m || !segs || segs->stroke_count == 0) {
        return;
    }
    if (style->stroke_type == SVG_PAINT_NONE || style->stroke_width <= 0.0f) {
        return;
    }
    if (!style->marker_start && !style->marker_mid && !style->marker_end) {
        return;
    }
    const svg_segment *first = &segs->stroke[0];
    const svg_segment *last_seg = &segs->stroke[segs->stroke_count - 1];
    int is_closed =
        fabsf(first->x0 - last_seg->x1) < 1e-4f && fabsf(first->y0 - last_seg->y1) < 1e-4f;
    if (style->marker_start) {
        float a = is_closed && segs->stroke_count > 1 ? svg_join_angle_deg(last_seg, first)
                                                      : svg_segment_angle_deg(first);
        if (style->marker_start->orient_auto && style->marker_start->orient_auto_start_reverse) {
            a += 180.0f;
        }
        svg_draw_marker_instance(ctx, style->marker_start, m, first->x0, first->y0, a,
                                 style->stroke_width, dpi);
    }
    if (style->marker_mid && segs->stroke_count > 1) {
        for (size_t i = 1; i < segs->stroke_count; i++) {
            float a = svg_join_angle_deg(&segs->stroke[i - 1], &segs->stroke[i]);
            svg_draw_marker_instance(ctx, style->marker_mid, m, segs->stroke[i].x0,
                                     segs->stroke[i].y0, a, style->stroke_width, dpi);
        }
    }
    if (style->marker_end) {
        float a = is_closed && segs->stroke_count > 1 ? svg_join_angle_deg(last_seg, first)
                                                      : svg_segment_angle_deg(last_seg);
        svg_draw_marker_instance(ctx, style->marker_end, m, last_seg->x1, last_seg->y1, a,
                                 style->stroke_width, dpi);
    }
}

static int svg_draw_shape(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                          const svg_tag *tag, const char *local, float dpi, char *child_start,
                          float anim_time, const svg_defs *defs, svg_bbox *out_bbox) {
    if (!ctx || !style || !m || !tag || !local) {
        return 0;
    }
    svg_tag anim_tag = *tag;
    if (!tag->is_self_closing && child_start && anim_time >= 0.0f) {
        cupidimage_svg_apply_animation_children(&anim_tag, local, child_start, anim_time);
        tag = &anim_tag;
    }
    if (out_bbox) {
        cupidimage_svg_bbox_init(out_bbox);
    }
    if (strcmp(local, "rect") == 0) {
        int ok = 0;
        float x =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "x"), ctx->vb_w, dpi, &ok);
        if (!ok)
            x = 0.0f;
        float y =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "y"), ctx->vb_h, dpi, &ok);
        if (!ok)
            y = 0.0f;
        float w =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "width"), ctx->vb_w, dpi, &ok);
        float h = cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "height"), ctx->vb_h,
                                              dpi, &ok);
        float rx =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "rx"), ctx->vb_w, dpi, &ok);
        float ry =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "ry"), ctx->vb_h, dpi, &ok);
        if (!ok)
            rx = 0.0f;
        if (!cupidimage_svg_get_attr(tag, "ry") && rx > 0.0f) {
            ry = rx;
        }
        if (out_bbox && w > 0.0f && h > 0.0f) {
            cupidimage_svg_bbox_add_rect(out_bbox, x, y, w, h);
            if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                cupidimage_svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
            }
        }
        cupidimage_svg_draw_samples_rect(ctx, style, m, x, y, w, h, rx, ry);
        return 1;
    }
    if (strcmp(local, "circle") == 0) {
        int ok = 0;
        float cx =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "cx"), ctx->vb_w, dpi, &ok);
        if (!ok)
            cx = 0.0f;
        float cy =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "cy"), ctx->vb_h, dpi, &ok);
        if (!ok)
            cy = 0.0f;
        float r = cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "r"),
                                              (ctx->vb_w + ctx->vb_h) * 0.5f, dpi, &ok);
        if (out_bbox && r > 0.0f) {
            cupidimage_svg_bbox_add_rect(out_bbox, cx - r, cy - r, r * 2.0f, r * 2.0f);
            if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                cupidimage_svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
            }
        }
        cupidimage_svg_draw_samples_ellipse(ctx, style, m, cx, cy, r, r);
        return 1;
    }
    if (strcmp(local, "ellipse") == 0) {
        int ok = 0;
        float cx =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "cx"), ctx->vb_w, dpi, &ok);
        if (!ok)
            cx = 0.0f;
        float cy =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "cy"), ctx->vb_h, dpi, &ok);
        if (!ok)
            cy = 0.0f;
        float rx =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "rx"), ctx->vb_w, dpi, &ok);
        float ry =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "ry"), ctx->vb_h, dpi, &ok);
        if (out_bbox && rx > 0.0f && ry > 0.0f) {
            cupidimage_svg_bbox_add_rect(out_bbox, cx - rx, cy - ry, rx * 2.0f, ry * 2.0f);
            if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                cupidimage_svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
            }
        }
        cupidimage_svg_draw_samples_ellipse(ctx, style, m, cx, cy, rx, ry);
        return 1;
    }
    if (strcmp(local, "line") == 0) {
        int ok = 0;
        float x1 =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "x1"), ctx->vb_w, dpi, &ok);
        float y1 =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "y1"), ctx->vb_h, dpi, &ok);
        float x2 =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "x2"), ctx->vb_w, dpi, &ok);
        float y2 =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "y2"), ctx->vb_h, dpi, &ok);
        if (out_bbox) {
            float minx = fminf(x1, x2);
            float miny = fminf(y1, y2);
            float maxx = fmaxf(x1, x2);
            float maxy = fmaxf(y1, y2);
            cupidimage_svg_bbox_add_rect(out_bbox, minx, miny, maxx - minx, maxy - miny);
            if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                cupidimage_svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
            }
        }
        cupidimage_svg_draw_samples_line(ctx, style, m, x1, y1, x2, y2);
        if (style->marker_start || style->marker_mid || style->marker_end) {
            svg_segments marker_segs;
            cupidimage_svg_segments_init(&marker_segs);
            if (cupidimage_svg_segments_add(&marker_segs, x1, y1, x2, y2, 1, 0)) {
                svg_draw_markers_for_segments(ctx, style, m, &marker_segs, dpi);
            }
            cupidimage_svg_segments_free(&marker_segs);
        }
        return 1;
    }
    if (strcmp(local, "polyline") == 0 || strcmp(local, "polygon") == 0) {
        const char *points = cupidimage_svg_get_attr(tag, "points");
        float *pts = NULL;
        int count = 0;
        if (cupidimage_svg_parse_points(points, &pts, &count)) {
            svg_segments segs;
            cupidimage_svg_segments_init(&segs);
            float minx = 0.0f, miny = 0.0f, maxx = 0.0f, maxy = 0.0f;
            if (count > 0) {
                minx = maxx = pts[0];
                miny = maxy = pts[1];
                for (int i = 1; i < count; i++) {
                    float px = pts[i * 2];
                    float py = pts[i * 2 + 1];
                    if (px < minx)
                        minx = px;
                    if (px > maxx)
                        maxx = px;
                    if (py < miny)
                        miny = py;
                    if (py > maxy)
                        maxy = py;
                }
            }
            for (int i = 0; i < count - 1; i++) {
                cupidimage_svg_segments_add(&segs, pts[i * 2], pts[i * 2 + 1], pts[(i + 1) * 2],
                                            pts[(i + 1) * 2 + 1], 1, 1);
            }
            if (strcmp(local, "polygon") == 0 && count > 1) {
                cupidimage_svg_segments_add(&segs, pts[(count - 1) * 2], pts[(count - 1) * 2 + 1],
                                            pts[0], pts[1], 1, 1);
            } else if (strcmp(local, "polyline") == 0 && count > 1) {
                cupidimage_svg_segments_add(&segs, pts[(count - 1) * 2], pts[(count - 1) * 2 + 1],
                                            pts[0], pts[1], 0, 1);
            }
            if (out_bbox && count > 0) {
                cupidimage_svg_bbox_add_rect(out_bbox, minx, miny, maxx - minx, maxy - miny);
                if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                    cupidimage_svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
                }
            }
            cupidimage_svg_draw_samples_segments(ctx, style, m, &segs);
            svg_draw_markers_for_segments(ctx, style, m, &segs, dpi);
            cupidimage_svg_segments_free(&segs);
        }
        free(pts);
        return 1;
    }
    if (strcmp(local, "path") == 0) {
        const char *d = cupidimage_svg_get_attr(tag, "d");
        svg_segments segs;
        cupidimage_svg_segments_init(&segs);
        if (cupidimage_svg_parse_path(d, &segs)) {
            if (out_bbox && segs.stroke_count > 0) {
                cupidimage_svg_bbox_add_rect(out_bbox, segs.minx, segs.miny, segs.maxx - segs.minx,
                                             segs.maxy - segs.miny);
                if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                    cupidimage_svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
                }
            }
            cupidimage_svg_draw_samples_segments(ctx, style, m, &segs);
            svg_draw_markers_for_segments(ctx, style, m, &segs, dpi);
        }
        cupidimage_svg_segments_free(&segs);
        return 1;
    }
    if (strcmp(local, "image") == 0) {
        if (style->visibility_hidden) {
            return 1;
        }
        const char *href = cupidimage_svg_get_attr(tag, "href");
        if (!href) {
            href = cupidimage_svg_get_attr(tag, "xlink:href");
        }
        cupidimage_image img;
        if (!cupidimage_svg_load_href_image(href, &img)) {
            return 0;
        }
        int ok = 0;
        float x =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "x"), ctx->vb_w, dpi, &ok);
        if (!ok) {
            x = 0.0f;
        }
        float y =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "y"), ctx->vb_h, dpi, &ok);
        if (!ok) {
            y = 0.0f;
        }
        int w_ok = 0;
        int h_ok = 0;
        float w = cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "width"), ctx->vb_w, dpi,
                                              &w_ok);
        float h = cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "height"), ctx->vb_h,
                                              dpi, &h_ok);
        if (!w_ok) {
            w = (float)img.width;
            w_ok = 1;
        }
        if (!h_ok) {
            h = (float)img.height;
            h_ok = 1;
        }
        int preserve_none = 0;
        float align_x = 0.5f;
        float align_y = 0.5f;
        cupidimage_svg_parse_preserve_aspect_ratio(
            cupidimage_svg_get_attr(tag, "preserveAspectRatio"), &preserve_none, &align_x,
            &align_y);
        if (w_ok && h_ok && w > 0.0f && h > 0.0f) {
            if (out_bbox) {
                cupidimage_svg_bbox_add_rect(out_bbox, x, y, w, h);
            }
            cupidimage_svg_draw_samples_image(ctx, style, m, &img, x, y, w, h, preserve_none,
                                              align_x, align_y);
        }
        cupidimage_free(&img);
        return 1;
    }
    if (strcmp(local, "use") == 0) {
        const char *href = cupidimage_svg_get_attr(tag, "href");
        if (!href) {
            href = cupidimage_svg_get_attr(tag, "xlink:href");
        }
        char idbuf[96];
        if (!cupidimage_svg_parse_href_id(href, idbuf, sizeof(idbuf))) {
            return 0;
        }
        int ok = 0;
        float x =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "x"), ctx->vb_w, dpi, &ok);
        if (!ok) {
            x = 0.0f;
        }
        float y =
            cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "y"), ctx->vb_h, dpi, &ok);
        if (!ok) {
            y = 0.0f;
        }
        svg_matrix use_m = *m;
        if (x != 0.0f || y != 0.0f) {
            svg_matrix t;
            svg_matrix_identity(&t);
            t.e = x;
            t.f = y;
            svg_matrix tm;
            svg_matrix_multiply(&use_m, &t, &tm);
            use_m = tm;
        }
        const svg_symbol *sym = defs ? cupidimage_svg_defs_find_symbol(defs, idbuf) : NULL;
        if (sym && sym->content && *sym->content) {
            int w_ok = 0;
            int h_ok = 0;
            float sw = cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "width"), ctx->vb_w,
                                                   dpi, &w_ok);
            float sh = cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "height"),
                                                   ctx->vb_h, dpi, &h_ok);
            float sx = 1.0f;
            float sy = 1.0f;
            float ax = 0.0f;
            float ay = 0.0f;
            if (sym->vb_ok && sym->vb_w > 0.0f && sym->vb_h > 0.0f) {
                if (w_ok && !h_ok) {
                    sh = sw * (sym->vb_h / sym->vb_w);
                    h_ok = 1;
                } else if (!w_ok && h_ok) {
                    sw = sh * (sym->vb_w / sym->vb_h);
                    w_ok = 1;
                }
                if (w_ok && h_ok && sw > 0.0f && sh > 0.0f) {
                    sx = sw / sym->vb_w;
                    sy = sh / sym->vb_h;
                    if (!sym->preserve_none) {
                        float uni = fminf(sx, sy);
                        ax = (sw - sym->vb_w * uni) * sym->align_x;
                        ay = (sh - sym->vb_h * uni) * sym->align_y;
                        sx = uni;
                        sy = uni;
                    }
                }
                if (sx != 1.0f || sy != 1.0f) {
                    svg_matrix s;
                    svg_matrix_identity(&s);
                    s.a = sx;
                    s.d = sy;
                    svg_matrix_multiply(&use_m, &s, &use_m);
                }
                if (ax != 0.0f || ay != 0.0f) {
                    svg_matrix ta;
                    svg_matrix_identity(&ta);
                    ta.e = ax;
                    ta.f = ay;
                    svg_matrix_multiply(&use_m, &ta, &use_m);
                }
                svg_matrix tvb;
                svg_matrix_identity(&tvb);
                tvb.e = -sym->vb_x;
                tvb.f = -sym->vb_y;
                svg_matrix_multiply(&use_m, &tvb, &use_m);
            }
            char *use_attrs = cupidimage_svg_build_use_wrapper_attrs(tag);
            char *wrapped = cupidimage_svg_wrap_with_group_attrs(sym->content, use_attrs);
            free(use_attrs);
            int rendered = wrapped ? svg_render_transformed_snippet(ctx, wrapped, &use_m, dpi) : 0;
            free(wrapped);
            if (rendered) {
                return 1;
            }
        }
        svg_use_def *ud = defs ? cupidimage_svg_defs_find_use((svg_defs *)defs, idbuf) : NULL;
        if (ud && ud->tag_name && ud->attr_count > 0) {
            svg_tag ref_tag;
            memset(&ref_tag, 0, sizeof(ref_tag));
            ref_tag.name = ud->tag_name;
            ref_tag.attr_count = ud->attr_count;
            for (int i = 0; i < ud->attr_count; i++) {
                ref_tag.attrs[i].name = ud->attrs[i].name;
                ref_tag.attrs[i].value = ud->attrs[i].value;
            }
            const char *ref_local = svg_local_name(ref_tag.name);
            svg_style ref_style = *style;
            float base_len = (ctx->vb_w + ctx->vb_h) * 0.5f;
            cupidimage_svg_apply_presentation_attrs(&ref_style, &ref_tag, base_len, dpi, defs);
            cupidimage_svg_apply_inline_style_attr(&ref_style, &ref_tag, base_len, dpi, defs);
            /* Let explicit properties on <use> override referenced-element styling. */
            cupidimage_svg_apply_presentation_attrs(&ref_style, tag, base_len, dpi, defs);
            cupidimage_svg_apply_inline_style_attr(&ref_style, tag, base_len, dpi, defs);
            if (ref_style.display_none) {
                return 1;
            }
            if (ref_style.visibility_hidden) {
                ref_style.fill_type = SVG_PAINT_NONE;
                ref_style.stroke_type = SVG_PAINT_NONE;
                ref_style.fill_gradient = NULL;
                ref_style.stroke_gradient = NULL;
                ref_style.fill_pattern = NULL;
                ref_style.stroke_pattern = NULL;
            }
            svg_bbox tmp_bbox;
            int drew = svg_draw_shape(ctx, &ref_style, &use_m, &ref_tag, ref_local, dpi, NULL,
                                      anim_time, defs, &tmp_bbox);
            if (drew && out_bbox && cupidimage_svg_bbox_valid(&tmp_bbox)) {
                *out_bbox = tmp_bbox;
            }
            return drew;
        }
        const svg_path_def *pd = defs ? cupidimage_svg_defs_find_path(defs, idbuf) : NULL;
        if (!pd || !pd->d) {
            return 0;
        }
        svg_segments segs;
        cupidimage_svg_segments_init(&segs);
        if (cupidimage_svg_parse_path(pd->d, &segs)) {
            if (out_bbox && segs.stroke_count > 0) {
                cupidimage_svg_bbox_add_rect(out_bbox, segs.minx + x, segs.miny + y,
                                             segs.maxx - segs.minx, segs.maxy - segs.miny);
                if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                    cupidimage_svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
                }
            }
            cupidimage_svg_draw_samples_segments(ctx, style, &use_m, &segs);
            cupidimage_svg_segments_free(&segs);
            return 1;
        }
        cupidimage_svg_segments_free(&segs);
        return 0;
    }
    return 0;
}

int cupidimage_svg_render(svg_render_ctx *ctx, const unsigned char *data, size_t size, char *err,
                          size_t errcap, const cupidimage_svg_options *opts) {
    float dpi = opts ? opts->dpi : 96.0f;
    float anim_time = opts ? opts->animation_time : 0.0f;
    if (dpi <= 0.0f) {
        dpi = 96.0f;
    }
    if (anim_time < 0.0f) {
        anim_time = 0.0f;
    }
    svg_defs defs;
    svg_css css;
    svg_preamble pre;
    if (!cupidimage_svg_parse_preamble(data, size, &pre, &defs, &css, dpi, anim_time, err,
                                       errcap)) {
        cupidimage_svg_defs_free(&defs);
        cupidimage_svg_css_free(&css);
        return 0;
    }
    float intrinsic_w = pre.width_ok ? pre.width_attr : (pre.vb_ok ? pre.vb_w : 512.0f);
    float intrinsic_h = pre.height_ok ? pre.height_attr : (pre.vb_ok ? pre.vb_h : 512.0f);
    if (!isfinite(intrinsic_w) || !isfinite(intrinsic_h) || intrinsic_w <= 0.0f ||
        intrinsic_h <= 0.0f) {
        cupidimage_svg_defs_free(&defs);
        cupidimage_svg_css_free(&css);
        cupidimage_set_err(err, errcap, "invalid SVG dimensions");
        return 0;
    }
    float out_w = intrinsic_w;
    float out_h = intrinsic_h;
    if (opts) {
        if (opts->width > 0 && opts->height > 0) {
            out_w = (float)opts->width;
            out_h = (float)opts->height;
        } else if (opts->width > 0) {
            out_w = (float)opts->width;
            out_h = intrinsic_h * (out_w / intrinsic_w);
        } else if (opts->height > 0) {
            out_h = (float)opts->height;
            out_w = intrinsic_w * (out_h / intrinsic_h);
        }
        if (opts->scale > 0.0f) {
            out_w *= opts->scale;
            out_h *= opts->scale;
        }
    }
    if (out_w < 1.0f)
        out_w = 1.0f;
    if (out_h < 1.0f)
        out_h = 1.0f;
    if (!cupidimage_svg_dimension(out_w, &ctx->width) ||
        !cupidimage_svg_dimension(out_h, &ctx->height)) {
        cupidimage_svg_defs_free(&defs);
        cupidimage_svg_css_free(&css);
        cupidimage_set_err(err, errcap, "invalid SVG dimensions");
        return 0;
    }
    ctx->ss = opts && opts->supersampling ? opts->supersampling : 2;
    if (ctx->ss != 1 && ctx->ss != 2 && ctx->ss != 4) {
        ctx->ss = 2;
    }
    ctx->dpi = dpi;
    ctx->anim_time = anim_time;
    if (!pre.vb_ok) {
        pre.vb_x = 0.0f;
        pre.vb_y = 0.0f;
        pre.vb_w = intrinsic_w;
        pre.vb_h = intrinsic_h;
    }
    ctx->vb_x = pre.vb_x;
    ctx->vb_y = pre.vb_y;
    ctx->vb_w = pre.vb_w;
    ctx->vb_h = pre.vb_h;

    size_t hi_w = (size_t)ctx->width * (size_t)ctx->ss;
    size_t hi_h = (size_t)ctx->height * (size_t)ctx->ss;
    if (hi_w > INT_MAX || hi_h > INT_MAX || hi_w > SVG_MAX_SAMPLES / hi_h) {
        cupidimage_svg_defs_free(&defs);
        cupidimage_svg_css_free(&css);
        cupidimage_set_err(err, errcap, "image too large");
        return 0;
    }
    size_t hi_size = hi_w * hi_h * 4u;
    ctx->hi_rgba = (uint8_t *)malloc(hi_size);
    if (!ctx->hi_rgba) {
        cupidimage_svg_defs_free(&defs);
        cupidimage_svg_css_free(&css);
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }
    uint8_t bg_alpha = opts ? opts->background_alpha : 0;
    uint8_t bg_r = 255;
    uint8_t bg_g = 255;
    uint8_t bg_b = 255;
    uint8_t bg_pr = (uint8_t)((bg_r * bg_alpha) / 255u);
    uint8_t bg_pg = (uint8_t)((bg_g * bg_alpha) / 255u);
    uint8_t bg_pb = (uint8_t)((bg_b * bg_alpha) / 255u);
    for (size_t i = 0; i < hi_w * hi_h; i++) {
        ctx->hi_rgba[i * 4u + 0] = bg_pr;
        ctx->hi_rgba[i * 4u + 1] = bg_pg;
        ctx->hi_rgba[i * 4u + 2] = bg_pb;
        ctx->hi_rgba[i * 4u + 3] = bg_alpha;
    }

    svg_matrix root_view;
    svg_matrix_identity(&root_view);
    float scale_x = ctx->width / pre.vb_w;
    float scale_y = ctx->height / pre.vb_h;
    if (pre.preserve_none) {
        svg_matrix_scale(&root_view, scale_x, scale_y);
        svg_matrix_translate(&root_view, -pre.vb_x * scale_x, -pre.vb_y * scale_y);
    } else {
        float scale = fminf(scale_x, scale_y);
        float extra_x = ctx->width - pre.vb_w * scale;
        float extra_y = ctx->height - pre.vb_h * scale;
        float tx = -pre.vb_x * scale + extra_x * pre.align_x;
        float ty = -pre.vb_y * scale + extra_y * pre.align_y;
        svg_matrix_scale(&root_view, scale, scale);
        svg_matrix_translate(&root_view, tx, ty);
    }

    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        cupidimage_svg_defs_free(&defs);
        cupidimage_svg_css_free(&css);
        free(ctx->hi_rgba);
        ctx->hi_rgba = NULL;
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }
    memcpy(buf, data, size);
    buf[size] = '\0';
    char *p = buf;
    svg_tag tag;
    svg_stack_item stack[SVG_MAX_DEPTH];
    svg_sibling_frame sibling_frames[SVG_MAX_DEPTH];
    memset(sibling_frames, 0, sizeof(sibling_frames));
    int depth = 0;
    int root_pushed = 0;
    int skip_depth = 0;
    float base_len = (ctx->vb_w + ctx->vb_h) * 0.5f;

    while (cupidimage_svg_next_tag(&p, &tag)) {
        const char *local = svg_local_name(tag.name);
        if (tag.is_end) {
            if (skip_depth > 0) {
                skip_depth--;
                continue;
            }
            if (depth > 1) {
                int clear_level = depth;
                if (clear_level >= SVG_MAX_DEPTH) {
                    clear_level = SVG_MAX_DEPTH - 1;
                }
                sibling_frames[clear_level].count = 0;
                depth--;
            }
            continue;
        }
        if (!root_pushed) {
            if (strcmp(local, "svg") != 0) {
                continue;
            }
            svg_tag root_tag = tag;
            if (anim_time >= 0.0f) {
                cupidimage_svg_css_apply_animations_to_tag(&root_tag, &css, anim_time);
            }
            svg_style root_style;
            cupidimage_svg_style_init(&root_style);
            cupidimage_svg_apply_presentation_attrs(&root_style, &root_tag, base_len, dpi, &defs);
            svg_elem_info root_info;
            cupidimage_svg_elem_info_from_tag(&root_info, local, &root_tag);
            svg_elem_info tmp_stack[SVG_MAX_DEPTH];
            tmp_stack[0] = root_info;
            cupidimage_svg_css_apply(&root_style, &css, &defs, tmp_stack, 1, NULL, 0, base_len,
                                     dpi);
            cupidimage_svg_apply_inline_style_attr(&root_style, &root_tag, base_len, dpi, &defs);
            svg_matrix root_attr;
            svg_matrix_identity(&root_attr);
            const char *root_transform_attr = cupidimage_svg_get_attr(&root_tag, "transform");
            if (root_transform_attr) {
                cupidimage_svg_parse_transform(root_transform_attr, &root_attr);
            }
            svg_matrix root_total;
            svg_matrix_multiply(&root_view, &root_attr, &root_total);
            stack[depth].style = root_style;
            stack[depth].transform = root_total;
            stack[depth].info = root_info;
            depth = 1;
            root_pushed = 1;
            if (root_tag.is_self_closing) {
                depth = 0;
                root_pushed = 0;
            }
            continue;
        }
        if (skip_depth > 0) {
            if (!tag.is_self_closing) {
                skip_depth++;
            }
            continue;
        }
        if (strcmp(local, "style") == 0) {
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = cupidimage_svg_find_close_tag(p, "style", &after);
                if (close && after) {
                    p = after;
                }
            }
            continue;
        }
        if (strcmp(local, "defs") == 0 || strcmp(local, "clippath") == 0 ||
            strcmp(local, "mask") == 0 || strcmp(local, "lineargradient") == 0 ||
            strcmp(local, "radialgradient") == 0 || strcmp(local, "pattern") == 0 ||
            strcmp(local, "filter") == 0 || strcmp(local, "symbol") == 0 ||
            strcmp(local, "marker") == 0) {
            if (!tag.is_self_closing) {
                skip_depth = 1;
            }
            continue;
        }

        svg_tag animated_tag = tag;
        const svg_tag *draw_tag = &animated_tag;
        if (!tag.is_self_closing && anim_time >= 0.0f) {
            cupidimage_svg_apply_animation_children(&animated_tag, local, p, anim_time);
        }
        if (anim_time >= 0.0f) {
            cupidimage_svg_css_apply_animations_to_tag(&animated_tag, &css, anim_time);
        }

        svg_style style = stack[depth - 1].style;
        cupidimage_svg_apply_presentation_attrs(&style, draw_tag, base_len, dpi, &defs);
        svg_elem_info info;
        cupidimage_svg_elem_info_from_tag(&info, local, draw_tag);
        svg_elem_info tmp_stack[SVG_MAX_DEPTH];
        int tmp_depth = depth < SVG_MAX_DEPTH ? depth : SVG_MAX_DEPTH;
        for (int i = 0; i < tmp_depth; i++) {
            tmp_stack[i] = stack[i].info;
        }
        if (tmp_depth < SVG_MAX_DEPTH) {
            tmp_stack[tmp_depth++] = info;
        }
        int sib_level = depth;
        if (sib_level >= SVG_MAX_DEPTH) {
            sib_level = SVG_MAX_DEPTH - 1;
        }
        svg_sibling_frame *sib_frame = &sibling_frames[sib_level];
        cupidimage_svg_css_apply(&style, &css, &defs, tmp_stack, tmp_depth, sib_frame->items,
                                 sib_frame->count, base_len, dpi);
        cupidimage_svg_apply_inline_style_attr(&style, draw_tag, base_len, dpi, &defs);
        if (!cupidimage_svg_sibling_frame_push(sib_frame, &info)) {
            continue;
        }

        svg_matrix m = stack[depth - 1].transform;
        const char *transform_attr = cupidimage_svg_get_attr(draw_tag, "transform");
        if (transform_attr) {
            svg_matrix t;
            if (cupidimage_svg_parse_transform(transform_attr, &t)) {
                svg_matrix tmp;
                svg_matrix_multiply(&m, &t, &tmp);
                m = tmp;
            }
        }

        if (style.display_none) {
            if (!tag.is_self_closing) {
                skip_depth = 1;
            }
            continue;
        }

        if (strcmp(local, "g") == 0 || strcmp(local, "svg") == 0) {
            if (!draw_tag->is_self_closing && depth < SVG_MAX_DEPTH) {
                stack[depth].style = style;
                stack[depth].transform = m;
                stack[depth].info = info;
                depth++;
            }
            continue;
        }

        if (style.filter && !style.visibility_hidden) {
            svg_render_ctx tmp_ctx = *ctx;
            size_t hi_w = (size_t)ctx->width * (size_t)ctx->ss;
            size_t hi_h = (size_t)ctx->height * (size_t)ctx->ss;
            size_t hi_size = hi_w * hi_h * 4u;
            tmp_ctx.hi_rgba = (uint8_t *)calloc(1u, hi_size);
            if (!tmp_ctx.hi_rgba) {
                continue;
            }
            int drew = 0;
            svg_bbox bbox;
            cupidimage_svg_bbox_init(&bbox);
            int need_fill = 0;
            int need_stroke = 0;
            int need_bg = 0;
            int need_bg_alpha = 0;
            if (style.filter && style.filter->content) {
                need_fill = svg_strcasestr_simple(style.filter->content, "fillpaint");
                need_stroke = svg_strcasestr_simple(style.filter->content, "strokepaint");
                need_bg = svg_strcasestr_simple(style.filter->content, "backgroundimage");
                need_bg_alpha = svg_strcasestr_simple(style.filter->content, "backgroundalpha");
            }
            uint8_t *fill_buf = NULL;
            uint8_t *stroke_buf = NULL;
            uint8_t *bg_buf = NULL;
            uint8_t *bg_alpha_buf = NULL;
            if (need_bg || need_bg_alpha) {
                bg_buf = (uint8_t *)malloc(hi_size);
                if (bg_buf) {
                    memcpy(bg_buf, ctx->hi_rgba, hi_size);
                }
                if (need_bg_alpha && bg_buf) {
                    bg_alpha_buf = (uint8_t *)malloc(hi_size);
                    if (bg_alpha_buf) {
                        for (size_t bi = 0; bi < hi_w * hi_h; bi++) {
                            uint8_t a = bg_buf[bi * 4u + 3];
                            bg_alpha_buf[bi * 4u + 0] = a;
                            bg_alpha_buf[bi * 4u + 1] = a;
                            bg_alpha_buf[bi * 4u + 2] = a;
                            bg_alpha_buf[bi * 4u + 3] = a;
                        }
                    }
                }
            }
            if (strcmp(local, "text") == 0) {
                svg_elem_info text_stack[SVG_MAX_DEPTH];
                int text_depth = depth < SVG_MAX_DEPTH ? depth : SVG_MAX_DEPTH;
                for (int i = 0; i < text_depth; i++) {
                    text_stack[i] = stack[i].info;
                }
                if (text_depth < SVG_MAX_DEPTH) {
                    text_stack[text_depth++] = info;
                }
                if (!draw_tag->is_self_closing) {
                    if (need_fill) {
                        svg_render_ctx fill_ctx = *ctx;
                        fill_ctx.hi_rgba = (uint8_t *)calloc(1u, hi_size);
                        if (fill_ctx.hi_rgba) {
                            svg_style fill_style = style;
                            fill_style.stroke_type = SVG_PAINT_NONE;
                            fill_style.stroke_gradient = NULL;
                            fill_style.stroke_pattern = NULL;
                            fill_style.stroke_width = 0.0f;
                            char *tp = p;
                            cupidimage_svg_render_text_element(&fill_ctx, &fill_style, &m, draw_tag,
                                                               &tp, &defs, &css, text_stack,
                                                               text_depth, base_len, dpi, NULL);
                            fill_buf = fill_ctx.hi_rgba;
                        }
                    }
                    if (need_stroke) {
                        svg_render_ctx stroke_ctx = *ctx;
                        stroke_ctx.hi_rgba = (uint8_t *)calloc(1u, hi_size);
                        if (stroke_ctx.hi_rgba) {
                            svg_style stroke_style = style;
                            stroke_style.fill_type = SVG_PAINT_NONE;
                            stroke_style.fill_gradient = NULL;
                            stroke_style.fill_pattern = NULL;
                            char *tp = p;
                            cupidimage_svg_render_text_element(
                                &stroke_ctx, &stroke_style, &m, draw_tag, &tp, &defs, &css,
                                text_stack, text_depth, base_len, dpi, NULL);
                            stroke_buf = stroke_ctx.hi_rgba;
                        }
                    }
                    char *tp = p;
                    cupidimage_svg_render_text_element(&tmp_ctx, &style, &m, draw_tag, &tp, &defs,
                                                       &css, text_stack, text_depth, base_len, dpi,
                                                       &bbox);
                    p = tp;
                }
                drew = 1;
            } else {
                drew = svg_draw_shape(&tmp_ctx, &style, &m, draw_tag, local, dpi, NULL, anim_time,
                                      &defs, &bbox);
                if (drew) {
                    if (need_fill) {
                        svg_render_ctx fill_ctx = *ctx;
                        fill_ctx.hi_rgba = (uint8_t *)calloc(1u, hi_size);
                        if (fill_ctx.hi_rgba) {
                            svg_style fill_style = style;
                            fill_style.stroke_type = SVG_PAINT_NONE;
                            fill_style.stroke_gradient = NULL;
                            fill_style.stroke_pattern = NULL;
                            fill_style.stroke_width = 0.0f;
                            svg_draw_shape(&fill_ctx, &fill_style, &m, draw_tag, local, dpi, NULL,
                                           anim_time, &defs, NULL);
                            fill_buf = fill_ctx.hi_rgba;
                        }
                    }
                    if (need_stroke) {
                        svg_render_ctx stroke_ctx = *ctx;
                        stroke_ctx.hi_rgba = (uint8_t *)calloc(1u, hi_size);
                        if (stroke_ctx.hi_rgba) {
                            svg_style stroke_style = style;
                            stroke_style.fill_type = SVG_PAINT_NONE;
                            stroke_style.fill_gradient = NULL;
                            stroke_style.fill_pattern = NULL;
                            svg_draw_shape(&stroke_ctx, &stroke_style, &m, draw_tag, local, dpi,
                                           NULL, anim_time, &defs, NULL);
                            stroke_buf = stroke_ctx.hi_rgba;
                        }
                    }
                }
            }
            if (drew) {
                svg_filter_region region;
                cupidimage_svg_compute_filter_region(style.filter, &tmp_ctx, &bbox, &m, &region);
                cupidimage_svg_apply_filter_ops(style.filter, &tmp_ctx, tmp_ctx.hi_rgba, base_len,
                                                &region, &bbox, &m, bg_buf, bg_alpha_buf, fill_buf,
                                                stroke_buf);
                cupidimage_svg_composite_buffer(ctx, tmp_ctx.hi_rgba);
            }
            free(tmp_ctx.hi_rgba);
            free(fill_buf);
            free(stroke_buf);
            free(bg_buf);
            free(bg_alpha_buf);
            continue;
        }

        if (strcmp(local, "text") == 0) {
            svg_elem_info text_stack[SVG_MAX_DEPTH];
            int text_depth = depth < SVG_MAX_DEPTH ? depth : SVG_MAX_DEPTH;
            for (int i = 0; i < text_depth; i++) {
                text_stack[i] = stack[i].info;
            }
            if (text_depth < SVG_MAX_DEPTH) {
                text_stack[text_depth++] = info;
            }
            if (!draw_tag->is_self_closing) {
                svg_style text_style = style;
                if (text_style.visibility_hidden) {
                    text_style.fill_type = SVG_PAINT_NONE;
                    text_style.stroke_type = SVG_PAINT_NONE;
                    text_style.fill_gradient = NULL;
                    text_style.stroke_gradient = NULL;
                    text_style.fill_pattern = NULL;
                    text_style.stroke_pattern = NULL;
                }
                cupidimage_svg_render_text_element(ctx, &text_style, &m, draw_tag, &p, &defs, &css,
                                                   text_stack, text_depth, base_len, dpi, NULL);
            }
            continue;
        }

        svg_style draw_style = style;
        if (draw_style.visibility_hidden) {
            draw_style.fill_type = SVG_PAINT_NONE;
            draw_style.stroke_type = SVG_PAINT_NONE;
            draw_style.fill_gradient = NULL;
            draw_style.stroke_gradient = NULL;
            draw_style.fill_pattern = NULL;
            draw_style.stroke_pattern = NULL;
        }
        if (svg_draw_shape(ctx, &draw_style, &m, draw_tag, local, dpi, NULL, anim_time, &defs,
                           NULL)) {
            continue;
        }
    }

    free(buf);
    cupidimage_svg_sibling_frames_free(sibling_frames, SVG_MAX_DEPTH);
    cupidimage_svg_defs_free(&defs);
    cupidimage_svg_css_free(&css);
    return 1;
}

int cupidimage_svg_get_dimensions(const unsigned char *data, size_t size, uint32_t *width,
                                  uint32_t *height, char *err, size_t errcap) {
    if (!data || !width || !height) {
        cupidimage_set_err(err, errcap, "invalid arguments");
        return 0;
    }
    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }
    memcpy(buf, data, size);
    buf[size] = '\0';
    char *p = buf;
    svg_tag tag;
    int found = 0;
    float w = 0.0f, h = 0.0f;
    int w_ok = 0, h_ok = 0;
    float vb_x = 0.0f, vb_y = 0.0f, vb_w = 0.0f, vb_h = 0.0f;
    int vb_ok = 0;
    while (cupidimage_svg_next_tag(&p, &tag)) {
        if (!tag.is_end && strcmp(svg_local_name(tag.name), "svg") == 0) {
            found = 1;
            float dpi = 96.0f;
            w = cupidimage_svg_parse_length(cupidimage_svg_get_attr(&tag, "width"), 0.0f, dpi,
                                            &w_ok);
            h = cupidimage_svg_parse_length(cupidimage_svg_get_attr(&tag, "height"), 0.0f, dpi,
                                            &h_ok);
            vb_ok = cupidimage_svg_parse_viewbox(cupidimage_svg_get_attr(&tag, "viewBox"), &vb_x,
                                                 &vb_y, &vb_w, &vb_h);
            break;
        }
    }
    free(buf);
    if (!found) {
        cupidimage_set_err(err, errcap, "not an SVG");
        return 0;
    }
    float intrinsic_w = w_ok ? w : (vb_ok ? vb_w : 512.0f);
    float intrinsic_h = h_ok ? h : (vb_ok ? vb_h : 512.0f);
    if (!cupidimage_svg_dimension(intrinsic_w, width) ||
        !cupidimage_svg_dimension(intrinsic_h, height)) {
        cupidimage_set_err(err, errcap, "invalid SVG dimensions");
        return 0;
    }
    return 1;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
