#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int cupidimage_pdf_clip_append_edge_to(pdf_render_state *st, pdf_clip_mask *clip, double x0,
                                       double y0, double x1, double y1) {
    size_t *work = &st->clip_budget->tests_left;
    if (!clip || clip->pending_count == 65536 || !*work || !isfinite(x0) || !isfinite(y0) ||
        !isfinite(x1) || !isfinite(y1))
        return 0;
    (*work)--;
    if (clip->pending_count == clip->pending_capacity) {
        unsigned capacity = clip->pending_capacity ? clip->pending_capacity * 2 : 64;
        size_t extra = (size_t)(capacity - clip->pending_capacity) * sizeof(*clip->pending_edges);
        if (st->clip_budget->bytes_used > 64u * 1024u * 1024u ||
            extra > 64u * 1024u * 1024u - st->clip_budget->bytes_used ||
            clip->pending_count > *work)
            return 0;
        *work -= clip->pending_count;
        pdf_region_edge *next = realloc(clip->pending_edges, (size_t)capacity * sizeof(*next));
        if (!next)
            return 0;
        clip->pending_edges = next;
        clip->pending_capacity = capacity;
        st->clip_budget->bytes_used += extra;
    }
    clip->pending_edges[clip->pending_count++] = (pdf_region_edge){x0, y0, x1, y1, 0};
    return 1;
}

int cupidimage_pdf_clip_finish_geometry(pdf_render_state *st, pdf_clip_mask *clip) {
    pdf_clip_geometry *parent = st->gs.clip ? st->gs.clip->geometry : NULL;
    pdf_clip_geometry *geometry = cupidimage_pdf_clip_geometry_create(
        parent, clip->pending_edges, clip->pending_count, 0, &st->clip_budget->bytes_used,
        &st->clip_budget->tests_left);
    if (!geometry)
        return 0;
    clip->geometry = geometry;
    st->clip_budget->bytes_used -= (size_t)clip->pending_capacity * sizeof(*clip->pending_edges);
    free(clip->pending_edges);
    clip->pending_edges = NULL;
    clip->pending_count = clip->pending_capacity = 0;
    clip->captured_form_depth = (unsigned)st->form_depth;
    if (st->gs.clip && clip->geometry == st->gs.clip->geometry &&
        clip->captured_form_depth == st->gs.clip->captured_form_depth)
        return 1;
    return cupidimage_pdf_clip_render_geometry(st, clip);
}

int cupidimage_pdf_clip_path_geometry(pdf_render_state *st, pdf_clip_mask *clip,
                                      const pdf_path *path, int even_odd) {
    pdf_region_edge *edges = NULL;
    unsigned count = 0;
    if (!cupidimage_pdf_path_fill_edges(path, &st->clip_budget->tests_left, &edges, &count))
        return 0;
    pdf_clip_geometry *parent = st->gs.clip ? st->gs.clip->geometry : NULL;
    clip->geometry = cupidimage_pdf_clip_geometry_create(parent, edges, count, (unsigned)even_odd,
                                                         &st->clip_budget->bytes_used,
                                                         &st->clip_budget->tests_left);
    free(edges);
    clip->captured_form_depth = (unsigned)st->form_depth;
    if (st->gs.clip && clip->geometry && clip->geometry == st->gs.clip->geometry &&
        clip->captured_form_depth == st->gs.clip->captured_form_depth)
        return 1;
    return clip->geometry && cupidimage_pdf_clip_render_geometry(st, clip);
}

int cupidimage_pdf_clip_append_edge(pdf_render_state *st, double x0, double y0, double x1,
                                    double y1) {
    return cupidimage_pdf_clip_append_edge_to(st, st->text_clip, x0, y0, x1, y1);
}

void cupidimage_pdf_clip_pending_clear(pdf_render_state *st, pdf_clip_mask *clip) {
    st->clip_budget->bytes_used -= (size_t)clip->pending_capacity * sizeof(*clip->pending_edges);
    free(clip->pending_edges);
    clip->pending_edges = NULL;
    clip->pending_count = clip->pending_capacity = 0;
}

int cupidimage_pdf_clip_render_geometry(pdf_render_state *st, pdf_clip_mask *clip) {
    size_t pixels = (size_t)st->img->width * st->img->height;
    if (!clip->geometry || clip->coverage || pixels > 16u * 1024u * 1024u ||
        pixels > st->clip_budget->tests_left || st->clip_budget->bytes_used > 64u * 1024u * 1024u ||
        pixels > 64u * 1024u * 1024u - st->clip_budget->bytes_used)
        return 0;
    st->clip_budget->tests_left -= pixels;
    clip->coverage = calloc(pixels ? pixels : 1, 1);
    if (!clip->coverage)
        return 0;
    clip->coverage_bytes = pixels;
    st->clip_budget->bytes_used += pixels;
    memset(clip->bits, 0, clip->bytes);
    const double *b = clip->geometry->bounds;
    if (b[2] <= b[0] || b[3] <= b[1])
        return 1;
    int left = (int)fmax(0, fmin(st->img->width, floor(b[0]))),
        right = (int)fmax(0, fmin(st->img->width, ceil(b[2])));
    int top = (int)fmax(0, fmin(st->img->height, floor(b[1]))),
        bottom = (int)fmax(0, fmin(st->img->height, ceil(b[3])));
    if (right <= left || bottom <= top)
        return 1;
    if (clip->geometry->rectangle) {
        for (int y = top; y < bottom; y++) {
            long double height = fmaxl(0, fminl(b[3], y + 1) - fmaxl(b[1], y));
            for (int x = left; x < right; x++) {
                long double width = fmaxl(0, fminl(b[2], x + 1) - fmaxl(b[0], x));
                unsigned char alpha = (unsigned char)fminl(255, floorl(width * height * 256));
                size_t pixel = (size_t)y * st->img->width + x;
                clip->coverage[pixel] = alpha;
                if (alpha)
                    clip->bits[pixel / 8] |= (unsigned char)(1u << (pixel % 8));
            }
        }
        return 1;
    }
    unsigned char rules[64] = {0}, *mask = NULL;
    pdf_region_edge *edges = NULL;
    unsigned count = 0;
    int ok = cupidimage_pdf_clip_geometry_collect(clip->geometry, 0, rules, &edges, &count,
                                                  &st->clip_budget->tests_left);
    if (ok)
        ok = cupidimage_pdf_region_coverage(edges, count, rules, clip->geometry->depth, left, top,
                                            right, bottom, &st->clip_budget->tests_left, &mask);
    if (ok)
        for (int y = top; y < bottom; y++)
            memcpy(clip->coverage + (size_t)y * st->img->width + left,
                   mask + (size_t)(y - top) * (right - left), (size_t)(right - left));
    if (ok)
        for (size_t i = 0; i < pixels; i++)
            if (clip->coverage[i])
                clip->bits[i / 8] |= (unsigned char)(1u << (i % 8));
    free(edges);
    free(mask);
    return ok;
}

int cupidimage_pdf_clip_contains_crop(pdf_render_state *st, int left, int top, int right,
                                      int bottom) {
    pdf_clip_mask *clip = st->gs.clip;
    if (!clip || !clip->coverage)
        return 1;
    if (clip->geometry && clip->geometry->rectangle) {
        if (!st->clip_budget->tests_left)
            return 0;
        st->clip_budget->tests_left--;
        const double *bounds = clip->geometry->bounds;
        return bounds[0] <= left && bounds[1] <= top && bounds[2] >= right && bounds[3] >= bottom;
    }
    size_t area = (size_t)(right - left) * (bottom - top);
    if (area > st->clip_budget->tests_left)
        return 0;
    st->clip_budget->tests_left -= area;
    for (int y = top; y < bottom; y++)
        for (int x = left; x < right; x++)
            if (clip->coverage[(size_t)y * st->img->width + x] != 255)
                return 0;
    return 1;
}

int cupidimage_pdf_clip_intersect(pdf_render_state *st, const pdf_region_edge *draw,
                                  unsigned draw_count, unsigned even_odd,
                                  const pdf_region_ellipse *ellipses, unsigned ellipse_count,
                                  const double axes[4], int left, int top, int right, int bottom,
                                  unsigned char **output) {
    *output = NULL;
    const pdf_clip_geometry *geometry = st->gs.clip ? st->gs.clip->geometry : NULL;
    if (!geometry || even_odd > 1 || draw_count > 65536 ||
        geometry->total_edges > 65536 - draw_count)
        return 0;
    unsigned char rules[64] = {0};
    rules[0] = (unsigned char)even_odd;
    pdf_region_edge *edges = NULL;
    unsigned count = 0;
    if (!cupidimage_pdf_clip_geometry_collect(geometry, 1, rules, &edges, &count,
                                              &st->clip_budget->tests_left))
        return 0;
    if (count + draw_count > st->clip_budget->tests_left) {
        free(edges);
        return 0;
    }
    st->clip_budget->tests_left -= count + draw_count;
    pdf_region_edge *next =
        realloc(edges, (size_t)(count + draw_count ? count + draw_count : 1) * sizeof(*next));
    if (!next) {
        free(edges);
        return 0;
    }
    edges = next;
    for (unsigned i = 0; i < draw_count; i++) {
        edges[count + i] = draw[i];
        edges[count + i].region = 0;
    }
    int ok = cupidimage_pdf_region_coverage_ellipses(
        edges, count + draw_count, ellipses, ellipse_count, axes, rules, geometry->depth + 1, left,
        top, right, bottom, &st->clip_budget->tests_left, output);
    free(edges);
    return ok;
}

int cupidimage_pdf_clip_limit_mask(pdf_render_state *st, const unsigned char *source, int bottom_up,
                                   unsigned char *mask, int left, int top, int right, int bottom) {
    size_t area = (size_t)(right - left) * (bottom - top);
    if (!st->gs.clip || !st->gs.clip->coverage || area > st->clip_budget->tests_left)
        return 0;
    st->clip_budget->tests_left -= area;
    for (int y = top; y < bottom; y++)
        for (int x = left; x < right; x++) {
            size_t index = (size_t)(y - top) * (right - left) + x - left;
            unsigned original =
                source[(size_t)(bottom_up ? bottom - y - 1 : y - top) * (right - left) + x - left];
            unsigned clip = st->gs.clip->coverage[(size_t)y * st->img->width + x];
            if (clip == 255)
                mask[index] = (unsigned char)original;
            else if (!clip)
                mask[index] = 0;
            else if (mask[index] > original)
                mask[index] = (unsigned char)original;
        }
    return 1;
}

int cupidimage_pdf_clip_capture_forms(pdf_render_state *st) {
    if (!cupidimage_pdf_clip_antialias(st))
        return 1;
    unsigned first = st->gs.clip ? st->gs.clip->captured_form_depth : 0;
    for (unsigned i = first; i < (unsigned)st->form_depth; i++) {
        const pdf_form_clip *form = st->form_clips + i;
        const double *m = form->inverse;
        double det = m[0] * m[3] - m[1] * m[2];
        if (!isfinite(det) || !det)
            return 0;
        double gx[4] = {form->box[0], form->box[2], form->box[2], form->box[0]};
        double gy[4] = {form->box[1], form->box[1], form->box[3], form->box[3]}, x[4], y[4];
        for (unsigned j = 0; j < 4; j++) {
            double dx = gx[j] - m[4], dy = gy[j] - m[5];
            x[j] = ((m[3] * dx - m[2] * dy) / det - st->page_x0_pts) * (96.0 / 72.0);
            y[j] = (st->page_top_pts - (-m[1] * dx + m[0] * dy) / det) * (96.0 / 72.0);
        }
        pdf_region_edge edges[4];
        for (unsigned j = 0; j < 4; j++)
            edges[j] = (pdf_region_edge){x[j], y[j], x[(j + 1) % 4], y[(j + 1) % 4], 0};
        size_t bytes = ((size_t)st->img->width * st->img->height + 7) / 8;
        if (!st->clip_budget->calls_left || st->clip_budget->bytes_used > 64u * 1024u * 1024u ||
            bytes > 64u * 1024u * 1024u - st->clip_budget->bytes_used)
            return 0;
        st->clip_budget->calls_left--;
        pdf_clip_mask *clip = calloc(1, sizeof(*clip) + bytes);
        if (!clip)
            return 0;
        clip->references = 1;
        clip->bytes = bytes;
        st->clip_budget->bytes_used += bytes;
        clip->geometry = cupidimage_pdf_clip_geometry_create(
            st->gs.clip ? st->gs.clip->geometry : NULL, edges, 4, 0, &st->clip_budget->bytes_used,
            &st->clip_budget->tests_left);
        if (!clip->geometry || !cupidimage_pdf_clip_render_geometry(st, clip)) {
            cupidimage_pdf_clip_release(st, clip);
            return 0;
        }
        clip->captured_form_depth = i + 1;
        cupidimage_pdf_clip_release(st, st->gs.clip);
        st->gs.clip = clip;
    }
    return 1;
}
