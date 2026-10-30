#include "core/internal.h"
#include "cupidimage.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdf_internal.h"

#include "pdf_content_syntax.h"
#include "pdf_render_internal.h"

int cupidimage_pdf_draw_form(pdf_render_state *st, pdf_object_id object,
                             const pdf_object_view *form);
int cupidimage_pdf_render_stream(pdf_render_state *st, const unsigned char *stream,
                                 size_t stream_len);

static void pdf_clip_retain(pdf_clip_mask *clip) {
    if (clip)
        clip->references++;
}

static void pdf_clip_release(pdf_render_state *st, pdf_clip_mask *clip) {
    if (clip && --clip->references == 0) {
        st->clip_budget->bytes_used -= clip->bytes;
        free(clip);
    }
}

void cupidimage_pdf_render_state_release(pdf_render_state *st) {
    cupidimage_pdf_text_group_discard(st);
    pdf_clip_release(st, st->gs.clip);
    cupidimage_pdf_soft_mask_release(st, st->gs.soft_mask);
    pdf_clip_release(st, st->text_clip);
    if (st->text_winding) {
        st->clip_budget->bytes_used -= st->text_winding_bytes;
        free(st->text_winding);
    }
    for (int i = 0; i < st->gs_stack_len; i++) {
        pdf_clip_release(st, st->gs_stack[i].clip);
        cupidimage_pdf_soft_mask_release(st, st->gs_stack[i].soft_mask);
    }
}

unsigned char cupidimage_pdf_clamp_u8(double v) {
    if (v < 0.0)
        v = 0.0;
    if (v > 1.0)
        v = 1.0;
    /* Do not round an exact half-byte down after normalized arithmetic loses one ULP. */
    return (unsigned char)nextafter(v * 255.0 + 0.5, INFINITY);
}

static int pdf_pixel_visible(const pdf_render_state *st, int x, int y) {
    const cupidimage_image *img = st->img;
    if (!img || !img->rgba)
        return 0;
    if (x < 0 || y < 0 || x >= (int)img->width || y >= (int)img->height) {
        return 0;
    }
    size_t pixel = (size_t)y * img->width + (size_t)x;
    if (st->gs.clip && !(st->gs.clip->bits[pixel / 8] & (1u << (pixel % 8))))
        return 0;
    double px = st->page_x0_pts + (x + 0.5) * (72.0 / 96.0);
    double py = st->page_top_pts - (y + 0.5) * (72.0 / 96.0);
    for (int i = 0; i < st->form_depth; i++) {
        const pdf_form_clip *clip = &st->form_clips[i];
        const double *m = clip->inverse;
        double u = m[0] * px + m[2] * py + m[4];
        double v = m[1] * px + m[3] * py + m[5];
        if (u < clip->box[0] || u >= clip->box[2] || v < clip->box[1] || v >= clip->box[3])
            return 0;
    }
    return 1;
}

void cupidimage_pdf_composite_pixel(pdf_render_state *st, int x, int y, uint8_t r, uint8_t g,
                                    uint8_t b, double alpha, double shape) {
    if (shape <= 0 || !pdf_pixel_visible(st, x, y))
        return;
    cupidimage_image *img = st->img;
    size_t pixel = (size_t)y * img->width + (size_t)x;
    if (st->gs.soft_mask) {
        if (st->gs.soft_mask->width != img->width || st->gs.soft_mask->height != img->height) {
            st->image_failed = 1;
            return;
        }
        alpha *= st->gs.soft_mask->values[pixel];
        if (st->gs.alpha_is_shape)
            shape *= st->gs.soft_mask->values[pixel];
        if (shape <= 0)
            return;
    }
    if (st->group) {
        double source[3] = {r / 255.0, g / 255.0, b / 255.0};
        cupidimage_pdf_group_paint(st->group, pixel, source, alpha, shape, st->gs.blend_mode);
        return;
    }
    if (st->shape_plane)
        st->shape_plane[pixel] = shape + st->shape_plane[pixel] * (1 - shape);
    if (st->alpha_plane)
        st->alpha_plane[pixel] = alpha + st->alpha_plane[pixel] * (1 - alpha);
    if (alpha <= 0)
        return;
    size_t idx = pixel * 4u;
    if (st->gs.blend_mode == 0 && alpha == 1) {
        img->rgba[idx] = r;
        img->rgba[idx + 1] = g;
        img->rgba[idx + 2] = b;
        img->rgba[idx + 3] = 255;
        return;
    }
    uint8_t source[3] = {r, g, b};
    if (img->rgba[idx + 3] == 255) {
        if (st->gs.blend_mode == 0)
            for (int i = 0; i < 3; i++)
                img->rgba[idx + i] =
                    (uint8_t)floor(alpha * source[i] + (1 - alpha) * img->rgba[idx + i] + .5);
        else {
            double blended[3];
            cupidimage_pdf_blend_rgb(st->gs.blend_mode, source, img->rgba + idx, blended);
            for (int i = 0; i < 3; i++)
                img->rgba[idx + i] = cupidimage_pdf_clamp_u8(
                    alpha * blended[i] + (1 - alpha) * img->rgba[idx + i] / 255.0);
        }
        return;
    }
    double backdrop_alpha = img->rgba[idx + 3] / 255.0;
    double output_alpha = alpha + backdrop_alpha * (1 - alpha), blended[3];
    if (st->gs.blend_mode)
        cupidimage_pdf_blend_rgb(st->gs.blend_mode, source, img->rgba + idx, blended);
    else
        for (int i = 0; i < 3; i++)
            blended[i] = source[i] / 255.0;
    if (output_alpha <= 0)
        return;
    for (int i = 0; i < 3; i++) {
        double value =
            alpha * ((1 - backdrop_alpha) * source[i] / 255.0 + backdrop_alpha * blended[i]) +
            backdrop_alpha * (1 - alpha) * img->rgba[idx + i] / 255.0;
        img->rgba[idx + i] = cupidimage_pdf_clamp_u8(value / output_alpha);
    }
    img->rgba[idx + 3] = cupidimage_pdf_clamp_u8(output_alpha);
}

void cupidimage_pdf_composite_color(pdf_render_state *st, int x, int y, const double source[3],
                                    double alpha, double shape) {
    if (shape <= 0 || !pdf_pixel_visible(st, x, y))
        return;
    size_t pixel = (size_t)y * st->img->width + (size_t)x;
    if (st->gs.soft_mask) {
        if (st->gs.soft_mask->width != st->img->width ||
            st->gs.soft_mask->height != st->img->height) {
            st->image_failed = 1;
            return;
        }
        alpha *= st->gs.soft_mask->values[pixel];
        if (st->gs.alpha_is_shape)
            shape *= st->gs.soft_mask->values[pixel];
        if (shape <= 0)
            return;
    }
    if (st->group) {
        cupidimage_pdf_group_paint(st->group, pixel, source, alpha, shape, st->gs.blend_mode);
        return;
    }
    if (st->shape_plane)
        st->shape_plane[pixel] = shape + st->shape_plane[pixel] * (1 - shape);
    if (st->alpha_plane)
        st->alpha_plane[pixel] = alpha + st->alpha_plane[pixel] * (1 - alpha);
    if (alpha <= 0)
        return;
    unsigned char *p = st->img->rgba + pixel * 4;
    double backdrop[3] = {p[0] / 255.0, p[1] / 255.0, p[2] / 255.0};
    double ab = p[3] / 255.0, ar = alpha + ab * (1 - alpha), blended[3];
    cupidimage_pdf_blend_colors(st->gs.blend_mode, source, backdrop, blended);
    if (ar <= 0)
        return;
    for (unsigned i = 0; i < 3; i++)
        p[i] = cupidimage_pdf_clamp_u8(
            (alpha * ((1 - ab) * source[i] + ab * blended[i]) + ab * (1 - alpha) * backdrop[i]) /
            ar);
    p[3] = cupidimage_pdf_clamp_u8(ar);
}

void cupidimage_pdf_paint_pixel(pdf_render_state *st, int x, int y, int stroke, double coverage) {
    pdf_pattern *pattern = stroke ? st->gs.stroke_pattern : st->gs.fill_pattern;
    const pdf_image_colors *space = stroke ? &st->gs.stroke_space : &st->gs.fill_space;
    double rgb[3] = {0};
    double shape = coverage, intrinsic_alpha = 1;
    if (space->pattern) {
        /* Selecting Pattern starts with an empty pattern until SCN/scn selects one. */
        if (!pattern)
            return;
        double px = st->page_x0_pts + (x + .5) * (72.0 / 96.0),
               py = st->page_top_pts - (y + .5) * (72.0 / 96.0);
        if (!cupidimage_pdf_pattern_sample(st, pattern, px, py, rgb, &shape, &intrinsic_alpha)) {
            st->image_failed = 1;
            return;
        }
    }
    if (space->pattern)
        shape *= coverage;
    if (st->gs.alpha_is_shape)
        shape *= stroke ? st->gs.stroke_alpha : st->gs.fill_alpha;
    double alpha = coverage * (stroke ? st->gs.stroke_alpha : st->gs.fill_alpha) * intrinsic_alpha;
    if (space->pattern && (!pattern->uncolored || pattern->deferred))
        cupidimage_pdf_composite_color(st, x, y, rgb, alpha, shape);
    else
        cupidimage_pdf_composite_space(st, x, y, stroke ? st->gs.stroke_color : st->gs.fill_color,
                                       space, alpha, shape);
}

void cupidimage_pdf_composite_space(pdf_render_state *st, int x, int y, const double values[4],
                                    const pdf_image_colors *space, double alpha, double shape) {
    if (space->tint) {
        cupidimage_pdf_tint_paint(st, x, y, values, space->tint, alpha, shape);
        return;
    }
    if (space->icc) {
        double converted[3];
        if (!cupidimage_pdf_icc_color(space->icc, values, st->group && st->group->gray,
                                      st->gs.rendering_intent, converted)) {
            st->image_failed = 1;
            return;
        }
        cupidimage_pdf_composite_color(st, x, y, converted, alpha, shape);
        return;
    }
    if (!space->calibrated) {
        cupidimage_pdf_composite_device(st, x, y, values, space->components, alpha, shape);
        return;
    }
    double converted[3];
    cupidimage_pdf_calibrated_color(space, values, st->group && st->group->gray, converted);
    cupidimage_pdf_composite_color(st, x, y, converted, alpha, shape);
}

void cupidimage_pdf_composite_device(pdf_render_state *st, int x, int y, const double values[4],
                                     int components, double alpha, double shape) {
    double converted[3];
    cupidimage_pdf_device_color(values, components, st->group && st->group->gray, converted);
    cupidimage_pdf_composite_color(st, x, y, converted, alpha, shape);
}

static void pdf_matrix_identity(float m[6]) {
    m[0] = 1.0f;
    m[1] = 0.0f;
    m[2] = 0.0f;
    m[3] = 1.0f;
    m[4] = 0.0f;
    m[5] = 0.0f;
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

void cupidimage_pdf_user_to_pixel(const pdf_render_state *st, float ux, float uy, float *px,
                                  float *py) {
    float tx = st->gs.ctm[0] * ux + st->gs.ctm[2] * uy + st->gs.ctm[4];
    float ty = st->gs.ctm[1] * ux + st->gs.ctm[3] * uy + st->gs.ctm[5];
    float s = 96.0f / 72.0f;
    *px = (tx - st->page_x0_pts) * s;
    *py = (st->page_top_pts - ty) * s;
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
    if (st->path.overflow) {
        st->image_failed = 1;
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: path precision or segment limit exceeded");
        return 0;
    }
    if (st->path.seg_count <= 0)
        return 1;

    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    pdf_seg xformed[8192];
    int n = 0;
    float start_x = 0, start_y = 0;

    for (int i = 0; i < st->path.seg_count; i++) {
        pdf_seg edge = st->path.segs[i];
        if (!isfinite(edge.x0) || !isfinite(edge.y0) || !isfinite(edge.x1) || !isfinite(edge.y1))
            return 0;
        if (st->path.segs[i].starts_subpath) {
            start_x = edge.x0;
            start_y = edge.y0;
        }
        xformed[n++] = edge;
        min_x = fminf(min_x, fminf(edge.x0, edge.x1));
        max_x = fmaxf(max_x, fmaxf(edge.x0, edge.x1));
        min_y = fminf(min_y, fminf(edge.y0, edge.y1));
        max_y = fmaxf(max_y, fmaxf(edge.y0, edge.y1));
        if (i + 1 == st->path.seg_count || st->path.segs[i + 1].starts_subpath) {
            /* Fill closes each subpath without adding a closing stroke. */
            pdf_seg closing = {.x0 = edge.x1, .y0 = edge.y1, .x1 = start_x, .y1 = start_y};
            xformed[n++] = closing;
        }
    }

    int x0 = (int)fminf((float)st->img->width, fmaxf(0, floorf(min_x)));
    int y0 = (int)fminf((float)st->img->height, fmaxf(0, floorf(min_y)));
    int x1 = (int)fminf((float)st->img->width, fmaxf(0, ceilf(max_x)));
    int y1 = (int)fminf((float)st->img->height, fmaxf(0, ceilf(max_y)));

    if (mask) {
        size_t area = (size_t)(x1 - x0) * (size_t)(y1 - y0);
        if (area > st->clip_budget->tests_left / (size_t)n)
            return 0;
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
                    cupidimage_pdf_paint_pixel(st, x, y, 0, 1);
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
    int empty_fill = cupidimage_pdf_empty_color(&st->gs.fill_space, st->gs.fill_pattern);
    int empty_stroke = cupidimage_pdf_empty_color(&st->gs.stroke_space, st->gs.stroke_pattern);
    if (empty_fill || empty_stroke) {
        if (!empty_fill)
            pdf_fill_path(st, even_odd);
        if (!empty_stroke)
            cupidimage_pdf_stroke_segments(st, &st->path, NULL);
        return;
    }
    if (st->gs.fill_alpha == 1 && st->gs.stroke_alpha == 1 && !st->gs.blend_mode &&
        !st->gs.soft_mask && !st->gs.fill_space.pattern && !st->gs.stroke_space.pattern) {
        pdf_fill_path(st, even_odd);
        cupidimage_pdf_stroke_segments(st, &st->path, NULL);
        return;
    }
    size_t pixels = (size_t)st->img->width * st->img->height, bytes = (pixels + 7) / 8;
    unsigned char *masks = (unsigned char *)calloc(bytes, 2);
    if (!masks) {
        st->image_failed = 1;
        cupidimage_set_err(st->error, st->error_capacity, "pdf: path coverage allocation failed");
        return;
    }
    unsigned char *stroke = masks + bytes;
    if (!pdf_rasterize_path(st, even_odd, masks)) {
        free(masks);
        st->image_failed = 1;
        cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid fill coverage");
        return;
    }
    cupidimage_pdf_stroke_segments(st, &st->path, stroke);
    pdf_paint_group combined = {0};
    if (!st->image_failed && cupidimage_pdf_fill_stroke_begin(st, &combined)) {
        for (size_t pixel = 0; pixel < pixels && !st->image_failed; pixel++) {
            unsigned bit = 1u << (pixel % 8);
            int x = (int)(pixel % st->img->width), y = (int)(pixel / st->img->width);
            int stroked = (stroke[pixel / 8] & bit) != 0;
            if ((masks[pixel / 8] & bit) && (!stroked || combined.surface.image.rgba))
                cupidimage_pdf_paint_pixel(st, x, y, 0, 1);
            if (stroked)
                cupidimage_pdf_paint_pixel(st, x, y, 1, 1);
        }
        if (!cupidimage_pdf_fill_stroke_end(st, &combined, 1))
            st->image_failed = 1;
    }
    free(masks);
}

static int pdf_apply_clip(pdf_render_state *st) {
    size_t bytes = ((size_t)st->img->width * st->img->height + 7) / 8;
    if (st->path.overflow || !st->clip_budget->calls_left ||
        bytes > 64u * 1024u * 1024u - st->clip_budget->bytes_used)
        return 0;
    st->clip_budget->calls_left--;
    pdf_clip_mask *clip = (pdf_clip_mask *)calloc(1, sizeof(*clip) + bytes);
    if (!clip)
        return 0;
    clip->references = 1;
    clip->bytes = bytes;
    if (!pdf_rasterize_path(st, st->pending_clip == 2, clip->bits)) {
        free(clip);
        return 0;
    }
    if (st->gs.clip) {
        for (size_t i = 0; i < bytes; i++)
            clip->bits[i] &= st->gs.clip->bits[i];
    }
    st->clip_budget->bytes_used += bytes;
    pdf_clip_release(st, st->gs.clip);
    st->gs.clip = clip;
    return 1;
}

int cupidimage_pdf_text_clip_prepare(pdf_render_state *st) {
    if (st->text_clip)
        return 1;
    size_t pixels = (size_t)st->img->width * st->img->height, bytes = (pixels + 7) / 8;
    size_t available = 64u * 1024u * 1024u - st->clip_budget->bytes_used;
    if (!st->clip_budget->calls_left || bytes > available ||
        pixels > (available - bytes) / sizeof(int32_t))
        return 0;
    pdf_clip_mask *clip = calloc(1, sizeof(*clip) + bytes);
    if (!clip)
        return 0;
    int32_t *winding = calloc(pixels, sizeof(*winding));
    if (!winding) {
        free(clip);
        return 0;
    }
    clip->references = 1;
    clip->bytes = bytes;
    st->text_winding = winding;
    st->text_winding_bytes = pixels * sizeof(*winding);
    st->clip_budget->bytes_used += bytes + st->text_winding_bytes;
    st->clip_budget->calls_left--;
    st->text_clip = clip;
    return 1;
}

void cupidimage_pdf_text_end(pdf_render_state *st) {
    if (!cupidimage_pdf_text_group_end(st))
        st->image_failed = 1;
    if (st->text_clip) {
        if (st->gs.clip)
            for (size_t i = 0; i < st->text_clip->bytes; i++)
                st->text_clip->bits[i] &= st->gs.clip->bits[i];
        pdf_clip_release(st, st->gs.clip);
        st->gs.clip = st->text_clip;
        st->text_clip = NULL;
        st->clip_budget->bytes_used -= st->text_winding_bytes;
        free(st->text_winding);
        st->text_winding = NULL;
        st->text_winding_bytes = 0;
    }
    st->in_text = 0;
}

static void pdf_path_end(pdf_render_state *st) {
    if (st->pending_clip && !pdf_apply_clip(st)) {
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: clipping path exceeds limits or is invalid");
        st->image_failed = 1;
    }
    st->pending_clip = 0;
    cupidimage_pdf_path_clear(&st->path);
}

static int pdf_render_tj_array(pdf_render_state *st, const char *array, size_t length) {
    const unsigned char *data = (const unsigned char *)array;
    size_t pos = 0;
    while (pos < length) {
        cupidimage_pdf_skip_ws_and_comments(data, length, &pos);
        if (pos == length)
            break;
        if (data[pos] == '(' || data[pos] == '<') {
            char *text = NULL;
            size_t bytes = 0;
            int ok = data[pos] == '('
                         ? cupidimage_pdf_content_literal(data, length, &pos, &text, &bytes)
                         : cupidimage_pdf_content_hex(data, length, &pos, &text, &bytes);
            if (!ok)
                return 0;
            cupidimage_pdf_render_text_string(st, text, bytes);
            free(text);
        } else {
            double adjustment;
            if (!cupidimage_pdf_content_number(data, length, &pos, &adjustment))
                return 0;
            int vertical = st->gs.current_font && st->gs.current_font->cid &&
                           st->gs.current_font->cid->vertical;
            cupidimage_pdf_text_advance(st, -adjustment / 1000 * st->gs.font_size_pts *
                                                (vertical ? 1 : st->gs.horizontal_scale));
        }
        if (st->image_failed)
            return 0;
    }
    return 1;
}

typedef enum {
    PDF_OPERAND_NUM,
    PDF_OPERAND_NAME,
    PDF_OPERAND_STRING,
    PDF_OPERAND_ARRAY,
    PDF_OPERAND_DICTIONARY,
    PDF_OPERAND_BOOLEAN,
    PDF_OPERAND_NULL
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

static int pdf_set_source_color(pdf_render_state *st, int stroke, const pdf_image_colors *selected,
                                const pdf_operand *ops, int count) {
    int space = selected->components;
    if (space < 1 || space > PDF_COLOR_COMPONENTS || count != space)
        return 0;
    for (int i = 0; i < count; i++)
        if (!pdf_expect_num(&ops[i]))
            return 0;
    double *native = stroke ? st->gs.stroke_color : st->gs.fill_color;
    for (int i = 0; i < PDF_COLOR_COMPONENTS; i++)
        native[i] = i < count ? cupidimage_pdf_color_component(selected, i, ops[i].num) : 0;
    if (stroke) {
        st->gs.stroke_space.components = space;
        st->gs.stroke_space.indexed = 0;
        st->gs.stroke_space.pattern = 0;
        st->gs.stroke_pattern = NULL;
    } else {
        st->gs.fill_space.components = space;
        st->gs.fill_space.indexed = 0;
        st->gs.fill_space.pattern = 0;
        st->gs.fill_pattern = NULL;
    }
    return 1;
}

static void pdf_execute_operator(pdf_render_state *st, const char *op, pdf_operand *ops, int *count,
                                 unsigned *compatibility, unsigned *marked) {
    int n = *count;
    st->paint_serial = ++st->form_budget->paint_serial;
    int paints_path = !strcmp(op, "S") || !strcmp(op, "s") || !strcmp(op, "f") ||
                      !strcmp(op, "F") || !strcmp(op, "f*") || !strcmp(op, "B") ||
                      !strcmp(op, "B*") || !strcmp(op, "b") || !strcmp(op, "b*") ||
                      (!strcmp(op, "n") && st->pending_clip);
    if (paints_path && !cupidimage_pdf_path_flatten(st)) {
        st->image_failed = 1;
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: path precision, allocation or segment limit exceeded");
        pdf_clear_operands(ops, count);
        return;
    }
    if (st->glyph_active && !st->glyph_seen_metrics && strcmp(op, "d0") && strcmp(op, "d1")) {
        st->image_failed = 1;
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: Type 3 glyph must begin with d0 or d1");
        pdf_clear_operands(ops, count);
        return;
    }
    int scope_operator = 1;
    if (!strcmp(op, "BX")) {
        if (n || *compatibility == 64)
            st->image_failed = 1;
        else
            (*compatibility)++;
    } else if (!strcmp(op, "EX")) {
        if (n || !*compatibility)
            st->image_failed = 1;
        else
            (*compatibility)--;
    } else if (!strcmp(op, "MP") || !strcmp(op, "DP") || !strcmp(op, "BMC") || !strcmp(op, "BDC")) {
        int properties = !strcmp(op, "DP") || !strcmp(op, "BDC");
        int begin = !strcmp(op, "BMC") || !strcmp(op, "BDC");
        int valid = n == (properties ? 2 : 1) && ops[0].type == PDF_OPERAND_NAME;
        if (valid && properties)
            valid = ops[1].type == PDF_OPERAND_NAME || ops[1].type == PDF_OPERAND_DICTIONARY;
        if (!valid || (begin && *marked == 64))
            st->image_failed = 1;
        else if (begin)
            (*marked)++;
    } else if (!strcmp(op, "EMC")) {
        if (n || !*marked)
            st->image_failed = 1;
        else
            (*marked)--;
    } else if (!strcmp(op, "ri")) {
        if (n != 1 || ops[0].type != PDF_OPERAND_NAME)
            st->image_failed = 1;
        else
            st->gs.rendering_intent = cupidimage_pdf_intent_name(ops[0].str);
    } else if (!strcmp(op, "d0") || !strcmp(op, "d1")) {
        int box = op[1] == '1',
            valid = st->glyph_active && !st->glyph_seen_metrics && n == (box ? 6 : 2);
        for (int i = 0; valid && i < n; i++)
            valid = pdf_expect_num(&ops[i]) && isfinite(ops[i].num) && fabs(ops[i].num) <= 1e30;
        valid = valid && ops[1].num == 0 && fabs(ops[0].num - st->glyph_expected_width) <= 1e-6;
        if (box && valid)
            valid = ops[2].num <= ops[4].num && ops[3].num <= ops[5].num;
        if (valid) {
            st->glyph_seen_metrics = 1;
            st->glyph_uncolored |= box;
            if (st->glyph_uncolored) {
                /* d1 contributes geometry only; caller paint is applied once to its shape. */
                st->gs.fill_alpha = st->gs.stroke_alpha = 1;
                st->gs.blend_mode = st->gs.alpha_is_shape = 0;
                cupidimage_pdf_soft_mask_release(st, st->gs.soft_mask);
                st->gs.soft_mask = NULL;
                memset(&st->gs.fill_space, 0, sizeof(st->gs.fill_space));
                st->gs.fill_space.components = 1;
                st->gs.stroke_space = st->gs.fill_space;
                st->gs.fill_pattern = st->gs.stroke_pattern = NULL;
                memset(st->gs.fill_color, 0, sizeof(st->gs.fill_color));
                memset(st->gs.stroke_color, 0, sizeof(st->gs.stroke_color));
            }
        } else {
            st->image_failed = 1;
            cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid Type 3 glyph metrics");
        }
        pdf_clear_operands(ops, count);
        return;
    } else {
        scope_operator = 0;
    }
    if (scope_operator) {
        if (st->image_failed)
            cupidimage_set_err(st->error, st->error_capacity,
                               "pdf: invalid content scope operator");
        pdf_clear_operands(ops, count);
        return;
    }
    if ((st->glyph_uncolored || st->pattern_uncolored) &&
        (!strcmp(op, "g") || !strcmp(op, "G") || !strcmp(op, "rg") || !strcmp(op, "RG") ||
         !strcmp(op, "k") || !strcmp(op, "K") || !strcmp(op, "cs") || !strcmp(op, "CS") ||
         !strcmp(op, "sc") || !strcmp(op, "SC") || !strcmp(op, "scn") || !strcmp(op, "SCN"))) {
        if (st->pattern_uncolored) {
            st->image_failed = 1;
            cupidimage_set_err(st->error, st->error_capacity,
                               "pdf: uncolored pattern sets a color");
        }
        pdf_clear_operands(ops, count);
        return;
    }

    if (strcmp(op, "sh") == 0) {
        if (st->glyph_uncolored || st->pattern_uncolored || n != 1 ||
            ops[0].type != PDF_OPERAND_NAME || !cupidimage_pdf_draw_shading(st, ops[0].str)) {
            st->image_failed = 1;
            cupidimage_set_err(st->error, st->error_capacity,
                               "pdf: invalid or unsupported shading");
        }
    } else if (strcmp(op, "gs") == 0) {
        if (n != 1 || ops[0].type != PDF_OPERAND_NAME ||
            !cupidimage_pdf_apply_extgstate(st, ops[0].str)) {
            st->image_failed = 1;
            cupidimage_set_err(st->error, st->error_capacity,
                               "pdf: invalid or unsupported graphics state");
        }
    } else if (strcmp(op, "Do") == 0) {
        if (n != 1 || ops[0].type != PDF_OPERAND_NAME ||
            !cupidimage_pdf_draw_image(st, ops[0].str)) {
            st->image_failed = 1;
            if (n != 1 || ops[0].type != PDF_OPERAND_NAME)
                cupidimage_set_err(st->error, st->error_capacity,
                                   "pdf: invalid image draw operands");
        }
    } else if (strcmp(op, "BT") == 0) {
        if (n == 0)
            cupidimage_pdf_text_begin(st);
        else
            st->image_failed = 1;
    } else if (strcmp(op, "ET") == 0) {
        if (n == 0 && st->in_text)
            cupidimage_pdf_text_end(st);
        else
            st->image_failed = 1;
    } else if (strcmp(op, "Tf") == 0) {
        if (n == 2 && ops[0].type == PDF_OPERAND_NAME && pdf_expect_num(&ops[1]) &&
            isfinite(ops[1].num) && fabs(ops[1].num) <= 1e30) {
            st->gs.current_font =
                st->font_table ? cupidimage_pdf_font_table_find(st->font_table, ops[0].str) : NULL;
            st->gs.font_size_pts = ops[1].num;
        } else
            st->image_failed = 1;
    } else if (strcmp(op, "Td") == 0 || strcmp(op, "TD") == 0) {
        if (st->in_text && n == 2 && pdf_expect_num(&ops[0]) && pdf_expect_num(&ops[1])) {
            if (op[1] == 'D')
                st->gs.leading_pts = -ops[1].num;
            cupidimage_pdf_text_move(st, ops[0].num, ops[1].num);
        } else
            st->image_failed = 1;
    } else if (strcmp(op, "Tm") == 0) {
        int valid = st->in_text && n == 6;
        for (int i = 0; valid && i < 6; i++)
            valid = pdf_expect_num(&ops[i]) && isfinite(ops[i].num) && fabs(ops[i].num) <= 1e30;
        if (!valid)
            st->image_failed = 1;
        else
            for (int i = 0; i < 6; i++)
                st->text_matrix[i] = st->text_line_matrix[i] = ops[i].num;
    } else if (strcmp(op, "T*") == 0) {
        if (st->in_text && n == 0)
            cupidimage_pdf_text_move(st, 0, -st->gs.leading_pts);
        else
            st->image_failed = 1;
    } else if (strcmp(op, "TL") == 0 || strcmp(op, "Tc") == 0 || strcmp(op, "Tw") == 0 ||
               strcmp(op, "Tz") == 0 || strcmp(op, "Ts") == 0) {
        if (n != 1 || !pdf_expect_num(&ops[0]) || !isfinite(ops[0].num) || fabs(ops[0].num) > 1e30)
            st->image_failed = 1;
        else if (op[1] == 'L')
            st->gs.leading_pts = ops[0].num;
        else if (op[1] == 'c')
            st->gs.char_space = ops[0].num;
        else if (op[1] == 'w')
            st->gs.word_space = ops[0].num;
        else if (op[1] == 'z')
            st->gs.horizontal_scale = ops[0].num / 100.0;
        else
            st->gs.text_rise = ops[0].num;
    } else if (strcmp(op, "Tr") == 0) {
        if (n != 1 || !pdf_expect_num(&ops[0]) ||
            (!isfinite(ops[0].num) || ops[0].num < 0 || ops[0].num > 7 ||
             floor(ops[0].num) != ops[0].num))
            st->image_failed = 1;
        else
            st->gs.text_mode = (int)ops[0].num;
    } else if (strcmp(op, "cs") == 0 || strcmp(op, "CS") == 0) {
        pdf_image_colors colors = {0};
        int valid = n == 1 && ops[0].type == PDF_OPERAND_NAME &&
                    cupidimage_pdf_color_space(st, ops[0].str, 0, &colors);
        if (!valid) {
            cupidimage_set_err(st->error, st->error_capacity,
                               "pdf: invalid or unsupported color space");
            st->image_failed = 1;
        } else {
            int stroke = op[0] == 'C';
            pdf_operand initial[PDF_COLOR_COMPONENTS] = {{0}};
            for (int i = 0; i < colors.components; i++) {
                initial[i].type = PDF_OPERAND_NUM;
                initial[i].num =
                    colors.indexed
                        ? cupidimage_pdf_color_palette_value(&colors, i, colors.palette[i])
                    : colors.tint || (colors.components == 4 && i == 3) ? 1
                                                                        : 0;
            }
            pdf_set_source_color(st, stroke, &colors, initial, colors.components);
            if (stroke) {
                st->gs.stroke_space = colors;
                st->gs.stroke_pattern = NULL;
            } else {
                st->gs.fill_space = colors;
                st->gs.fill_pattern = NULL;
            }
        }
    } else if (strcmp(op, "g") == 0 || strcmp(op, "G") == 0 || strcmp(op, "rg") == 0 ||
               strcmp(op, "RG") == 0 || strcmp(op, "k") == 0 || strcmp(op, "K") == 0 ||
               strcmp(op, "sc") == 0 || strcmp(op, "SC") == 0 || strcmp(op, "scn") == 0 ||
               strcmp(op, "SCN") == 0) {
        int stroke = op[0] >= 'A' && op[0] <= 'Z';
        pdf_image_colors colors = stroke ? st->gs.stroke_space : st->gs.fill_space;
        int direct = op[0] != 's' && op[0] != 'S';
        if (direct) {
            memset(&colors, 0, sizeof(colors));
            const char *name = op[0] == 'g' || op[0] == 'G'   ? "DeviceGray"
                               : op[0] == 'r' || op[0] == 'R' ? "DeviceRGB"
                                                              : "DeviceCMYK";
            if (!cupidimage_pdf_color_space(st, name, 0, &colors)) {
                st->image_failed = 1;
                cupidimage_set_err(st->error, st->error_capacity,
                                   "pdf: invalid or unsupported default color space");
                pdf_clear_operands(ops, count);
                return;
            }
        }
        if (!direct && colors.pattern) {
            int operands = colors.indexed ? 1 : colors.components;
            int valid = op[2] == 'n' || op[2] == 'N';
            valid = valid && n == operands + 1 && ops[n - 1].type == PDF_OPERAND_NAME;
            pdf_operand base[PDF_COLOR_COMPONENTS] = {{0}};
            const pdf_operand *values = ops;
            if (valid && colors.indexed) {
                valid = pdf_expect_num(ops) && isfinite(ops[0].num);
                if (valid) {
                    unsigned index = (unsigned)floor(fmin(colors.high, fmax(0, ops[0].num)) + .5);
                    for (int i = 0; i < colors.components; i++) {
                        base[i].type = PDF_OPERAND_NUM;
                        base[i].num = cupidimage_pdf_color_palette_value(
                            &colors, i, colors.palette[index * colors.components + i]);
                    }
                    values = base;
                }
            }
            if (valid && colors.components)
                valid = pdf_set_source_color(st, stroke, &colors, values, colors.components);
            if (stroke)
                st->gs.stroke_space = colors;
            else
                st->gs.fill_space = colors;
            if (valid)
                valid = cupidimage_pdf_select_pattern(st, ops[n - 1].str, stroke);
            if (!valid) {
                st->image_failed = 1;
                cupidimage_set_err(st->error, st->error_capacity,
                                   "pdf: invalid or unsupported pattern");
            }
            pdf_clear_operands(ops, count);
            return;
        }
        pdf_operand converted[PDF_COLOR_COMPONENTS] = {{0}};
        const pdf_operand *values = ops;
        int count = n, valid = 1;
        if (colors.indexed) {
            valid = n == 1 && pdf_expect_num(ops);
            if (valid) {
                unsigned index = (unsigned)floor(fmin(colors.high, fmax(0, ops[0].num)) + .5);
                for (int i = 0; i < colors.components; i++) {
                    converted[i].type = PDF_OPERAND_NUM;
                    converted[i].num = cupidimage_pdf_color_palette_value(
                        &colors, i, colors.palette[index * colors.components + i]);
                }
                values = converted;
                count = colors.components;
            }
        }
        if (!valid || !pdf_set_source_color(st, stroke, &colors, values, count)) {
            cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid color operands");
            st->image_failed = 1;
        } else if (stroke)
            st->gs.stroke_space = colors;
        else
            st->gs.fill_space = colors;
    } else if (!strcmp(op, "i")) {
        if (n != 1 || !pdf_expect_num(ops) || !cupidimage_pdf_set_flatness(&st->gs, ops[0].num)) {
            st->image_failed = 1;
            cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid flatness tolerance");
        }
    } else if (!strcmp(op, "w") || !strcmp(op, "J") || !strcmp(op, "j") || !strcmp(op, "M")) {
        int valid = n == 1 && pdf_expect_num(ops) && isfinite(ops[0].num);
        double value = valid ? ops[0].num : -1;
        if (!strcmp(op, "w")) {
            valid = valid && value >= 0 && value <= 1e6;
            if (valid)
                st->gs.line_width_pts = (float)value;
        } else if (!strcmp(op, "M")) {
            valid = valid && value >= 1 && value <= 1e6;
            if (valid)
                st->gs.miter_limit = value;
        } else {
            valid = valid && value >= 0 && value <= 2 && floor(value) == value;
            if (valid) {
                if (!strcmp(op, "J"))
                    st->gs.line_cap = (int)value;
                else
                    st->gs.line_join = (int)value;
            }
        }
        if (!valid)
            st->image_failed = 1;
    } else if (!strcmp(op, "d")) {
        if (n != 2 || ops[0].type != PDF_OPERAND_ARRAY || !pdf_expect_num(ops + 1) ||
            !cupidimage_pdf_set_dash(&st->gs, (const unsigned char *)ops[0].str, ops[0].len,
                                     ops[1].num))
            st->image_failed = 1;
    } else if (strcmp(op, "cm") == 0) {
        if (n >= 6 && pdf_expect_num(&ops[n - 6]) && pdf_expect_num(&ops[n - 5]) &&
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
            cupidimage_pdf_soft_mask_retain(st->gs.soft_mask);
            st->gs_stack[st->gs_stack_len++] = st->gs;
        } else {
            cupidimage_set_err(st->error, st->error_capacity,
                               "pdf: graphics state stack limit exceeded");
            st->image_failed = 1;
        }
    } else if (strcmp(op, "Q") == 0) {
        if (st->gs_stack_len > 0) {
            pdf_clip_release(st, st->gs.clip);
            cupidimage_pdf_soft_mask_release(st, st->gs.soft_mask);
            st->gs = st->gs_stack[--st->gs_stack_len];
        }
    } else if (strcmp(op, "m") == 0) {
        if (n >= 2 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            cupidimage_pdf_path_move_user(st, (float)ops[n - 2].num, (float)ops[n - 1].num);
        }
    } else if (strcmp(op, "l") == 0) {
        if (n >= 2 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            cupidimage_pdf_path_line_user(st, (float)ops[n - 2].num, (float)ops[n - 1].num);
        }
    } else if (strcmp(op, "c") == 0) {
        if (n >= 6 && pdf_expect_num(&ops[n - 6]) && pdf_expect_num(&ops[n - 5]) &&
            pdf_expect_num(&ops[n - 4]) && pdf_expect_num(&ops[n - 3]) &&
            pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            cupidimage_pdf_path_curve_to(st, (float)ops[n - 6].num, (float)ops[n - 5].num,
                                         (float)ops[n - 4].num, (float)ops[n - 3].num,
                                         (float)ops[n - 2].num, (float)ops[n - 1].num, 0);
        }
    } else if (strcmp(op, "v") == 0 || strcmp(op, "y") == 0) {
        if (n >= 4 && pdf_expect_num(&ops[n - 4]) && pdf_expect_num(&ops[n - 3]) &&
            pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            float x = (float)ops[n - 2].num, y = (float)ops[n - 1].num;
            float cx = (float)ops[n - 4].num, cy = (float)ops[n - 3].num;
            if (op[0] == 'v') {
                cupidimage_pdf_path_curve_to(st, 0, 0, cx, cy, x, y, 1);
            } else {
                cupidimage_pdf_path_curve_to(st, cx, cy, x, y, x, y, 0);
            }
        }
    } else if (strcmp(op, "h") == 0) {
        cupidimage_pdf_path_close(&st->path);
    } else if (strcmp(op, "re") == 0) {
        if (n >= 4 && pdf_expect_num(&ops[n - 4]) && pdf_expect_num(&ops[n - 3]) &&
            pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            float x = (float)ops[n - 4].num;
            float y = (float)ops[n - 3].num;
            float w = (float)ops[n - 2].num;
            float h = (float)ops[n - 1].num;
            cupidimage_pdf_path_move_user(st, x, y);
            cupidimage_pdf_path_line_user(st, x + w, y);
            cupidimage_pdf_path_line_user(st, x + w, y + h);
            cupidimage_pdf_path_line_user(st, x, y + h);
            cupidimage_pdf_path_close(&st->path);
        }
    } else if (strcmp(op, "n") == 0) {
        pdf_path_end(st);
    } else if (strcmp(op, "S") == 0) {
        cupidimage_pdf_stroke_segments(st, &st->path, NULL);
        pdf_path_end(st);
    } else if (strcmp(op, "s") == 0) {
        cupidimage_pdf_path_close(&st->path);
        cupidimage_pdf_stroke_segments(st, &st->path, NULL);
        pdf_path_end(st);
    } else if (strcmp(op, "f") == 0 || strcmp(op, "F") == 0 || strcmp(op, "f*") == 0) {
        pdf_fill_path(st, strcmp(op, "f*") == 0);
        pdf_path_end(st);
    } else if (strcmp(op, "B") == 0 || strcmp(op, "B*") == 0) {
        pdf_fill_and_stroke(st, strcmp(op, "B*") == 0);
        pdf_path_end(st);
    } else if (strcmp(op, "b") == 0 || strcmp(op, "b*") == 0) {
        cupidimage_pdf_path_close(&st->path);
        pdf_fill_and_stroke(st, strcmp(op, "b*") == 0);
        pdf_path_end(st);
    } else if (strcmp(op, "W") == 0 || strcmp(op, "W*") == 0) {
        st->pending_clip = op[1] == '*' ? 2 : 1;
    } else if (strcmp(op, "Tj") == 0) {
        if (st->in_text && n == 1 && ops[0].type == PDF_OPERAND_STRING)
            cupidimage_pdf_render_text_string(st, ops[0].str, ops[0].len);
        else
            st->image_failed = 1;
    } else if (strcmp(op, "TJ") == 0) {
        if (!st->in_text || n != 1 || ops[0].type != PDF_OPERAND_ARRAY ||
            !pdf_render_tj_array(st, ops[0].str, ops[0].len))
            st->image_failed = 1;
    } else if (strcmp(op, "'") == 0) {
        if (st->in_text && n == 1 && ops[0].type == PDF_OPERAND_STRING) {
            cupidimage_pdf_text_move(st, 0, -st->gs.leading_pts);
            cupidimage_pdf_render_text_string(st, ops[0].str, ops[0].len);
        } else
            st->image_failed = 1;
    } else if (strcmp(op, "\"") == 0) {
        if (st->in_text && n == 3 && pdf_expect_num(&ops[0]) && pdf_expect_num(&ops[1]) &&
            isfinite(ops[0].num) && isfinite(ops[1].num) && fabs(ops[0].num) <= 1e30 &&
            fabs(ops[1].num) <= 1e30 && ops[2].type == PDF_OPERAND_STRING) {
            st->gs.word_space = ops[0].num;
            st->gs.char_space = ops[1].num;
            cupidimage_pdf_text_move(st, 0, -st->gs.leading_pts);
            cupidimage_pdf_render_text_string(st, ops[2].str, ops[2].len);
        } else
            st->image_failed = 1;
    } else if (!*compatibility) {
        st->image_failed = 1;
        cupidimage_set_err(st->error, st->error_capacity, "pdf: unknown content operator");
    }

    cupidimage_pdf_patterns_end_paint(st);
    if (st->image_failed && st->error && st->error_capacity && !st->error[0])
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: invalid or unsupported content operator");
    pdf_clear_operands(ops, count);
}

int cupidimage_pdf_render_stream(pdf_render_state *st, const unsigned char *stream,
                                 size_t stream_len) {
    char *err = st->error;
    size_t errcap = st->error_capacity;
    pdf_operand ops[64];
    int op_count = 0;
    unsigned compatibility = 0, marked = 0;
    memset(ops, 0, sizeof(ops));

    size_t pos = 0;
    while (pos < stream_len) {
        cupidimage_pdf_skip_ws_and_comments(stream, stream_len, &pos);
        if (pos >= stream_len) {
            break;
        }

        unsigned char c = stream[pos];
        if (op_count == 64 && (c == '(' || c == '<' || c == '[' || c == '/' || c == '+' ||
                               c == '-' || c == '.' || (c >= '0' && c <= '9'))) {
            cupidimage_set_err(err, errcap, "pdf: content operand limit exceeded");
            pdf_clear_operands(ops, &op_count);
            return 0;
        }

        if (c == '(') {
            char *txt = NULL;
            size_t txt_len = 0;
            if (!cupidimage_pdf_content_literal(stream, stream_len, &pos, &txt, &txt_len)) {
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
            if (!cupidimage_pdf_content_dictionary(stream, stream_len, &pos, &dictionary,
                                                   &length)) {
                cupidimage_set_err(err, errcap, "pdf: malformed content dictionary");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_DICTIONARY;
                ops[op_count].str = dictionary;
                ops[op_count].len = length;
                op_count++;
            } else
                free(dictionary);
            continue;
        }

        if (c == '<' && !(pos + 1 < stream_len && stream[pos + 1] == '<')) {
            char *txt = NULL;
            size_t txt_len = 0;
            if (!cupidimage_pdf_content_hex(stream, stream_len, &pos, &txt, &txt_len)) {
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
            if (!cupidimage_pdf_content_array(stream, stream_len, &pos, &arr, &arr_len)) {
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
            if (!cupidimage_pdf_content_name(stream, stream_len, &pos, &name, &name_len)) {
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
            if (!cupidimage_pdf_content_number(stream, stream_len, &pos, &num)) {
                cupidimage_set_err(err, errcap, "pdf: malformed or oversized number");
                pdf_clear_operands(ops, &op_count);
                return 0;
            } else {
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
        while (pos < stream_len && !cupidimage_pdf_is_ws(stream[pos]) &&
               !cupidimage_pdf_is_delim(stream[pos])) {
            pos++;
        }
        if (pos > start) {
            size_t tok_len = pos - start;
            char opbuf[16];
            if (tok_len >= sizeof(opbuf)) {
                pdf_clear_operands(ops, &op_count);
                if (compatibility)
                    continue;
                cupidimage_set_err(err, errcap, "pdf: unknown content operator");
                return 0;
            }
            memcpy(opbuf, &stream[start], tok_len);
            opbuf[tok_len] = '\0';
            if (!strcmp(opbuf, "true") || !strcmp(opbuf, "false") || !strcmp(opbuf, "null")) {
                if (op_count == 64) {
                    pdf_clear_operands(ops, &op_count);
                    cupidimage_set_err(err, errcap, "pdf: content operand limit exceeded");
                    return 0;
                }
                ops[op_count].type =
                    !strcmp(opbuf, "null") ? PDF_OPERAND_NULL : PDF_OPERAND_BOOLEAN;
                ops[op_count].num = !strcmp(opbuf, "true");
                op_count++;
                continue;
            }
            if (!strcmp(opbuf, "BI")) {
                int ok = op_count == 0 && (!st->glyph_active || st->glyph_seen_metrics) &&
                         cupidimage_pdf_draw_inline_image(st, stream, stream_len, &pos);
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
            pdf_execute_operator(st, opbuf, ops, &op_count, &compatibility, &marked);
            if (st->image_failed)
                return 0;
            continue;
        }

        pdf_clear_operands(ops, &op_count);
        cupidimage_set_err(err, errcap, "pdf: unexpected content delimiter");
        return 0;
    }

    if (op_count || compatibility || marked) {
        pdf_clear_operands(ops, &op_count);
        cupidimage_set_err(err, errcap, "pdf: incomplete content operands or scope");
        return 0;
    }
    pdf_clear_operands(ops, &op_count);
    if (st->in_text) {
        cupidimage_set_err(err, errcap, "pdf: unterminated text object");
        return 0;
    }
    return 1;
}

void cupidimage_pdf_matrix_concat_double(double m[6], const double n[6]) {
    double r[6] = {m[0] * n[0] + m[2] * n[1],        m[1] * n[0] + m[3] * n[1],
                   m[0] * n[2] + m[2] * n[3],        m[1] * n[2] + m[3] * n[3],
                   m[0] * n[4] + m[2] * n[5] + m[4], m[1] * n[4] + m[3] * n[5] + m[5]};
    memcpy(m, r, sizeof(r));
}

int cupidimage_pdf_draw_type3_glyph(pdf_render_state *st, const pdf_font_map *font, unsigned code) {
    const pdf_type3_font *type = font->type3;
    pdf_object_id object = type->glyphs[code];
    int ok = 0;
    if (!object || st->gs.text_mode == 3 || st->gs.font_size_pts == 0 ||
        st->gs.horizontal_scale == 0)
        return 1;
    if (st->glyph_depth == 16 || !st->form_budget->glyph_calls_left)
        return 0;
    st->form_budget->glyph_calls_left--;
    pdf_object_view program = {0};
    unsigned char *decoded = NULL;
    pdf_render_state *child = NULL;
    pdf_transparency_group glyph_group = {0};
    pdf_font_table fonts;
    cupidimage_pdf_font_table_init(&fonts);
    const unsigned char *stream;
    size_t length;
    if (!cupidimage_pdf_state_object(st, object, &program) ||
        !cupidimage_pdf_object_stream(st->document, st->document_size, &program, &stream,
                                      &length) ||
        !cupidimage_pdf_decode_stream(&program, stream, length, &decoded, &length) ||
        length > st->form_budget->bytes_left)
        goto done;
    st->form_budget->bytes_left -= length;
    child = malloc(sizeof(*child));
    if (!child)
        goto done;
    *child = *st;
    child->text_group = NULL;
    pdf_clip_retain(child->gs.clip);
    cupidimage_pdf_soft_mask_retain(child->gs.soft_mask);
    child->gs_stack_len = 0;
    child->pending_clip = 0;
    child->image_failed = 0;
    child->text_clip = NULL;
    child->text_winding = NULL;
    child->text_winding_bytes = 0;
    child->in_text = 0;
    child->glyph_active = 1;
    child->glyph_seen_metrics = 0;
    child->glyph_uncolored = st->glyph_uncolored;
    child->glyph_expected_width = type->widths[code];
    child->glyph_depth++;
    cupidimage_pdf_path_clear(&child->path);
    double m[6];
    for (int i = 0; i < 6; i++)
        m[i] = st->gs.ctm[i];
    double scale[6] = {st->gs.font_size_pts * st->gs.horizontal_scale,
                       0,
                       0,
                       st->gs.font_size_pts,
                       0,
                       st->gs.text_rise};
    cupidimage_pdf_matrix_concat_double(m, st->text_matrix);
    cupidimage_pdf_matrix_concat_double(m, scale);
    cupidimage_pdf_matrix_concat_double(m, type->matrix);
    for (int i = 0; i < 6; i++) {
        if (!isfinite(m[i]) || fabs(m[i]) > 1e30)
            goto done;
        child->gs.ctm[i] = (float)m[i];
    }
    pdf_object_view glyph_resources = {0};
    int local_resources = cupidimage_pdf_optional_dictionary(
        st->document, st->document_size, &type->dictionary, "Resources", &glyph_resources);
    cupidimage_pdf_object_view_release(&glyph_resources);
    if (local_resources < 0)
        goto done;
    if (local_resources > 0) {
        child->resource_override = &type->dictionary;
        if (!cupidimage_pdf_collect_resource_fonts(st->document, st->document_size,
                                                   &type->dictionary, &fonts, st->error,
                                                   st->error_capacity))
            goto done;
        child->font_table = &fonts;
    }
    child->initial_gs = child->gs;
    child->initial_gs.soft_mask = NULL;
    child->initial_gs.clip = NULL;
    if (!cupidimage_pdf_group_surface(st, &glyph_group, 0, NULL))
        goto done;
    child->group = &glyph_group;
    child->img = &glyph_group.image;
    ok = cupidimage_pdf_render_stream(child, decoded, length) && child->glyph_seen_metrics;
    if (ok)
        ok = child->glyph_uncolored ? cupidimage_pdf_type3_stencil_finish(st, &glyph_group)
                                    : cupidimage_pdf_composite_implicit_group(st, &glyph_group);
done:
    if (child)
        cupidimage_pdf_render_state_release(child);
    cupidimage_pdf_group_free(st, &glyph_group);
    free(child);
    free(decoded);
    cupidimage_pdf_object_view_release(&program);
    cupidimage_pdf_font_table_free(&fonts);
    if (!ok && st->error && st->error_capacity && !st->error[0])
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: invalid, recursive, or unsupported Type 3 glyph");
    return ok;
}

int cupidimage_pdf_draw_form(pdf_render_state *st, pdf_object_id object,
                             const pdf_object_view *form) {
    size_t pos, length;
    int type = 1, ok = 0;
    const unsigned char *stream;
    unsigned char *inflated = NULL;
    pdf_render_state *child = NULL;
    pdf_transparency_group group = {0};
    pdf_font_table fonts;
    cupidimage_pdf_font_table_init(&fonts);
    if (st->form_depth >= 16 || !st->form_budget->calls_left)
        goto done;
    st->form_budget->calls_left--;
    for (int i = 0; i < st->form_depth; i++) {
        if (st->form_objects[i] == object)
            goto done;
    }
    if ((cupidimage_pdf_page_value(form, "FormType", &pos) &&
         (!cupidimage_pdf_image_integer(st, form, "FormType", &type) || type != 1)) ||
        cupidimage_pdf_page_value(form, "Ref", &pos))
        goto done;
    child = (pdf_render_state *)malloc(sizeof(*child));
    if (!child)
        goto done;
    *child = *st;
    child->text_group = NULL;
    child->in_text = 0;
    pdf_clip_retain(child->gs.clip);
    cupidimage_pdf_soft_mask_retain(child->gs.soft_mask);
    child->gs_stack_len = 0;
    child->text_clip = NULL;
    child->text_winding = NULL;
    child->text_winding_bytes = 0;
    child->pending_clip = 0;
    child->image_failed = 0;
    child->glyph_active = 0;
    cupidimage_pdf_path_clear(&child->path);
    pdf_form_clip *clip = &child->form_clips[st->form_depth];
    if (!cupidimage_pdf_page_value(form, "BBox", &pos) ||
        !cupidimage_pdf_page_rectangle(st->document, st->document_size, form, pos, clip->box))
        goto done;
    if (cupidimage_pdf_page_value(form, "Matrix", &pos)) {
        pdf_object_view indirect = {0};
        const pdf_object_view *matrix = form;
        pdf_object_id matrix_obj;
        if (pos < form->len && form->data[pos] != '[') {
            if (!cupidimage_pdf_page_reference(form, pos, &matrix_obj) ||
                !cupidimage_pdf_state_object(st, matrix_obj, &indirect))
                goto done;
            matrix = &indirect;
            pos = 0;
            cupidimage_pdf_skip_ws_and_comments(matrix->data, matrix->len, &pos);
        }
        float m[6];
        int valid = pos < matrix->len && matrix->data[pos++] == '[';
        for (int i = 0; valid && i < 6; i++) {
            double number;
            cupidimage_pdf_skip_ws_and_comments(matrix->data, matrix->len, &pos);
            valid = cupidimage_pdf_parse_pdf_number(matrix->data, matrix->len, &pos, &number) &&
                    fabs(number) <= 1e30;
            if (valid)
                m[i] = (float)number;
        }
        cupidimage_pdf_skip_ws_and_comments(matrix->data, matrix->len, &pos);
        valid = valid && pos < matrix->len && matrix->data[pos] == ']';
        cupidimage_pdf_object_view_release(&indirect);
        if (!valid)
            goto done;
        pdf_matrix_concat(child->gs.ctm, m);
    }
    const float *m = child->gs.ctm;
    for (int i = 0; i < 6; i++)
        if (!isfinite(m[i]))
            goto done;
    double det = (double)m[0] * m[3] - (double)m[1] * m[2];
    if (det == 0) {
        ok = 1;
        goto done;
    }
    double *inv = clip->inverse;
    inv[0] = m[3] / det;
    inv[1] = -m[1] / det;
    inv[2] = -m[2] / det;
    inv[3] = m[0] / det;
    inv[4] = -(inv[0] * m[4] + inv[2] * m[5]);
    inv[5] = -(inv[1] * m[4] + inv[3] * m[5]);
    child->form_objects[child->form_depth++] = object;
    pdf_object_view form_resources = {0};
    int local_resources = cupidimage_pdf_optional_dictionary(st->document, st->document_size, form,
                                                             "Resources", &form_resources);
    cupidimage_pdf_object_view_release(&form_resources);
    if (local_resources < 0)
        goto done;
    if (local_resources > 0) {
        child->page_object = object;
        child->resource_override = form;
        if (!cupidimage_pdf_collect_page_fonts_indexed(st->document, st->document_size, object,
                                                       &fonts, st->error, st->error_capacity,
                                                       st->xref))
            goto done;
        child->font_table = &fonts;
    }
    if (!cupidimage_pdf_image_stream(st, form, &stream, &length) ||
        !cupidimage_pdf_decode_stream(form, stream, length, &inflated, &length))
        goto done;
    stream = inflated;
    if (length > st->form_budget->bytes_left)
        goto done;
    st->form_budget->bytes_left -= length;
    if (!cupidimage_pdf_group_begin(child, st, form, &group))
        goto done;
    child->initial_gs = child->gs;
    child->initial_gs.soft_mask = NULL;
    child->initial_gs.clip = NULL;
    ok = cupidimage_pdf_render_stream(child, stream, length);
    if (ok)
        ok = cupidimage_pdf_group_finish(st, &group);
done:
    cupidimage_pdf_font_table_free(&fonts);
    free(inflated);
    if (child)
        cupidimage_pdf_render_state_release(child);
    free(child);
    cupidimage_pdf_group_free(st, &group);
    if (!ok)
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: invalid, recursive, or unsupported Form XObject");
    return ok;
}

int cupidimage_pdf_render_content_stream_options(
    const unsigned char *stream, size_t stream_len, const unsigned char *document,
    size_t document_size, pdf_object_id page_object, const pdf_font_table *font_table,
    cupidimage_image *img, const pdf_page_geometry *geometry, char *err, size_t errcap,
    const pdf_xref *xref, const cupidimage_pdf_options *opts) {
    pdf_render_state st = {0};
    st.text_antialias = opts ? opts->text_antialias : 0;
    st.truetype_hinting = opts ? opts->truetype_hinting : 0;
    st.cff_hinting = opts ? opts->cff_hinting : 0;
    st.type1_hinting = opts ? opts->type1_hinting : 0;
    pdf_clip_budget clip_budget = {0, 256u * 1024u * 1024u, 4096};
    st.clip_budget = &clip_budget;
    st.xref = xref;
    pdf_form_budget form_budget = {
        64u * 1024u * 1024u, 4096, 128u * 1024u * 1024u, 65536, 256u * 1024u * 1024u, 0, 0,
        16u * 1024u * 1024u, NULL};
    st.form_budget = &form_budget;
    pdf_pattern_store patterns = {0};
    st.patterns = &patterns;
    pdf_icc_store profiles = {0};
    st.icc_store = &profiles;
    pdf_tint_store tints = {0};
    st.tints = &tints;
    pdf_palette_store palettes = {0};
    st.palettes = &palettes;
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
    st.gs.ctm[0] = st.gs.ctm[3] = (float)geometry->user_unit;
    st.gs.line_width_pts = 1.0f;
    st.gs.miter_limit = 10;
    st.gs.flatness = 1;
    st.gs.fill_alpha = st.gs.stroke_alpha = 1;
    st.gs.text_knockout = 1;
    st.gs.fill_space.components = st.gs.stroke_space.components = 1;
    st.gs_stack_len = 0;
    cupidimage_pdf_path_clear(&st.path);
    st.in_text = 0;
    st.font_table = font_table;
    st.gs.current_font = NULL;

    st.initial_gs = st.gs;
    st.initial_gs.soft_mask = NULL;
    st.initial_gs.clip = NULL;
    pdf_transparency_group page_group = {0};
    int ok = cupidimage_pdf_page_group_begin(&st, &page_group);
    if (ok)
        ok = cupidimage_pdf_render_stream(&st, stream, stream_len);
    cupidimage_pdf_render_state_release(&st);
    /* Page paint state must not mask, clip or blend the final medium composite. */
    st.gs = st.initial_gs;
    st.img = img;
    st.group = NULL;
    if (ok)
        ok = cupidimage_pdf_group_finish(&st, &page_group);
    cupidimage_pdf_group_free(&st, &page_group);
    cupidimage_pdf_patterns_free(&patterns);
    cupidimage_pdf_palettes_free(&palettes);
    cupidimage_pdf_tint_free(&tints);
    cupidimage_pdf_icc_free(&profiles);
    cupidimage_pdf_page_hint_free(&form_budget.hint_cache);
    if (!ok && err && errcap && !err[0])
        cupidimage_set_err(err, errcap,
                           "pdf: unsupported page group or transparency limit exceeded");
    return ok;
}

int cupidimage_pdf_render_content_stream_indexed(const unsigned char *stream, size_t stream_len,
                                                 const unsigned char *document,
                                                 size_t document_size, pdf_object_id page_object,
                                                 const pdf_font_table *font_table,
                                                 cupidimage_image *img,
                                                 const pdf_page_geometry *geometry, char *err,
                                                 size_t errcap, const pdf_xref *xref) {
    return cupidimage_pdf_render_content_stream_options(stream, stream_len, document, document_size,
                                                        page_object, font_table, img, geometry, err,
                                                        errcap, xref, NULL);
}

int cupidimage_pdf_render_content_stream(const unsigned char *stream, size_t stream_len,
                                         const unsigned char *document, size_t document_size,
                                         pdf_object_id page_object,
                                         const pdf_font_table *font_table, cupidimage_image *img,
                                         const pdf_page_geometry *geometry, char *err,
                                         size_t errcap) {
    return cupidimage_pdf_render_content_stream_indexed(stream, stream_len, document, document_size,
                                                        page_object, font_table, img, geometry, err,
                                                        errcap, NULL);
}
