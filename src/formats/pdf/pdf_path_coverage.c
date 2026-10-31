#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>

static int path_fill_coverage(pdf_render_state *st, const pdf_path *path, unsigned even_odd,
                              int clipped, int left, int top, int right, int bottom,
                              unsigned char **output) {
    pdf_region_edge *edges = NULL;
    unsigned count = 0;
    unsigned char *mask = NULL;
    int ok = cupidimage_pdf_path_fill_edges(path, &st->clip_budget->tests_left, &edges, &count);
    unsigned char rule = (unsigned char)even_odd;
    if (ok)
        ok = cupidimage_pdf_region_coverage(edges, count, &rule, 1, left, top, right, bottom,
                                            &st->clip_budget->tests_left, output);
    if (ok && clipped) {
        ok = cupidimage_pdf_clip_intersect(st, edges, count, even_odd, NULL, 0, NULL, left, top,
                                           right, bottom, &mask);
        if (ok)
            ok = cupidimage_pdf_clip_limit_mask(st, *output, 0, mask, left, top, right, bottom);
        if (ok) {
            free(*output);
            *output = mask;
            mask = NULL;
        }
    }
    free(edges);
    free(mask);
    if (!ok) {
        free(*output);
        *output = NULL;
    }
    return ok;
}

int cupidimage_pdf_paint_path_coverage(pdf_render_state *st, const pdf_path *path, int fill,
                                       int stroke, int even_odd) {
    unsigned char *fill_mask = NULL, *stroke_mask = NULL;
    pdf_paint_group combined = {0};
    int ok = 0, saved_clip = st->paint_coverage_clipped;
    fill = fill && !cupidimage_pdf_empty_color(&st->gs.fill_space, st->gs.fill_pattern);
    stroke = stroke && !cupidimage_pdf_empty_color(&st->gs.stroke_space, st->gs.stroke_pattern);
    if (!fill && !stroke)
        return 1;
    if (!cupidimage_pdf_path_valid(path))
        goto done;
    if (!path->seg_count)
        return 1;
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (int i = 0; i < path->seg_count; i++) {
        const pdf_seg *edge = cupidimage_pdf_path_segments(path) + i;
        if (edge->curve || !isfinite(edge->x0) || !isfinite(edge->y0) || !isfinite(edge->x1) ||
            !isfinite(edge->y1))
            goto done;
        x0 = fmin(x0, fmin(edge->x0, edge->x1));
        x1 = fmax(x1, fmax(edge->x0, edge->x1));
        y0 = fmin(y0, fmin(edge->y0, edge->y1));
        y1 = fmax(y1, fmax(edge->y0, edge->y1));
    }
    /* Filled point subpaths enclose a device pixel, including at integer origins. */
    if (fill) {
        x1 = floor(x1) + 1;
        y1 = floor(y1) + 1;
    }
    if (stroke) {
        const float *m = st->gs.ctm;
        double pen = fmax(hypot(m[0], m[2]), hypot(m[1], m[3]));
        double extent = fmax(st->gs.miter_limit, st->gs.line_cap == 2 ? sqrt(2.0) : 1.0);
        double margin = st->gs.line_width_pts
                            ? pen * st->gs.line_width_pts * (96.0 / 72.0) * .5 * extent + 1
                            : 1;
        if (!isfinite(margin))
            goto done;
        x0 -= margin;
        x1 += margin;
        y0 -= margin;
        y1 += margin;
    }
    int left = (int)fmax(0, fmin(st->img->width, floor(x0))),
        right = (int)fmax(0, fmin(st->img->width, ceil(x1)));
    int top = (int)fmax(0, fmin(st->img->height, floor(y0))),
        bottom = (int)fmax(0, fmin(st->img->height, ceil(y1)));
    /* Nothing outside the inherited clip can be painted. Limit the raster
     * window before allocating or intersecting coverage, while retaining the
     * original edges and exact fractional clipping inside that window. */
    if (st->gs.clip && st->gs.clip->geometry && st->gs.clip->coverage &&
        !st->paint_coverage_clipped) {
        const double *bounds = st->gs.clip->geometry->bounds;
        left = (int)fmax(left, fmin(st->img->width, floor(bounds[0])));
        top = (int)fmax(top, fmin(st->img->height, floor(bounds[1])));
        right = (int)fmin(right, fmax(0, ceil(bounds[2])));
        bottom = (int)fmin(bottom, fmax(0, ceil(bounds[3])));
    }
    if (left >= right || top >= bottom)
        return 1;
    int clipped = st->gs.clip && st->gs.clip->coverage &&
                  !cupidimage_pdf_clip_contains_crop(st, left, top, right, bottom);
    if (fill && !path_fill_coverage(st, path, (unsigned)even_odd, clipped, left, top, right, bottom,
                                    &fill_mask))
        goto done;
    if (stroke && !cupidimage_pdf_stroke_path_coverage(st, path, left, top, right, bottom, clipped,
                                                       &stroke_mask))
        goto done;
    if (fill && stroke && !cupidimage_pdf_fill_stroke_begin(st, &combined, 1))
        goto done;
    st->paint_coverage_clipped = clipped;
    for (int y = top; y < bottom && !st->image_failed; y++)
        for (int x = left; x < right && !st->image_failed; x++) {
            size_t pixel = (size_t)(y - top) * (right - left) + x - left;
            unsigned f = fill_mask ? fill_mask[pixel] : 0, s = stroke_mask ? stroke_mask[pixel] : 0;
            if (f && (s < 255 || combined.surface.image.rgba))
                cupidimage_pdf_paint_pixel(st, x, y, 0, f / 255.0);
            if (s)
                cupidimage_pdf_paint_pixel(st, x, y, 1, s / 255.0);
        }
    ok = !st->image_failed;
done:
    if (!cupidimage_pdf_fill_stroke_end(st, &combined, ok))
        ok = 0;
    st->paint_coverage_clipped = saved_clip;
    free(fill_mask);
    free(stroke_mask);
    if (!ok) {
        st->image_failed = 1;
        if (!st->error || !st->error_capacity || !st->error[0])
            cupidimage_set_err(st->error, st->error_capacity,
                               "pdf: path coverage exceeds geometry or work limits");
    }
    return ok;
}
